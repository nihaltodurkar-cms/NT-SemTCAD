"""Verify a staged (or dev) Python runtime + backend for the native desktop app.

NATIVE-DESKTOP-PLAN.md section 26.3, P5-S2. Run it WITH THE RUNTIME'S OWN
interpreter, from anywhere; it re-executes itself in a scrubbed environment
(the same PATH the app gives its Python children, see
`pythonProcessEnvironment` in desktop/src/backend/backend_client.cpp):

    runtime\\python.exe desktop\\tools\\check_runtime.py --backend backend

Gates (each prints PASS / FAIL / WARN / SKIP; exit status 1 on any FAIL):

    layout       backend\\ holds the packages the app launches, and the
                 compiled pytcad\\_core extension
    interpreter  this interpreter is the runtime's own (sys.prefix)
    closure      every module the backend entry points import -- the five
                 launched modules plus everything backend_service/server.py
                 imports lazily -- resolves INSIDE the runtime or backend
    addons       gmsh / scikit-image / tetgen are NOT in the core runtime
                 (decision 26.4-4; --addon flips this to "must be present")
    extension    pytcad._core imports and lives in backend\\pytcad
    pardiso      MKL PARDISO loads, from the runtime's own mkl_rt
    handshake    `python -m backend_service` answers system.ping/info/warmup
                 and reports THIS runtime as its interpreter
    examples     diode_1d, mosfet_2d, resistor_3d run through
                 gui.services.solver_runner and produce valid result files
    reference    versions (python, numpy, scipy, pyamg, mkl, BLAS) and the
                 example results match a reference emitted from tcad-dev

Reference workflow (two runs, two interpreters):

    tcad-dev\\python.exe   check_runtime.py --backend <repo>\\pytcad --emit-reference ref\\
    runtime\\python.exe    check_runtime.py --backend backend --reference ref\\

The script is stdlib-only until the gates import the backend, so a broken
runtime fails a gate rather than the script.
"""
import argparse
import ast
import glob
import importlib
import importlib.util
import json
import os
import subprocess
import sys
import sysconfig
import time

EXAMPLES = ("diode_1d", "mosfet_2d", "resistor_3d")
# Optional packages that must stay OUT of the core runtime (decision 26.4-4:
# gmsh is GPL-2.0+, tetgen wraps AGPL-3.0 TetGen, scikit-image rides with
# them for the 3D process geometry). Import names, not conda names.
ADDON_PACKAGES = ("gmsh", "skimage", "tetgen")
# The modules the app launches (job_runner / BackendClient), from
# desktop/src/run/*.cpp and backend_client.cpp.
ENTRY_MODULES = ("backend_service.server",
                 "gui.services.solver_runner",
                 "gui.services.moscap_runner",
                 "gui.services.compact_runner",
                 "gui.services.process_runner")
LOCAL_PACKAGES = ("backend_service", "gui", "pytcad", "workbench")
# Site hooks some hosts install; not part of the runtime under test.
IGNORED_MODULES = ("sitecustomize", "usercustomize")
CHILD_FLAG = "TCAD_CHECK_RUNTIME_CHILD"


# -- results -----------------------------------------------------------------

class Report:
    def __init__(self):
        self.rows = []

    def add(self, status, gate, detail=""):
        assert status in ("PASS", "FAIL", "WARN", "SKIP")
        self.rows.append({"status": status, "gate": gate, "detail": detail})
        print(f"{status:<4}  {gate:<12} {detail}", flush=True)

    @property
    def failed(self):
        return any(r["status"] == "FAIL" for r in self.rows)


# -- environment -------------------------------------------------------------

def prefix_path_dirs(prefix, windows=None):
    """The directories `conda activate` would put on PATH for `prefix`,
    in order -- the C++ list in backend_client.cpp's pythonProcessEnvironment
    (a non-activated conda interpreter aborts on its first LAPACK call
    without them: MKL loads its threading DLLs from PATH, exit 0xC06D007F).
    """
    if windows is None:
        windows = os.name == "nt"
    subs = (".", "Library/mingw-w64/bin", "Library/usr/bin", "Library/bin",
            "Scripts", "bin")
    dirs = []
    for sub in subs:
        d = os.path.normpath(os.path.join(prefix, sub))
        if os.path.isdir(d) and d not in dirs:
            dirs.append(d)
    return dirs


def scrubbed_env(prefix, base=None):
    """A minimal child environment: the runtime's own directories plus the
    operating system's, nothing inherited from any conda env. PYTCAD_*
    variables pass through (thread counts, backend selection)."""
    base = os.environ if base is None else base
    env = {}
    if os.name == "nt":
        root = base.get("SystemRoot", r"C:\Windows")
        system = [os.path.join(root, "System32"), root]
        for key in ("SystemRoot", "WINDIR", "TEMP", "TMP", "COMSPEC", "PATHEXT",
                    "USERPROFILE", "LOCALAPPDATA", "APPDATA", "ProgramData"):
            if key in base:
                env[key] = base[key]
    else:
        system = ["/usr/bin", "/bin"]
        for key in ("HOME", "TMPDIR", "LANG", "LC_ALL"):
            if key in base:
                env[key] = base[key]
    env["PATH"] = os.pathsep.join(prefix_path_dirs(prefix) + system)
    for key, val in base.items():
        if key.startswith("PYTCAD_") or key == "TCAD_BACKEND_DEBUG":
            env[key] = val
    # Deterministic solves: any fixed thread count reproduces run to run,
    # different counts differ at round-off (linsolve.pardiso_threads).
    env.setdefault("PYTCAD_PARDISO_THREADS", "1")
    env.setdefault("PYTHONUTF8", "1")
    env.setdefault("OPENBLAS_NUM_THREADS", "1")
    return env


def is_within(path, roots):
    """True when `path` resolves inside any of `roots` (symlinks and case
    normalised)."""
    p = os.path.normcase(os.path.realpath(path))
    for r in roots:
        rr = os.path.normcase(os.path.realpath(r))
        if p == rr or p.startswith(rr.rstrip(os.sep) + os.sep):
            return True
    return False


def dir_size_mb(path):
    total = 0
    for base, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(base, f))
            except OSError:
                pass
    return total / (1024 * 1024)


# -- import closure ----------------------------------------------------------

def lazy_imports(server_py):
    """Every `gui.*` / `workbench.*` / `pytcad.*` module backend_service/
    server.py imports (function-local imports included), by AST -- so the
    gate follows the file instead of a hand-kept list. `from gui.services
    import examples` names the module gui.services.examples."""
    with open(server_py, "r", encoding="utf-8") as fh:
        tree = ast.parse(fh.read(), server_py)
    found = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for a in node.names:
                if a.name.split(".")[0] in LOCAL_PACKAGES:
                    found.add(a.name)
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            if node.module.split(".")[0] in LOCAL_PACKAGES:
                found.add(node.module)
                for a in node.names:
                    found.add(f"{node.module}.{a.name}")
    return sorted(found)


def try_import(name):
    """Import `name`; a name that is an attribute (a class) rather than a
    module is not a failure -- `from gui.services.device_spec import
    DeviceSpec` yields the candidate gui.services.device_spec.DeviceSpec.
    Returns (ok, error)."""
    try:
        importlib.import_module(name)
        return True, ""
    except ModuleNotFoundError as exc:
        parent = name.rpartition(".")[0]
        if parent and exc.name == name:
            try:
                mod = importlib.import_module(parent)
            except Exception as exc2:            # the parent itself is broken
                return False, f"{type(exc2).__name__}: {exc2}"
            if hasattr(mod, name.rpartition(".")[2]):
                return True, ""
        return False, f"ModuleNotFoundError: {exc}"
    except Exception as exc:
        return False, f"{type(exc).__name__}: {exc}"


def outside_modules(roots, allow_extra=()):
    """Loaded modules whose file is not inside `roots` (or the stdlib)."""
    stdlib = {sysconfig.get_paths().get(k) for k in ("stdlib", "platstdlib")}
    stdlib.discard(None)
    allowed = list(roots) + sorted(stdlib) + list(allow_extra)
    bad = []
    for name, mod in sorted(sys.modules.items()):
        if name.split(".")[0] in IGNORED_MODULES or name == "__main__":
            continue
        f = getattr(mod, "__file__", None)
        if not f:
            continue
        if not is_within(f, allowed):
            bad.append((name, f))
    return bad


def third_party_loaded():
    std = set(sys.stdlib_module_names)
    return sorted({k.split(".")[0] for k in sys.modules
                   if k.split(".")[0] not in std and not k.startswith("_")
                   and k.split(".")[0] not in LOCAL_PACKAGES
                   and k.split(".")[0] not in IGNORED_MODULES
                   and k.split(".")[0] != "cython_runtime"})


# -- versions ----------------------------------------------------------------

def mkl_conda_version(prefix):
    """The conda `mkl` package's version from conda-meta (mkl has no
    dist-info); None when the prefix is not a conda prefix."""
    hits = sorted(glob.glob(os.path.join(prefix, "conda-meta", "mkl-[0-9]*.json")))
    if not hits:
        return None
    return os.path.basename(hits[-1])[len("mkl-"):-len(".json")]


def blas_name():
    try:
        import numpy as np
        cfg = np.show_config(mode="dicts")
        return cfg["Build Dependencies"]["blas"]["name"]
    except Exception:
        return None


def collect_versions(prefix):
    import importlib.metadata as md

    def ver(pkg):
        try:
            return md.version(pkg)
        except md.PackageNotFoundError:
            return None
    return {"python": "%d.%d.%d" % sys.version_info[:3],
            "numpy": ver("numpy"), "scipy": ver("scipy"), "pyamg": ver("pyamg"),
            "mkl": mkl_conda_version(prefix), "blas": blas_name()}


def compare_versions(ours, reference):
    """[(key, ours, reference)] for every reference value that differs. Python
    is compared to major.minor.micro (the same build the suite ran against)."""
    diffs = []
    for key, ref in reference.items():
        if ref is None:
            continue
        if ours.get(key) != ref:
            diffs.append((key, ours.get(key), ref))
    return diffs


# Provenance fields that differ between any two runs by construction.
VOLATILE_META_KEYS = ("created_utc",)


def normalized_meta(value):
    """record__meta is a JSON string (RunRecord provenance): drop the fields
    that are different on every run (the timestamp) and keep the rest --
    the models and numerics settings are worth comparing."""
    import numpy as np
    text = str(np.asarray(value).item())
    try:
        d = json.loads(text)
    except ValueError:
        return text
    if isinstance(d, dict):
        for k in VOLATILE_META_KEYS:
            d.pop(k, None)
    return json.dumps(d, sort_keys=True)


def compare_arrays(a, b, rtol, atol_rel=1e-12):
    """Compare two result dicts of numpy arrays. Returns (max_rel_diff,
    problems). A float array is compared against its own scale: |a-b| <=
    rtol * max|b| + atol_rel * max|b|; non-float arrays must be equal."""
    import numpy as np
    problems = []
    worst = 0.0
    if set(a) != set(b):
        problems.append("key sets differ: only-ours=%s only-ref=%s"
                        % (sorted(set(a) - set(b)), sorted(set(b) - set(a))))
    for key in sorted(set(a) & set(b)):
        if key == "record__meta":
            if normalized_meta(a[key]) != normalized_meta(b[key]):
                problems.append("record__meta differs (beyond the timestamp)")
            continue
        x, y = np.asarray(a[key]), np.asarray(b[key])
        if x.shape != y.shape:
            problems.append(f"{key}: shape {x.shape} != {y.shape}")
            continue
        if x.dtype.kind in "fc" and y.dtype.kind in "fc":
            scale = float(np.max(np.abs(y))) if y.size else 0.0
            diff = float(np.max(np.abs(x - y))) if y.size else 0.0
            rel = diff / scale if scale > 0 else diff
            worst = max(worst, rel)
            if not (diff <= (rtol + atol_rel) * scale or diff == 0.0):
                problems.append(f"{key}: max|diff|={diff:.3e} scale={scale:.3e} rel={rel:.3e}")
        elif not np.array_equal(x, y):
            problems.append(f"{key}: non-float arrays differ")
    return worst, problems


# -- gates -------------------------------------------------------------------

def gate_layout(rep, backend):
    need = [("pytcad", "__init__.py"), ("gui", "__init__.py"),
            ("gui", "services", "solver_runner.py"),
            ("backend_service", "__main__.py"), ("backend_service", "server.py"),
            ("workbench", "__init__.py")]
    missing = [os.path.join(*p) for p in need
               if not os.path.isfile(os.path.join(backend, *p))]
    ext = glob.glob(os.path.join(backend, "pytcad", "_core*.pyd")) \
        + glob.glob(os.path.join(backend, "pytcad", "_core*.so"))
    if missing:
        rep.add("FAIL", "layout", "missing from backend: " + ", ".join(missing))
    elif not ext:
        rep.add("FAIL", "layout", "no pytcad/_core*.pyd in backend (Device1D and every "
                                  "compiled kernel need it; build it first, CLAUDE.md)")
    else:
        rep.add("PASS", "layout", "packages present; extension " + os.path.basename(ext[0]))
    # nothing that should never ship
    stray = [d for d in ("tests", "benchmarks", "build", os.path.join("pytcad", "benchmarks"))
             if os.path.isdir(os.path.join(backend, d))]
    stray += [os.path.join("gui", "tests")] if os.path.isdir(os.path.join(backend, "gui", "tests")) else []
    if stray:
        rep.add("WARN", "layout", "test/dev directories in backend: " + ", ".join(stray))


def gate_interpreter(rep, runtime):
    if not runtime:
        rep.add("SKIP", "interpreter", "no --runtime given; using " + sys.prefix)
        return
    # sys.executable is compared WITHOUT resolving symlinks: a venv's python is
    # a link to the base interpreter; a conda-pack runtime has no such link.
    exe = os.path.normcase(os.path.abspath(sys.executable))
    root = os.path.normcase(os.path.abspath(runtime)).rstrip(os.sep) + os.sep
    if exe.startswith(root) and os.path.normcase(os.path.realpath(sys.prefix)) \
            == os.path.normcase(os.path.realpath(runtime)):
        rep.add("PASS", "interpreter", f"{sys.executable} (Python {sys.version.split()[0]})")
    else:
        rep.add("FAIL", "interpreter",
                f"running {sys.executable} (prefix {sys.prefix}), not the runtime {runtime}")


def gate_closure(rep, backend, runtime):
    server_py = os.path.join(backend, "backend_service", "server.py")
    names = list(ENTRY_MODULES)
    try:
        names += lazy_imports(server_py)
    except (OSError, SyntaxError) as exc:
        rep.add("FAIL", "closure", f"cannot read {server_py}: {exc}")
        return
    names = sorted(set(names))
    failed = []
    for n in names:
        ok, err = try_import(n)
        if not ok:
            failed.append(f"{n}: {err}")
    if failed:
        rep.add("FAIL", "closure", f"{len(failed)} of {len(names)} module(s) do not import: "
                + "; ".join(failed[:5]))
        return
    roots = [backend] + ([runtime] if runtime else [sys.prefix])
    bad = outside_modules(roots)
    if bad:
        rep.add("FAIL", "closure", "loaded from OUTSIDE the runtime/backend: "
                + "; ".join(f"{n} <- {f}" for n, f in bad[:6]))
        return
    rep.add("PASS", "closure", f"{len(names)} modules import; third-party loaded: "
            + ", ".join(third_party_loaded()))
    # the whole-tree import walk is informational (modules the app never launches)


def gate_addons(rep, addon):
    present = [p for p in ADDON_PACKAGES if importlib.util.find_spec(p) is not None]
    if addon:
        absent = [p for p in ADDON_PACKAGES if p not in present]
        if absent:
            rep.add("FAIL", "addons", "add-on runtime is missing: " + ", ".join(absent))
        else:
            rep.add("PASS", "addons", "add-on packages present: " + ", ".join(present))
    elif present:
        rep.add("FAIL", "addons", "must NOT be in the core runtime (decision 26.4-4, GPL/AGPL): "
                + ", ".join(present))
    else:
        rep.add("PASS", "addons", "gmsh, scikit-image, tetgen absent from the core runtime")


def gate_extension(rep, backend):
    from pytcad import _accel
    if not _accel.HAVE_ACCEL:
        rep.add("FAIL", "extension", "pytcad._core does not import: " + _accel.status())
        return
    f = getattr(_accel.core, "__file__", "")
    if not is_within(f, [backend]):
        rep.add("FAIL", "extension", f"pytcad._core loaded from {f}, not from the backend")
        return
    rep.add("PASS", "extension", _accel.status())


def gate_pardiso(rep, runtime):
    from pytcad import linsolve
    if not linsolve.pardiso_available():
        rep.add("FAIL", "pardiso", "MKL PARDISO unavailable: the 2D/3D direct solves fall back to "
                "Eigen (2-16x slower, NATIVE-DESKTOP-PLAN.md 26.1); the runtime needs `mkl`")
        return
    path = linsolve._PARDISO_STATE.get("path", "")
    roots = [runtime] if runtime else [sys.prefix]
    if not is_within(path, roots):
        rep.add("FAIL", "pardiso", f"mkl_rt loaded from {path}, outside the runtime")
        return
    rep.add("PASS", "pardiso", f"{path}; threads={linsolve.pardiso_threads()}")


class Rpc:
    """Line-delimited JSON-RPC to `python -m backend_service`."""

    def __init__(self, backend, env, python=None):
        self.p = subprocess.Popen([python or sys.executable, "-u", "-m", "backend_service"],
                                  cwd=backend, env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True, encoding="utf-8")
        self.n = 0

    def call(self, method, params=None):
        self.n += 1
        req = {"jsonrpc": "2.0", "id": self.n, "method": method}
        if params is not None:
            req["params"] = params
        self.p.stdin.write(json.dumps(req) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError("backend closed its pipe: " + self.p.stderr.read()[-2000:])
        resp = json.loads(line)
        if "error" in resp:
            raise RuntimeError(f"{method}: {resp['error']}")
        return resp["result"]

    def close(self):
        try:
            self.call("system.shutdown")
        except Exception:
            pass
        try:
            self.p.stdin.close()
            self.p.wait(timeout=30)
        except Exception:
            self.p.kill()


def gate_handshake(rep, backend, env, runtime):
    rpc = None
    try:
        rpc = Rpc(backend, env)
        ping = rpc.call("system.ping")
        info = rpc.call("system.info")
        warm = rpc.call("system.warmup")
        want = os.path.normcase(os.path.realpath(runtime or sys.prefix))
        got = os.path.normcase(os.path.realpath(info.get("prefix", "")))
        if not ping.get("pong"):
            rep.add("FAIL", "handshake", f"system.ping said {ping}")
        elif got != want:
            rep.add("FAIL", "handshake", f"backend runs prefix {info.get('prefix')}, expected {runtime or sys.prefix}")
        else:
            rep.add("PASS", "handshake", f"protocol {ping.get('protocol')}, pid {info.get('pid')}, "
                    f"warmup {warm.get('import_ms', '?')} ms")
        return rpc
    except Exception as exc:
        rep.add("FAIL", "handshake", f"{type(exc).__name__}: {exc}")
        if rpc:
            rpc.close()
        return None


def run_example(rpc, backend, env, name, scratch, timeout=1200):
    """Build `name`, write its job file exactly as the app does, run
    solver_runner in a child; returns (npz_path, seconds)."""
    spec = rpc.call("examples.build", {"name": name})
    text = rpc.call("spec.job_text", {"spec": spec})
    job = os.path.join(scratch, f"{name}.job.json")
    out = os.path.join(scratch, f"{name}.npz")
    with open(job, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    t0 = time.time()
    proc = subprocess.run([sys.executable, "-u", "-m", "gui.services.solver_runner", job, out],
                          cwd=backend, env=env, capture_output=True, text=True,
                          encoding="utf-8", timeout=timeout)
    dt = time.time() - t0
    if proc.returncode != 0 or not os.path.isfile(out):
        raise RuntimeError(f"solver_runner exit {proc.returncode}: "
                           + (proc.stderr or proc.stdout)[-1500:])
    return out, dt


def load_npz(path):
    import numpy as np
    with np.load(path, allow_pickle=False) as z:
        return {k: z[k] for k in z.files}


def gate_examples(rep, rpc, backend, env, names, scratch):
    from gui.services import solver_backend
    results = {}
    for name in names:
        try:
            out, dt = run_example(rpc, backend, env, name, scratch)
            schema = solver_backend.validate_result(out)     # raises ResultSchemaError
            data = load_npz(out)
            results[name] = data
            rep.add("PASS", "examples", f"{name}: {len(data)} arrays, result schema v{schema}, {dt:.1f} s")
        except Exception as exc:
            rep.add("FAIL", "examples", f"{name}: {type(exc).__name__}: {exc}")
    return results


def gate_reference(rep, reference, versions, results, rtol):
    vpath = os.path.join(reference, "versions.json")
    if not os.path.isfile(vpath):
        rep.add("FAIL", "reference", f"{vpath} not found (emit it in tcad-dev with --emit-reference)")
        return
    with open(vpath, "r", encoding="utf-8") as fh:
        ref_versions = json.load(fh)
    diffs = compare_versions(versions, ref_versions)
    if diffs:
        rep.add("FAIL", "reference", "versions differ from tcad-dev: "
                + "; ".join(f"{k}: runtime={a} tcad-dev={b}" for k, a, b in diffs))
    else:
        rep.add("PASS", "reference", "versions match tcad-dev: "
                + ", ".join(f"{k}={v}" for k, v in ref_versions.items() if v))
    for name, data in results.items():
        rpath = os.path.join(reference, "results", f"{name}.npz")
        if not os.path.isfile(rpath):
            rep.add("FAIL", "reference", f"{name}: no reference result {rpath}")
            continue
        worst, problems = compare_arrays(data, load_npz(rpath), rtol)
        if problems:
            rep.add("FAIL", "reference", f"{name}: " + "; ".join(problems[:4]))
        else:
            rep.add("PASS", "reference", f"{name}: matches tcad-dev, max relative diff {worst:.2e} (rtol {rtol:g})")


def emit_reference(rep, outdir, backend, env, names, scratch, runtime):
    os.makedirs(os.path.join(outdir, "results"), exist_ok=True)
    versions = collect_versions(runtime or sys.prefix)
    with open(os.path.join(outdir, "versions.json"), "w", encoding="utf-8") as fh:
        json.dump(versions, fh, indent=2)
    rep.add("PASS", "emit", "versions: " + json.dumps(versions))
    rpc = Rpc(backend, env)
    try:
        for name in names:
            try:
                out, dt = run_example(rpc, backend, env, name, scratch)
                data = load_npz(out)
                import numpy as np
                np.savez(os.path.join(outdir, "results", f"{name}.npz"), **data)
                rep.add("PASS", "emit", f"{name}: {len(data)} arrays, {dt:.1f} s")
            except Exception as exc:
                rep.add("FAIL", "emit", f"{name}: {type(exc).__name__}: {exc}")
    finally:
        rpc.close()


# -- driver ------------------------------------------------------------------

def guarded(rep, gate, fn, *args):
    """Run one gate; an exception inside it is that gate's FAIL, not a crash
    that hides every gate after it."""
    try:
        return fn(*args)
    except Exception as exc:
        rep.add("FAIL", gate, f"{type(exc).__name__}: {exc}")
        return None


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--backend", required=True, help="the backend root (staged backend\\ or <repo>/pytcad)")
    ap.add_argument("--runtime", help="the runtime prefix (default: this interpreter's)")
    ap.add_argument("--examples", default=",".join(EXAMPLES),
                    help="comma list, or 'none' (default: %(default)s)")
    ap.add_argument("--reference", help="a directory written by --emit-reference")
    ap.add_argument("--emit-reference", metavar="DIR", help="write versions + example results to DIR and exit")
    ap.add_argument("--rtol", type=float, default=1e-6, help="result comparison tolerance (default %(default)g)")
    ap.add_argument("--addon", action="store_true", help="the add-on runtime: gmsh/scikit-image/tetgen MUST be present")
    ap.add_argument("--no-scrub", action="store_true", help="inherit the caller's environment")
    ap.add_argument("--json", metavar="FILE", help="also write the report as JSON")
    ap.add_argument("--scratch", help="scratch directory (default: a temp dir, removed after)")
    return ap.parse_args(argv)


def maybe_reexec(args, argv):
    """Re-run this script under the scrubbed environment (once). Returns the
    child's exit status, or None when this process already is the child."""
    if args.no_scrub or os.environ.get(CHILD_FLAG):
        return None
    prefix = args.runtime or sys.prefix
    env = scrubbed_env(prefix)
    env[CHILD_FLAG] = "1"
    return subprocess.call([sys.executable, os.path.abspath(__file__)] + argv, env=env)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    args = parse_args(argv)
    code = maybe_reexec(args, argv)
    if code is not None:
        return code

    import tempfile
    backend = os.path.abspath(args.backend)
    runtime = os.path.abspath(args.runtime) if args.runtime else None
    prefix = runtime or sys.prefix
    names = [] if args.examples == "none" else [n for n in args.examples.split(",") if n]
    rep = Report()
    print(f"check_runtime: backend={backend} runtime={prefix} python={sys.version.split()[0]}", flush=True)
    sys.path.insert(0, backend)          # what `python -m` in cwd=backend does
    os.chdir(backend)
    env = dict(os.environ)               # already scrubbed (or --no-scrub)

    own_scratch = args.scratch is None
    scratch = args.scratch or tempfile.mkdtemp(prefix="tcad_check_")
    os.makedirs(scratch, exist_ok=True)
    try:
        if args.emit_reference:
            guarded(rep, "layout", gate_layout, rep, backend)
            guarded(rep, "emit", emit_reference, rep, os.path.abspath(args.emit_reference), backend,
                    env, names, scratch, runtime)
            return _finish(rep, args)
        guarded(rep, "layout", gate_layout, rep, backend)
        guarded(rep, "interpreter", gate_interpreter, rep, runtime)
        guarded(rep, "addons", gate_addons, rep, args.addon)
        guarded(rep, "closure", gate_closure, rep, backend, runtime)
        guarded(rep, "extension", gate_extension, rep, backend)
        guarded(rep, "pardiso", gate_pardiso, rep, runtime)
        rpc = guarded(rep, "handshake", gate_handshake, rep, backend, env, runtime)
        results = {}
        if rpc is not None and names:
            try:
                results = guarded(rep, "examples", gate_examples, rep, rpc, backend, env, names, scratch) or {}
            finally:
                rpc.close()
        elif rpc is not None:
            rpc.close()
            rep.add("SKIP", "examples", "--examples none")
        else:
            rep.add("SKIP", "examples", "backend did not start")
        if args.reference:
            guarded(rep, "reference", lambda: gate_reference(rep, args.reference, collect_versions(prefix),
                                                              results, args.rtol))
        else:
            rep.add("WARN", "reference", "no --reference: versions and numerics NOT compared with tcad-dev")
        rep.add("SKIP" if not os.path.isdir(prefix) else "PASS", "size",
                f"runtime {dir_size_mb(prefix):.0f} MB, backend {dir_size_mb(backend):.0f} MB")
        return _finish(rep, args)
    finally:
        if own_scratch:
            import shutil
            shutil.rmtree(scratch, ignore_errors=True)


def _finish(rep, args):
    n = {s: sum(1 for r in rep.rows if r["status"] == s) for s in ("PASS", "FAIL", "WARN", "SKIP")}
    print(f"\n{n['PASS']} passed, {n['FAIL']} failed, {n['WARN']} warnings, {n['SKIP']} skipped", flush=True)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(rep.rows, fh, indent=2)
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())

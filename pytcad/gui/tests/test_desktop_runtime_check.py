"""NATIVE-DESKTOP-PLAN.md section 26.3, P5-S2: the runtime verification script
(desktop/tools/check_runtime.py) and the files that stage the runtime
(desktop/tools/tcad-runtime.yml, desktop/tools/stage.ps1).

What runs anywhere (Linux CI included), and what does not:

- the script's own logic -- environment scrubbing, the import-closure walk, the
  version and numerical comparisons -- is unit-tested here, each gate with a
  case where it must FAIL as well as where it passes;
- the script is run for real as a subprocess against this checkout's backend;
  the example, reference and tamper checks need the compiled `pytcad._core`
  and skip without it;
- the spec files are checked statically (no gmsh/tetgen/scikit-image in the
  core runtime, no dev-env mutation in stage.ps1).

What cannot run here, and is NOT claimed by any test in this file: conda-pack
of a real tcad-runtime env, conda-unpack, robocopy, the staged app, MKL from a
conda prefix on Windows. Those are the Windows gates listed in
NATIVE-DESKTOP-PLAN.md section 26.5.
"""
import glob
import importlib.util
import json
import os
import re
import subprocess
import sys

import numpy as np
import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ROOT, "desktop", "tools")
SCRIPT = os.path.join(TOOLS, "check_runtime.py")
YML = os.path.join(TOOLS, "tcad-runtime.yml")
STAGE = os.path.join(TOOLS, "stage.ps1")

_spec = importlib.util.spec_from_file_location("check_runtime", SCRIPT)
cr = importlib.util.module_from_spec(_spec)
sys.modules["check_runtime"] = cr
_spec.loader.exec_module(cr)

sys.path.insert(0, ROOT)
from pytcad import _accel  # noqa: E402

needs_core = pytest.mark.skipif(not _accel.HAVE_ACCEL,
                                reason="pytcad._core is not importable here")


def _read(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def _run(*args, timeout=600):
    """check_runtime.py as a subprocess (it re-launches itself scrubbed)."""
    p = subprocess.run([sys.executable, SCRIPT, "--backend", ROOT, *args],
                       capture_output=True, text=True, encoding="utf-8", timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def _line(out, status, gate):
    m = [ln for ln in out.splitlines() if re.match(rf"{status}\s+{gate}\b", ln)]
    return m[0] if m else None


# -- the spec files ------------------------------------------------------------

def _yml_deps():
    deps, in_deps = [], False
    for ln in _read(YML).splitlines():
        if ln.startswith("dependencies:"):
            in_deps = True
        elif in_deps and re.match(r"\s+-\s+\S", ln):
            deps.append(ln.split("-", 1)[1].strip())
    return deps


def test_runtime_spec_has_the_backend_packages_and_no_gpl_addons():
    names = {d.split("=")[0] for d in _yml_deps()}
    assert {"python", "numpy", "scipy", "mkl", "pyamg"} <= names
    # decision 26.4-4: shipped only in the separate add-on
    assert not names & {"gmsh", "python-gmsh", "scikit-image", "tetgen", "pytetgen"}
    assert not names & set(cr.ADDON_PACKAGES)


def test_runtime_spec_pins_only_what_the_repo_evidences_and_uses_conda_forge_only():
    pins = {d.split("=")[0]: d for d in _yml_deps() if "=" in d}
    # python 3.14 / numpy 2.5.3 / scipy 1.18.1: the tcad-dev versions recorded in
    # the pip log this slice removed; mkl and pyamg have no recorded version, so
    # they must NOT carry an invented one (stage.ps1 pins them from tcad-dev).
    assert pins == {"python": "python=3.14", "numpy": "numpy=2.5.3", "scipy": "scipy=1.18.1"}
    text = _read(YML)
    chans = re.search(r"channels:\n((?:\s+-\s+\S+\n)+)", text).group(1).split()
    assert [c for c in chans if c != "-"] == ["conda-forge", "nodefaults"]


def test_stage_script_never_touches_the_dev_envs_and_runs_the_gates():
    s = _read(STAGE)
    # the compiler incident (CLAUDE.md): nothing is installed into a dev env
    assert not re.search(r"conda\s+(install|update|remove)\s+[^\n]*-n\s+tcad-(dev|gui|cpp)", s)
    assert re.search(r'"tcad-dev",\s*"tcad-gui",\s*"tcad-cpp"', s)          # refused as -RuntimeEnv
    assert "conda-pack" in s and "conda-unpack" in s and "tcad-runtime.yml" in s
    assert "check_runtime.py" in s and "--reference" in s and "--emit-reference" in s
    assert re.search(r"_core\*\.pyd", s)                                     # refuses a backend with no extension
    # the tests never ship, and nothing else of gui\ but __init__ + services
    assert "gui\\tests" not in s and 'gui\\services' in s
    # no line EXECUTES a conda install/update (the text only appears in advice messages)
    assert not re.search(r"^\s*(&\s*)?conda(\.exe)?\s+(install|update|upgrade)\b", s, re.M)
    assert "--override-channels" not in s or "nodefaults" in _read(YML)


# -- environment ---------------------------------------------------------------

def test_prefix_path_dirs_follow_conda_activate_order(tmp_path):
    for sub in ("Library/mingw-w64/bin", "Library/bin", "Scripts", "share"):
        (tmp_path / sub).mkdir(parents=True)
    dirs = cr.prefix_path_dirs(str(tmp_path))
    rel = [os.path.relpath(d, tmp_path).replace(os.sep, "/") for d in dirs]
    assert rel == [".", "Library/mingw-w64/bin", "Library/bin", "Scripts"]   # only the existing ones


def test_scrubbed_env_drops_the_callers_path_and_keeps_pytcad_settings(tmp_path):
    (tmp_path / "bin").mkdir()
    base = {"PATH": "/opt/conda/envs/tcad-dev/bin:/somewhere", "PYTHONPATH": "/x", "CONDA_PREFIX": "/y",
            "PYTCAD_PARDISO_THREADS": "3", "PYTCAD_LINSOLVE_BACKEND": "eigen", "UNRELATED": "1",
            "HOME": "/home/u", "SystemRoot": r"C:\Windows"}
    env = cr.scrubbed_env(str(tmp_path), base)
    assert "tcad-dev" not in env["PATH"] and "/somewhere" not in env["PATH"]
    assert env["PATH"].split(os.pathsep)[0] == os.path.normpath(str(tmp_path))
    assert "PYTHONPATH" not in env and "CONDA_PREFIX" not in env and "UNRELATED" not in env
    assert env["PYTCAD_PARDISO_THREADS"] == "3" and env["PYTCAD_LINSOLVE_BACKEND"] == "eigen"
    # a fixed thread count unless the caller chose one: reproducible run to run
    assert cr.scrubbed_env(str(tmp_path), {"PATH": ""})["PYTCAD_PARDISO_THREADS"] == "1"


def test_is_within_is_a_path_prefix_test_not_a_string_prefix(tmp_path):
    (tmp_path / "rt").mkdir()
    (tmp_path / "rt2").mkdir()
    assert cr.is_within(str(tmp_path / "rt" / "a.py"), [str(tmp_path / "rt")])
    assert not cr.is_within(str(tmp_path / "rt2" / "a.py"), [str(tmp_path / "rt")])


# -- import closure ------------------------------------------------------------

def test_entry_modules_are_real_files():
    for m in cr.ENTRY_MODULES:
        assert os.path.isfile(os.path.join(ROOT, *m.split(".")) + ".py"), m


def test_lazy_imports_follow_server_py_including_function_local_ones():
    found = cr.lazy_imports(os.path.join(ROOT, "backend_service", "server.py"))
    for want in ("gui.services.examples", "gui.services.run_config", "gui.services.device_spec",
                 "workbench.core.catalog", "workbench.core.templates", "pytcad.materials"):
        assert want in found, want
    assert not [m for m in found if m.split(".")[0] not in cr.LOCAL_PACKAGES]


def test_outside_modules_flags_a_module_loaded_from_elsewhere(tmp_path, monkeypatch):
    elsewhere = tmp_path / "elsewhere"
    inside = tmp_path / "inside"
    elsewhere.mkdir()
    inside.mkdir()
    (elsewhere / "stray_mod_p5s2.py").write_text("X = 1\n")
    monkeypatch.syspath_prepend(str(elsewhere))
    import stray_mod_p5s2  # noqa: F401
    try:
        assert "stray_mod_p5s2" in [n for n, _ in cr.outside_modules([str(inside)])]
        assert "stray_mod_p5s2" not in [n for n, _ in cr.outside_modules([str(elsewhere)])]
    finally:
        sys.modules.pop("stray_mod_p5s2", None)


def test_addons_gate_fails_a_core_runtime_that_carries_one(tmp_path, monkeypatch):
    (tmp_path / "gmsh").mkdir()
    (tmp_path / "gmsh" / "__init__.py").write_text("")
    monkeypatch.syspath_prepend(str(tmp_path))
    rep = cr.Report()
    cr.gate_addons(rep, addon=False)
    assert rep.failed and "gmsh" in rep.rows[0]["detail"]
    rep = cr.Report()
    cr.gate_addons(rep, addon=True)                    # ... and it is what the add-on runtime must have
    assert rep.failed and "skimage" in rep.rows[0]["detail"] and "gmsh" not in rep.rows[0]["detail"]


def test_layout_gate_needs_the_compiled_extension(tmp_path):
    for p in ("pytcad/__init__.py", "gui/__init__.py", "gui/services/solver_runner.py",
              "backend_service/__main__.py", "backend_service/server.py", "workbench/__init__.py"):
        f = tmp_path / p
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text("")
    rep = cr.Report()
    cr.gate_layout(rep, str(tmp_path))
    assert rep.failed and "_core" in rep.rows[0]["detail"]
    (tmp_path / "pytcad" / "_core.cp314-win_amd64.pyd").write_bytes(b"")
    rep = cr.Report()
    cr.gate_layout(rep, str(tmp_path))
    assert not rep.failed
    (tmp_path / "gui" / "services" / "solver_runner.py").unlink()
    rep = cr.Report()
    cr.gate_layout(rep, str(tmp_path))
    assert rep.failed and "solver_runner" in rep.rows[0]["detail"]


def test_interpreter_gate_fails_for_a_different_runtime(tmp_path):
    rep = cr.Report()
    cr.gate_interpreter(rep, str(tmp_path))
    assert rep.failed
    rep = cr.Report()
    cr.gate_interpreter(rep, sys.prefix)
    assert not rep.failed


# -- versions and numerics -----------------------------------------------------

def test_mkl_version_comes_from_conda_meta(tmp_path):
    assert cr.mkl_conda_version(str(tmp_path)) is None
    (tmp_path / "conda-meta").mkdir()
    (tmp_path / "conda-meta" / "mkl-2025.3.0-h57928b3_0.json").write_text("{}")
    (tmp_path / "conda-meta" / "mkl-include-2025.3.0-x_0.json").write_text("{}")   # not the runtime
    assert cr.mkl_conda_version(str(tmp_path)) == "2025.3.0-h57928b3_0"


def test_compare_versions_reports_every_mismatch_and_ignores_unrecorded_ones():
    ours = {"python": "3.14.0", "numpy": "2.5.3", "scipy": "1.18.0", "mkl": None, "blas": "mkl"}
    ref = {"python": "3.14.0", "numpy": "2.5.3", "scipy": "1.18.1", "mkl": "2025.3.0", "blas": None}
    assert cr.compare_versions(ours, ref) == [("scipy", "1.18.0", "1.18.1"), ("mkl", None, "2025.3.0")]
    assert cr.compare_versions(ref, ref) == []


def _meta(created):
    return np.array(json.dumps({"backend": "pytcad", "created_utc": created, "models": {"srh": True}}))


def test_compare_arrays_tolerance_keys_shapes_and_meta():
    a = {"x": np.linspace(1, 2, 50), "n": np.arange(3), "record__meta": _meta("t1")}
    same = {"x": a["x"].copy(), "n": np.arange(3), "record__meta": _meta("t2")}   # only the timestamp differs
    worst, problems = cr.compare_arrays(a, same, 1e-6)
    assert worst == 0.0 and problems == []
    near = dict(same, x=a["x"] * (1 + 1e-9))
    assert cr.compare_arrays(a, near, 1e-6)[1] == []                               # inside rtol
    far = dict(same, x=a["x"] * 1.01)
    worst, problems = cr.compare_arrays(a, far, 1e-6)
    assert problems and worst == pytest.approx(0.01 / 1.01, rel=0.05)              # outside rtol
    assert cr.compare_arrays(a, dict(same, n=np.arange(3) + 1), 1e-6)[1]           # non-float arrays: exact
    assert cr.compare_arrays(a, {k: v for k, v in same.items() if k != "n"}, 1e-6)[1]
    assert cr.compare_arrays(a, dict(same, x=a["x"][:10]), 1e-6)[1]                # shape
    other = dict(same, record__meta=np.array(json.dumps({"backend": "pytcad", "created_utc": "t",
                                                         "models": {"srh": False}})))
    assert cr.compare_arrays(a, other, 1e-6)[1] == ["record__meta differs (beyond the timestamp)"]


# -- the script itself, as a subprocess ------------------------------------------

def test_the_script_passes_the_static_gates_on_this_checkout():
    """Layout, add-ons, import closure and the backend handshake need no
    compiled extension. (`extension`/`pardiso` are asserted separately.)"""
    code, out = _run("--examples", "none")
    if glob.glob(os.path.join(ROOT, "pytcad", "_core*.pyd")) + glob.glob(os.path.join(ROOT, "pytcad", "_core*.so")):
        assert _line(out, "PASS", "layout"), out
    assert _line(out, "PASS", "addons"), out
    assert _line(out, "PASS", "closure"), out
    assert "third-party loaded: numpy, pyamg, scipy" in out or "third-party loaded: numpy, scipy" in out, out
    assert _line(out, "PASS", "handshake"), out
    assert _line(out, "WARN", "reference"), out                # no --reference: not compared, and it says so
    if _accel.HAVE_ACCEL:
        assert _line(out, "PASS", "extension"), out
    else:
        assert code == 1 and _line(out, "FAIL", "extension"), out   # a missing extension fails the run


@needs_core
def test_reference_roundtrip_passes_and_a_tampered_reference_fails(tmp_path):
    ref = tmp_path / "ref"
    code, out = _run("--examples", "diode_1d", "--emit-reference", str(ref))
    assert code == 0 and _line(out, "PASS", "emit"), out
    assert (ref / "versions.json").is_file() and (ref / "results" / "diode_1d.npz").is_file()

    code, out = _run("--examples", "diode_1d", "--reference", str(ref))
    assert _line(out, "PASS", "examples"), out
    assert _line(out, "PASS", "reference"), out
    assert not _line(out, "FAIL", "reference"), out

    # tamper: a 1% change in one float field; and a different numpy version
    z = dict(np.load(ref / "results" / "diode_1d.npz"))
    key = next(k for k, v in z.items() if k.startswith("field__") and v.dtype.kind == "f")
    z[key] = z[key] * 1.01
    np.savez(ref / "results" / "diode_1d.npz", **z)
    code, out = _run("--examples", "diode_1d", "--reference", str(ref))
    assert code == 1 and _line(out, "FAIL", "reference") and key in out, out

    v = json.loads((ref / "versions.json").read_text())
    v["numpy"] = "0.0.1"
    (ref / "versions.json").write_text(json.dumps(v))
    code, out = _run("--examples", "none", "--reference", str(ref))
    assert code == 1 and "numpy: runtime=" in out, out


def test_a_backend_without_the_extension_or_packages_fails_the_layout_gate(tmp_path):
    p = subprocess.run([sys.executable, SCRIPT, "--backend", str(tmp_path), "--examples", "none",
                        "--no-scrub"], capture_output=True, text=True, encoding="utf-8", timeout=300)
    out = p.stdout + p.stderr
    assert p.returncode == 1 and _line(out, "FAIL", "layout"), out

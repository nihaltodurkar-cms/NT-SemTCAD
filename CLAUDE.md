# CLAUDE.md — guidance for AI agents working on PyTCAD

Read this fully before touching any file, then `ARCHITECTURE.md` (layers,
capability matrix, milestone status, open items) and the plan document of
the area you work on. Follow it literally: "frozen" means don't edit, "run
the tests" means run them and read the real output. Each gotcha below cost
a real debugging session. If this file conflicts with a user request, say
so and ask.

## What this is

PyTCAD: a validated TCAD toolkit (1D/2D/3D drift-diffusion, process
simulation), a Semiconductor Workbench domain layer (`workbench/`), and a
native C++/Qt Widgets desktop app (`desktop/`). Every educational surface is
backed by real computation. Never fake, never mock physics, never weaken tests.

**Windows only.** The project, its goldens (e.g. `HETERO_FW_DIGESTS`) and CI
target Windows: `.github/workflows/ci.yml` runs both jobs on `windows-latest`,
building `_core` with conda-forge MinGW `g++` from a separate `tcad-cpp` env.
There is no Linux CI and no PETSc job (conda-forge has no win-64 PETSc). Do
not describe the project or its CI as Linux. A cloud agent session may run in
a Linux container: a test passing there is a local pre-check only, never
evidence for Windows ("passes in this session's container", not "passes on
Linux"); leave the Windows claim to CI. Existing non-Windows branches in tests
may stay; add no Linux-only behaviour and no Linux CI job without asking.

**Removed (user decisions):** the PySide6/QML GUI (`gui/qml/`,
`gui/controllers/`, `gui/app.py`, PySide6-dependent `gui/services/` modules)
and the devsim backend (2026-09-27; `backend_ids()` returns only `"pytcad"`).
`gui/services/` itself stays: its Qt-free modules are the business logic
`backend_service/` and the desktop app depend on. `gui/README.md` and old
plan sections describing those are historical.

**Plan documents.** Only `pytcad/AC-SENSITIVITY-PLAN.md`, `DESIGN.md`,
`M47-3D-ENGINE-PLAN.md` and `NATIVE-DESKTOP-PLAN.md` are in the tree. Every
other plan (`M*-PLAN.md`), `history.md`, `Architecture_Master_Plan.md` and
`docs/user-guide/` are in git history only: `git log --all -- <path>`, then
`git show <commit>^:<path>` (a cloud clone may be shallow: `git fetch
--unshallow` first). Never cite a file you have not confirmed exists.

## Layout (run commands from `pytcad/`)

```
pytcad/            numerical core (device*.py, moscap.py, process*.py,
                   transient*.py, ac*.py, thermal*.py, continuation.py, linsolve.py)
core/              C++ engine -> pytcad._core
workbench/         core/ (domain objects), adapters/, solvers/, analysis/,
                   physics/ (published-value gated), workflow.py, study/batch
gui/services/      Qt-free: DeviceSpec (wire format), ResultStore, runners,
                   examples.py, run_config.py, project_store.py
backend_service/   Qt-free JSON-RPC 2.0 service over stdio for the desktop app
desktop/           C++20 / Qt 6 Widgets / VTK app (see desktop/README.md)
tests/             core validation; new physics lands in test_model_benchmarks.py first
gui/tests/         gui/services tests, backend RPC conformance, desktop test wrappers
benchmarks/        M32 performance suite, BASELINE.md
```

## Commands

```bash
# fast loop: parallel, skips "slow" gates and serial "timing" budgets (7-13 min)
python -m pytest tests/ gui/tests/ -n 6 -m "not slow and not timing" -q
python -m pytest tests/ gui/tests/ -m timing -q          # serial, ~1 min, every full run
python -m pytest tests/ gui/tests/ -n 6 -m slow -q       # before any milestone claim
python -m pytest tests/test_model_benchmarks.py -q       # physics gates
python examples/01_pn_diode.py
OPENBLAS_NUM_THREADS=1 python -m benchmarks [--size full]
powershell -ExecutionPolicy Bypass -File desktop\build.ps1 -Test   # desktop app
```

Cap workers at `-n 6` and set `OPENBLAS_NUM_THREADS=1` (BLAS otherwise
spawns a pool per worker). Run `slow` and `not slow` as separate
invocations. Suite invariant: **N passed, zero warnings** (`pytest.ini`
exempts one intentional warning; fix anything new at the source or assert
it with `pytest.warns`). One `pip install -r requirements.txt` covers
library, tests and optional deps.

## The C++ engine (required)

`pytcad._core` is required: Device1D (solver and material evaluation; the
pure-Python solver is gone), process diffusion, TED, AMR indicators,
unstructured assembly, the nonlocal BTBT tracer, thermal-grid and
density-gradient assembly, 3D assembly and the direct-solve session are
compiled, with no Python fallback. `import pytcad` still works without it;
those calls raise an `ImportError` naming the build command. Device2D/3D
assembly stays Python. There is no second 1D implementation to diff against:
1D gates are FD-Jacobian plus analytic/published values
(`tests/test_device1d_native_gates.py`).

Build: CMake + Ninja with `-DTCAD_INPLACE_OUTPUT=ON` (the extension lands in
`pytcad/pytcad/`), `CMAKE_CXX_COMPILER`/`CMAKE_AR`/`CMAKE_RANLIB` pointing at
the **separate** compiler env `tcad-cpp` (`conda create -n tcad-cpp -c
conda-forge gxx cmake ninja binutils`), and `CMAKE_PREFIX_PATH` at an env
with `eigen`. **Never install a compiler into the Python env**: doing so once
channel-switched `libhwloc`/`libxml2` and broke the env. `_core` statically
links the MinGW runtime (`core/CMakeLists.txt`) so it never needs the compiler
env's DLLs at import; check with `objdump -p`. PETSc is optional
(`TCAD_WITH_PETSC=AUTO|ON|OFF`; OFF builds without it, as Windows CI does).

**Transcendentals:** numpy's and C++'s `exp`/`log` differ at the ulp level.
Bit-identity gates (`np.array_equal`) need such values computed once on one
side and passed across (P4 kernels take `np.log(n)` and nodal Debye lengths).

| Env var | Effect |
|---|---|
| `PYTCAD_ACCEL` | read only by `linsolve.py`'s PETSc selection: `0` petsc4py, `1`/unset compiled PETSc |
| `PYTCAD_NATIVE_LINSOLVE` | default on: direct solves above 15k unknowns use `linsolve.DirectSession` (keeps symbolic analysis); `0` = scipy `spsolve` |
| `PYTCAD_LINSOLVE_BACKEND` | `DirectSession` factorization: MKL PARDISO by default (`mkl_rt` loaded at run time), Eigen SparseLU fallback; `eigen` forces Eigen. Residuals > 1e-6 raise `LinearSolveError` |
| `PYTCAD_PARDISO_THREADS` | MKL threads: 1 inside an xdist worker, else all cores; fixed counts reproducible, different counts differ at round-off |
| `PYTCAD_MKL_RT` | explicit path to `mkl_rt` |
| `PYTCAD_NUM_THREADS` | kernel threads, **default 1**: more threads oversubscribe batch workers and make reductions non-reproducible |

## Performance claims

No speed, scaling or "HPC-ready" claim in a plan doc or commit message
unless it comes from a `python -m benchmarks` run, with correctness, scaling,
memory and reproducibility in a table (`benchmarks/README.md` explains the
columns: `assembly` = residual + Jacobian, `asm calls` ≠ Newton iterations,
`py peak MB` is a floor).

## Hard rules

- **Numerical core (`pytcad/*.py`, `core/`) is frozen** unless the user signs
  off on a specific change; then FD-Jacobian gates first and bit-identity
  with the new model off.
- **Goldens:** `*.npz` is gitignored and digests pin one machine's FP order.
  Before a core edit: record the md5 of every golden it could touch (in the
  plan), make the edit, shim the old behaviour back to prove the old md5s
  return, and record which goldens moved and why. An unexplained move is a
  defect. Regenerate goldens only on the target machine
  (`PYTCAD_REGEN_M13_GOLDENS=1`); never copy digests between machines.
- Layering: desktop shell → BackendClient → `backend_service/` →
  `gui/services/` → subprocess per run → npz → ResultStore. `DeviceSpec`
  stays the wire format.
- New physics: published-value benchmark in `tests/test_model_benchmarks.py`
  first, plus catalog metadata.
- Optional deps (gmsh, mpmath, pyamg, tetgen, scikit-image, cupy, mpi4py)
  stay optional: soft-imported, graceful refusal.
- Every slice: suite green with pre-existing tests unchanged, an adversarial
  probe before commit, honesty over polish (report blockers and failures,
  no fudge factors).
- Don't commit unless told; the user pushes.
- Never claim something works, is fixed or is green without running it and
  reading the output; if a run is still going, say so.
- Never write a doc or history entry naming a file, function or class you
  have not confirmed exists.

## Workflow

Plan → user approves → TDD (red first) → implement → adversarial debug
(fuzz, probe, run examples / the app) → commit. A dirty tree is acceptable
only with openly failing tests and a precise handoff note.

## Gotcha: mixed line endings

No `.gitattributes`; files mix CRLF and LF and the set drifts. Measure:

```bash
for f in pytcad/*.py tests/*.py; do grep -qU $'\r' "$f" && echo "CRLF: $f"; done
```

`open(p).read()` … `open(p, "w").write(s)` silently converts a CRLF file to
LF and turns a one-line edit into a whole-file diff. Edit in binary mode
(`"rb"`/`"wb"`), check `git diff --stat`, and don't add a repo-wide
`.gitattributes` mid-branch.

## Gotchas: Python and testing

- pytest warning filters are regexes: escape `cm\^-3`.
- `str.replace` patches silently no-op on stale text: assert the old text is present.
- The shell's cwd can reset between tool calls: use absolute paths.
- `pgrep -f`/`ps | grep` wait loops match their own command line and never
  end. Use background execution with completion notification, a pidfile, or
  `[p]attern`.
- Writing the same path twice truncates; write once or append.
- Heredocs double backslashes: check line continuations in generated files.
- numpy 2.x: `np.trapezoid`; `np.polynomial.legendre.leggauss` is module-level;
  scipy `diags` order is (lower, main, upper).
- `spsolve` is not format-invariant (CSR vs CSC differ ~1e-16): a wrapper
  promising bit-identity must not reformat the matrix.
- Gate size/dimension-specific computation behind its own `if` first (an
  unguarded `doping.max(axis=2)` broke every 1D/2D job). Run the whole suite
  after touching shared dispatch code.
- Clamp only trial iterates during Newton, never the converged answer;
  re-check the raw value after convergence.
- Bit-identity digests are machine-specific: regenerate on the target
  machine and sanity-check (finite, sign, magnitude) before trusting them.
- A safety gate built for one hazard does not cover another on the same
  axis (the MPI split check covered doping gradients but not a GateBC's
  `normal_axis`); add an independent exclusion.
- Near-zero SG edge currents can be pure round-off (two terms ~1e15× the
  result): anything depending on their sign or size differs between builds.
  See `ARCHITECTURE.md` section 6 (M15 G-D).

## Gotchas: physics conventions

- Device3D's scaling (`Ns`, `LD`, `J0`, scaled coordinates) comes from
  `max|doping|` of the array it was built with; subdomain devices must share
  `Ns_override` computed from the full device.
- MOSCapacitor: rho balances Qg with the same sign; inversion at positive
  phi_s for p-substrate; global charge balance (not node-wise) is the
  neutrality test at abrupt steps.
- Heterojunction SG deltas: electron dpsi + dln(nie), hole dpsi − dln(nie).
  A shared delta passes FD-Jacobian but breaks hole detailed balance.
- TAT WKB factors are SI (F in V/m); V/cm underflows to plain SRH. Bulk-Si
  midgap TAT underflows to 0 at realizable fields.
- Implant windows beyond the substrate must be rejected by `validate_flow`.
- Checkpoint npz uses flat keys (`species_P`).
- The 1D sweep channel name is `"device"`, not the contact name.
- Fermi integral: the Boltzmann-limit deviation is exp(eta)/2^{3/2}; mpmath
  `quad` on [0, inf) needs subdivision at [1, eta+20, inf].

## Knowledge graph (graphify)

If `graphify-out/graph.json` exists, use `graphify query "<question>"`,
`graphify path "<A>" "<B>"` and `graphify explain "<concept>"` before raw
greps; `graphify-out/GRAPH_REPORT.md` only for broad reviews. After code
changes run `graphify update .`. Prefer `ast-grep` for structural searches;
read only the files and regions you need.

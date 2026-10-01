# PyTCAD

A compact, validated TCAD toolkit: self-consistent drift-diffusion device
simulation in 1D/2D/3D plus process simulation, with a C++ numerical engine,
a Python API, and a native C++/Qt Widgets desktop app. Every model states its
equation, its provenance (theory, measurement or fit) and where it breaks.

**Platform: Windows only.** The project, its golden values and CI
(`.github/workflows/ci.yml`, `windows-latest`) target Windows.
License: MIT.

## Repository layout

```
pytcad/                 project root (run commands from here)
  pytcad/               numerical core: Device1D/2D/3D, MOSCapacitor, process,
                        materials, meshes, transient/AC, thermal, continuation
  core/                 C++ engine, built as the extension module pytcad._core
  workbench/            domain layer: regions, material library, model catalog,
                        SolverBackend, analysis, splits/batch/calibration/study
  gui/services/         Qt-free business logic shared by the backend service
  backend_service/      JSON-RPC service the desktop app's backend process runs
  desktop/              native C++20 / Qt 6 Widgets / VTK desktop app
  tests/, gui/tests/    pytest suites
  benchmarks/           performance cases and checked-in BASELINE.md
  examples/             runnable examples (01_pn_diode.py, 02_process_flow.py, ...)
ARCHITECTURE.md         roadmap, capability matrix, milestone status
CLAUDE.md               rules and gotchas for contributors and AI agents
```

## Build and install

`pytcad._core` is **required**: Device1D, process simulation, AMR, the
nonlocal tunneling tracer, self-heating assembly and the direct-solve session
are compiled. `import pytcad` works without it, but those calls raise an
`ImportError` naming the build command.

The C++ compiler lives in its own conda env (MinGW `g++` from conda-forge),
never in the Python env; `_core` links the MinGW runtime statically.

```bash
cd pytcad
pip install -r requirements.txt                    # library, tests, optional deps
conda create -n tcad-cpp -c conda-forge gxx binutils cmake ninja
cmake -S core -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTCAD_INPLACE_OUTPUT=ON \
      -DCMAKE_CXX_COMPILER=<tcad-cpp>/Library/bin/g++.exe \
      -DCMAKE_AR=<tcad-cpp>/Library/bin/x86_64-w64-mingw32-ar.exe \
      -DCMAKE_RANLIB=<tcad-cpp>/Library/bin/x86_64-w64-mingw32-ranlib.exe \
      -DCMAKE_PREFIX_PATH=<python-env>/Library      # Eigen3 (conda: eigen)
cmake --build build/dev
python -c "from pytcad import _accel; print(_accel.status())"
```

Python ≥ 3.11. Eigen3 is required at build time; PETSc is optional
(`TCAD_WITH_PETSC=AUTO`). With `mkl` in the Python env, 2D/3D direct solves
use MKL PARDISO (loaded at run time), otherwise Eigen SparseLU. The desktop
app has its own build: see `pytcad/desktop/README.md`.

## Usage

```python
import numpy as np
from pytcad import Device1D, Models, MOSCapacitor, process
from pytcad.mesh import graded_mesh, check_mesh

x   = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)   # cm
dop = np.where(x < 1e-4, -1e17, 1e17)                     # cm^-3, + is n-type
check_mesh(x, dop)                                        # worst h / L_D

dev = Device1D(x, dop, T=300.0, models=Models(bgn=True, auger=True))
J   = dev.iv_sweep(np.arange(0, 0.75, 0.05))              # A/cm^2
Ec, Ev, EFn, EFp = dev.band_diagram()

C = process.implant(x, "P", energy_keV=50, dose=3e14)
C = process.diffuse_numeric(x, C, "P", T_C=950, t_s=1800)
dev2 = Device1D(x, C - 1e16, Ntotal=C + 1e16)             # Ntotal: mobility needs NA+ND

mos = MOSCapacitor(Nsub=-1e17, tox_cm=5e-7, gate="n+poly", Qf=1e12)
phis, Qg, Cg = mos.cv_sweep(np.linspace(-2, 2, 201))
```

More in `pytcad/examples/` (process flow, MOS C-V, 2D MOSFET Id-Vg,
3D-reduces-to-2D, TED anneal, MC implant, Schottky diode, 3D FinFET DIBL,
mixed-mode circuit, hydrodynamic closure).

## Equations and numerics

Steady-state van Roosbroeck system: Poisson
$\nabla\cdot(\varepsilon\nabla\psi) = -q(p - n + N_D^+ - N_A^-)$ and
continuity $\nabla\cdot J_n = qR$, $\nabla\cdot J_p = -qR$ with
drift-diffusion currents.

- **Scharfetter–Gummel fluxes**, exact for constant $J$ and $E$ across a cell.
- **Scaling** by peak doping (not $n_i$): $\psi/V_T$, $n/N_{peak}$, $x/L_D$.
- **Fully coupled Newton** with an analytic Jacobian, gated against finite
  differences (5×10⁻⁵); convergence judged on the update, not the residual.
- **Meshing:** keep $h/L_D \lesssim 1$; tensor-product 2D/3D meshes,
  adaptive refinement, and gmsh-based unstructured 2D/3D.
- **Continuation:** adaptive bias stepping and pseudo-arclength
  (`pytcad.continuation`) for avalanche folds.

## Physical models

Switched through `Models(...)`; capability per dimension is in
`ARCHITECTURE.md`.

| Model | Form / source |
|---|---|
| Mobility | Caughey–Thomas (doping), Canali (field); Lombardi CVT surface mobility (`surface_mobility`, 2D) |
| Recombination | SRH, Auger; Hurkx trap-assisted tunneling (`tat`) |
| Statistics | Boltzmann default; Fermi–Dirac (`fd`), incomplete ionization (`incomplete_ion`) |
| Bandgap narrowing | Slotboom (`bgn`) |
| Heterojunctions | per-node materials (Si, Ge, GaAs; InGaAs/AlGaAs via the workbench library), Anderson offsets, thermionic interfaces (`thermionic`) |
| Impact ionization | van Overstraeten–de Man, local (`impact`) and nonlocal effective field (`impact_nonlocal`) |
| Band-to-band tunneling | local Kane/Hurkx (`btbt`), nonlocal path WKB (`btbt_nonlocal`) |
| Quantum correction | density gradient (`dg`, `MOSCapacitor(dg=True)`) |
| Surface recombination | Robin BC at contacts (`S_n`, `S_p`) |
| Energy balance | electron energy transport (`energy_balance`) |
| Other analyses | transient, small-signal AC, self-heating, Schottky contacts, mixed-mode circuit |
| Process | implant (LSS tables, Monte-Carlo BCA), diffusion, TED, Deal–Grove; level-set 2D/3D deposit/etch/oxidation/silicidation |

**Mobility gotcha:** doping mobility uses total ionized impurities
$N_A+N_D$, not net doping; pass `Ntotal` for compensated regions.

## Validation

New physics lands with a published-value benchmark in
`tests/test_model_benchmarks.py` before any feature uses it; numerical paths
are gated by FD-Jacobian, reduction (3D→2D→1D) and bit-identity tests.
Example: abrupt 10¹⁷/10¹⁷ Si diode, $J(0.5\,\mathrm{V}) = 1.280\times10^{-2}$
A/cm² vs 1.321×10⁻² from the short-base ideal-diode formula.

```bash
cd pytcad
python -m pytest tests/ gui/tests/ -n 6 -m "not slow and not timing" -q   # fast
python -m pytest tests/ gui/tests/ -m timing -q                           # serial
python -m pytest tests/ gui/tests/ -n 6 -m slow -q                        # slow gates
```

Set `OPENBLAS_NUM_THREADS=1` for parallel runs. Known open failure:
`test_m15_ionization.py::test_g_d_breakdown_within_ten_percent` (see
`ARCHITECTURE.md`).

## Limits

- Drift-diffusion (plus a local/electron-only energy balance): no
  Monte-Carlo or quantum transport; density gradient is equilibrium-only.
- LSS implant tables are amorphous-Si moments (no channeling); diffusion
  and TED are lumped engineering models, not coupled point-defect PDEs.
- Deal–Grove under-predicts thin dry oxides (the initial-thickness term is a fit).
- Quasi-static C-V only.
- M14 absolute surface-mobility calibration is open (paywalled source).

## Further reading

- `ARCHITECTURE.md` — roadmap, capability matrix, milestone status
- `CLAUDE.md` — engineering rules and known gotchas
- `pytcad/README.md` — numerical-core details
- `pytcad/desktop/README.md`, `pytcad/NATIVE-DESKTOP-PLAN.md` — desktop app
- `pytcad/benchmarks/README.md` — performance suite
- Removed documents (milestone `M*-PLAN.md` files, `history.md`,
  `Architecture_Master_Plan.md`, `docs/user-guide/`) are in git history:
  find the deleting commit with `git log --all -- <path>`, then
  `git show <commit>^:<path>`.

# Architecture and roadmap

Live record of how PyTCAD is structured, what it can do, and what is open.
Updated 2026-10-01. Detail for a milestone is in its plan document where
one exists (`pytcad/*-PLAN.md`; removed plans are in git history, see
`README.md`, "Further reading"). This file states outcomes, not session narratives.

Goal: a learning and research TCAD environment that matches DEVSIM /
Silvaco / Sentaurus on core device physics and beats them on a few axes
(differentiable simulation, provenance, inspectable cited physics,
Python-native extensibility), while staying open and modular. Every
educational surface is backed by real computation. Windows only.

## 1. Layers

```
desktop/ (C++20, Qt 6 Widgets, VTK)
  -> BackendClient (JSON-RPC 2.0 over stdio)
  -> backend_service/ (Python, Qt-free)
  -> gui/services/ (DeviceSpec wire format, run config, result store, runners)
  -> subprocess per run -> .npz result (schema v3) -> ResultStore
workbench/   domain layer: Region/DomainDevice, MaterialLibrary, ModelCatalog,
             SolverBackend (only "pytcad"), analysis, splits/batch/calibration,
             study manifests, compact-model extraction, deck front end
pytcad/      numerical core (Device1D/2D/3D, MOSCapacitor, process, transient,
             AC, thermal, continuation, linear solvers)
core/        C++ engine -> pytcad._core (required)
```

Removed: the PySide6/QML GUI and the devsim backend (user decisions,
2026-09-27). `gui/services/` survives as shared Qt-free logic.

## 2. C++ engine (M31, M47 and later)

Compiled in `pytcad._core`: mesh geometry, process/AMR kernels, the nonlocal
BTBT path tracer, thermal-grid and density-gradient assembly, 3D assembly
(structured and unstructured), all of Device1D (solver and material
evaluation), and the direct-solve session (MKL PARDISO default, Eigen
SparseLU fallback). PETSc (KSP/PC, MUMPS) is optional. Device2D/Device3D
assembly stays in Python (2-6% of heavy runs). There is no pure-Python
fallback for compiled kernels.

## 3. Capability matrix

`Y` = works and is gated; `-` = absent.

| Capability | 1D | 2D | 3D |
|---|---|---|---|
| Drift-diffusion, structured mesh | Y | Y | Y |
| Drift-diffusion, unstructured mesh | n/a | Y | Y (homojunction) |
| Fermi-Dirac / incomplete ionization | Y | Y | Y |
| Impact ionization, local and nonlocal | Y | Y | Y |
| BTBT, local Kane and nonlocal path | Y | Y | Y |
| Trap-assisted tunneling | Y | Y | Y |
| Density gradient (equilibrium) | Y | Y | Y |
| Self-heating (lattice T) | Y | Y | Y |
| Energy balance (electrons only) | Y | Y | Y |
| Transient, small-signal AC | Y | Y | Y |
| Adaptive refinement | Y | Y | Y |
| Schottky contacts (Dirichlet / thermionic Robin) | both | both | Dirichlet |
| Mixed-mode circuit (MNA + device stamp) | Y | Y | Y |
| Process: implant, diffusion, oxidation | Y | Y | deposit/etch/oxidation (level set) |
| Process: TED, Monte-Carlo implant | Y | - | - |

Rule: a dimensional lift lands only with a reduction gate (a uniform 3D
case reproduces the validated 2D/1D answer to round-off).

## 4. Milestone status

| Milestone | Status |
|---|---|
| M1-M10 domain core, result schema, backends, builders, deck front end | done |
| M11 heterostructures, M12 tunneling (FN/WKB, Hurkx TAT) | done |
| M13 Fermi-Dirac + incomplete ionization | done |
| M14 surface mobility, D_it, surface recombination | done except G-A (absolute mobility; Lombardi 1988 constants paywalled) |
| M15 impact ionization (coupled Jacobian, arc-length continuation) | gates green except G-D on the current build: the N=2e16 breakdown fold traces to ~41.9 V vs analytic 33.7 V; see section 6 |
| M16 BTBT (local; 2D/3D in M16-S2) | done |
| M17 transient, M18 small-signal AC | done (1D/2D; 3D in M45) |
| M19 self-heating | done (1D; 2D/3D in M43) |
| M20 density gradient | done (equilibrium; 2D/3D in M42) |
| M21 meshing (adaptive, gmsh unstructured 2D/3D) | done |
| M22 linear solvers, continuation, MPI Schwarz, AMG/CUDA (GUI path) | done |
| M23 2D process, M24 TED, M25 MC implant, M26 3D FinFET | done as disclosed slices |
| M27 mixed-mode circuit, M28 Schottky module, M29 hydrodynamic closure | done |
| M30 workbench (splits, calibration, batch, studies, remote execution) | done |
| M31 C++ engine | done in stages; P7 (MPI/GPU via PETSc) not started |
| M32 benchmark suite (`pytcad/benchmarks/`) | done |
| M33 interface physics, M34 nonlocal tunneling/ionization | done |
| M35 level-set process 2D/3D | done |
| M36 stress/strain, M37 reliability, M39 NEGF, M40 optics | not started |
| M38 compact-model extraction | done |
| M41-M47 dimensional lifts and 3D engine | done |
| M51 geometry/mesh hover, M52 multi-metric convergence | done |
| M53 native desktop app (`pytcad/NATIVE-DESKTOP-PLAN.md`) | in progress |
| AC ohmic-port sensitivity (`pytcad/AC-SENSITIVITY-PLAN.md`) | done, one open item in its section 9 |

Proposed next: adjoint sensitivities, then gradient-based calibration,
uncertainty quantification, ML surrogates.

## 5. Standing rules

1. Core changes need explicit sign-off, FD-Jacobian gates first, and
   bit-identity with the new model off.
2. New physics lands in `tests/test_model_benchmarks.py` first, with
   published constants.
3. A milestone with failing gates is not complete; dependents wait.
4. New meshes and linear solvers ship with parity tests against validated paths.
5. Optional dependencies stay optional (soft import, graceful refusal).
6. Result-schema changes are additive and versioned.
7. Every model states what it does not model and where it breaks.
8. No performance claim without a benchmark table from `pytcad/benchmarks/`.

## 6. Open items

- **M15 G-D.** The M15 G-D arc-length "fold" is where the corrector stalls, and
  on both platforms that point was decided by round-off. The
  impact-ionization Jacobian at the junction follows round-off: the edge
  current there is the difference of terms ~10¹⁵× larger, wider than the
  |J| regularizer. Without that sensitivity the stall is at ~41.9 V. The
  traced current shows no multiplication before the stall. This needs a
  physical breakdown criterion and a round-off-safe regularizer.
- M14 G-A (paywalled constants).
- GUI: no freeform geometry authoring; no provenance-trace view; templates
  limited to diode, MOS-C, NMOS, HBT, HEMT and FinFET.
- GPU and MPI paths exist only for the 3D solve in `gui/services/`.

## 7. Out of scope

Monte-Carlo Boltzmann transport, kinetic-MC diffusion, radiation effects,
ferroelectric/phase-change materials, full viscoelastic oxidation,
electromagnetic solvers, PDK-grade compact models, bit-identity with
commercial tools, foundry-calibrated decks.

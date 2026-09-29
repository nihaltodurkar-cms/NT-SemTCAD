"""Phase 3.1 of the device port (~/.claude/plans/eager-purring-fairy.md):
pytcad._core.ReusableLU / linsolve.DirectSession, a direct solver that
keeps its symbolic analysis across a Newton solve's fixed-pattern
Jacobians, default on (PYTCAD_NATIVE_LINSOLVE=0 opts out) in Device2D/Device3D.

Gates: exactness of the solve against spsolve, the pattern-reuse
bookkeeping (same / subset / superset patterns), the failure contract
(singular -> LinearSolveError), the size/instability fallbacks keeping
the caller's own call, and full device solves agreeing with the default
path. Opt-in is off by default, so every other suite is untouched."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest
import scipy.sparse as sp
from scipy.sparse.linalg import spsolve

from pytcad import _accel, linsolve

pytestmark = pytest.mark.skipif(not _accel.HAVE_ACCEL, reason="pytcad._core not built")


def _system(n=400, seed=0, density=0.01, extra=None):
    rng = np.random.default_rng(seed)
    A = sp.random(n, n, density=density, random_state=rng, format="csr")
    A = A + sp.diags(np.full(n, 4.0 + n * density))      # diagonally dominant
    if extra is not None:
        A = A + extra
    return A.tocsr(), rng.standard_normal(n)


def test_reusable_lu_matches_spsolve_and_reuses_the_analysis():
    A, b = _system()
    s = linsolve.DirectSession()
    for k in range(4):                    # same pattern, new values each time
        Ak = A.copy()
        Ak.data = Ak.data * (1.0 + 0.1 * k)
        x = s._solve_native(Ak, b)
        np.testing.assert_allclose(x, spsolve(Ak.tocsc(), b), rtol=1e-11, atol=1e-13)
    assert s.analyses == 1 and s.factorizations == 4


def test_subset_pattern_scatters_superset_reanalyzes():
    A, b = _system(seed=1)
    s = linsolve.DirectSession()
    s._solve_native(A, b)
    # drop some off-diagonal entries -> subset pattern: no new analysis
    C = A.tocoo()
    keep = (C.row == C.col) | (np.arange(C.nnz) % 3 != 0)
    B = sp.csr_matrix((C.data[keep], (C.row[keep], C.col[keep])), shape=A.shape)
    x = s._solve_native(B, b)
    np.testing.assert_allclose(x, spsolve(B.tocsc(), b), rtol=1e-11, atol=1e-13)
    assert s.analyses == 1
    # add entries outside the pattern -> union re-analysis, still exact
    rng = np.random.default_rng(5)
    E = sp.random(*A.shape, density=0.002, random_state=rng, format="csr")
    Csup = (A + E).tocsr()
    x = s._solve_native(Csup, b)
    np.testing.assert_allclose(x, spsolve(Csup.tocsc(), b), rtol=1e-11, atol=1e-13)
    assert s.analyses == 2
    # and the original pattern is now a subset of the union: no analysis
    s._solve_native(A, b)
    assert s.analyses == 2


def test_singular_system_raises_linear_solve_error():
    A = sp.csr_matrix(np.array([[1.0, 2.0], [2.0, 4.0]]))
    with pytest.raises(linsolve.LinearSolveError):
        linsolve.DirectSession()._solve_native(A, np.array([1.0, 1.0]))


def test_session_keeps_the_callers_call_below_the_size_threshold():
    A, b = _system(n=300)
    s = linsolve.DirectSession()
    sentinel = np.arange(300.0)
    x = s.solve(A, b, fallback=lambda: sentinel)
    assert x is sentinel and s.fallback_solves == 1 and s.native_solves == 0


def test_session_hands_back_an_unstable_pattern(monkeypatch):
    monkeypatch.setattr(linsolve.DirectSession, "MIN_UNKNOWNS", 0)
    s = linsolve.DirectSession()
    rng = np.random.default_rng(3)
    base, b = _system(seed=2)
    calls = []
    for k in range(5):                    # a NEW pattern every call
        E = sp.random(*base.shape, density=0.002, random_state=rng, format="csr")
        A = (base + E).tocsr()
        s.solve(A, b, fallback=lambda: calls.append(k) or np.zeros(base.shape[0]))
    assert s.analyses == linsolve.DirectSession.MAX_ANALYSES
    assert s.native_solves == linsolve.DirectSession.MAX_ANALYSES
    assert len(calls) == 5 - linsolve.DirectSession.MAX_ANALYSES


def test_default_on_with_explicit_opt_out(monkeypatch):
    """Default ON since 2026-09-28; "0" is the only opt-out value."""
    monkeypatch.delenv("PYTCAD_NATIVE_LINSOLVE", raising=False)
    assert linsolve.native_direct_enabled()
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "0")
    assert not linsolve.native_direct_enabled()
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "1")
    assert linsolve.native_direct_enabled()


# ----------------------------------------------------------------------
#  Device-level: the whole Newton solve through the session agrees with
#  the default path (threshold dropped to 0 so the small test devices
#  actually exercise the native LU rather than its size fallback).
# ----------------------------------------------------------------------
def _mosfet():
    from pytcad.mosfet import build_mosfet
    dev = build_mosfet(Lg=0.5e-4, Lsd=0.5e-4, depth=0.6e-4, Na=1e17,
                       Nsd_peak=1e20, tox_cm=3e-7, nx=41, ny=25)
    return dev


def test_device2d_bias_solve_agrees_with_default_path(monkeypatch):
    bias = {"gate": 1.0, "drain": 0.1, "source": 0.0, "body": 0.0}
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "0")   # the spsolve reference
    ref = _mosfet(); ref.solve_equilibrium(); ref.solve_bias(bias)
    assert ref.last_linsolve_session is None

    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "1")
    monkeypatch.setattr(linsolve.DirectSession, "MIN_UNKNOWNS", 0)
    dev = _mosfet(); dev.solve_equilibrium(); dev.solve_bias(bias)
    s = dev.last_linsolve_session
    assert s is not None and s.native_solves > 0 and s.analyses == 1
    assert dev.last_converged and ref.last_converged
    np.testing.assert_allclose(dev.psi, ref.psi, rtol=0, atol=1e-9)
    for a, b in ((dev.n, ref.n), (dev.p, ref.p)):
        m = b > 1e-10
        assert np.max(np.abs(a[m] - b[m]) / b[m]) < 1e-7
    for name in ("drain", "source"):
        ia, ib = dev.terminal_current(name), ref.terminal_current(name)
        assert abs(ia - ib) <= 1e-6 * abs(ib) + 1e-30, (name, ia, ib)


def test_transient2d_step_solves_agree_with_default_path(monkeypatch):
    """transient2d's own Newton loop through a device-cached session
    (one analysis for every iteration of every step) agrees with the
    default spsolve path."""
    from pytcad.transient2d import solve_transient
    bias = {"gate": 1.0, "drain": 0.1, "source": 0.0, "body": 0.0}

    def run():
        dev = _mosfet(); dev.solve_equilibrium(); dev.solve_bias(bias)
        res = solve_transient(dev, waveforms={**bias, "drain": 0.3},
                              t_end=1e-11, dt0=1e-13, dt_min=1e-16)
        return dev, res

    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "0")   # the spsolve reference
    ref, rref = run()
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "1")
    monkeypatch.setattr(linsolve.DirectSession, "MIN_UNKNOWNS", 0)
    dev, rdev = run()
    s = getattr(dev, "_linsolve_session", None)
    assert s is not None and s.native_solves > 1 and s.analyses == 1
    assert np.array_equal(rdev.times, rref.times)      # same step control
    np.testing.assert_allclose(rdev.psi_hist[-1], rref.psi_hist[-1], rtol=0, atol=1e-9)
    for k in rref.terminal_current:
        a, b = rdev.terminal_current[k], rref.terminal_current[k]
        np.testing.assert_allclose(a, b, rtol=1e-6, atol=1e-6 * np.abs(b).max())


def _spy_sessions(monkeypatch):
    """Record every DirectSession created (the unstructured cores keep
    theirs local to the solve)."""
    made = []
    real = linsolve.DirectSession.__init__

    def init(self, *a, **k):
        real(self, *a, **k)
        made.append(self)
    monkeypatch.setattr(linsolve.DirectSession, "__init__", init)
    return made


@pytest.mark.parametrize("dim", [2, 3])
def test_unstructured_bias_solve_agrees_with_default_path(monkeypatch, dim):
    """unstructured_dd / unstructured_dd3d (and their Poisson equilibrium
    solves) through the session agree with the default direct path --
    B8/B9's own quick geometry."""
    pytest.importorskip("gmsh")
    import os
    os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
    from benchmarks import cases  # noqa: F401  (import check only)
    if dim == 2:
        from pytcad.gmsh_mesh import build_diode_mesh
        from pytcad.region_resolver import resolve_regions, resolve_contacts
        from pytcad.unstructured_assembly import (
            build_unstructured_stencil, build_edge_flux_geometry)
        from pytcad.unstructured_poisson import evaluate_doping_at_nodes
        from pytcad.unstructured_dd import solve_bias
        mesh = build_diode_mesh(Lx=4.0e-4, Ly=1.0e-4, Xj=2.0e-4)
        regions = resolve_regions(mesh)
        contacts = resolve_contacts(mesh)
        el, areas = build_unstructured_stencil(mesh.nodes, mesh.triangles)
        ie, tg = build_edge_flux_geometry(mesh.nodes, mesh.triangles, el)
        rot = np.empty(mesh.n_triangles(), dtype=object)
        for name, idx in regions.items():
            rot[idx] = name
        C = evaluate_doping_at_nodes(mesh.nodes, mesh.triangles, rot,
                                     {"p_region": -1e17, "n_region": 1e17})
        run = lambda: solve_bias(mesh.nodes, mesh.triangles, el, areas, ie, tg,
                                 C, contacts, {"left_contact": 0.5, "right_contact": 0.0})
    else:
        from pytcad.gmsh_mesh3d import build_diode_mesh3d
        from pytcad.unstructured_assembly3d import (
            build_unstructured_stencil3d, build_edge_flux_geometry3d)
        from pytcad.unstructured_dd3d import evaluate_doping_at_nodes3d, solve_bias3d
        mesh = build_diode_mesh3d(Lx=1.0e-4, Ly=2.5e-5, Lz=1.5e-5, Xj=0.5e-4,
                                  Nd_scale=1e17)
        el, vols = build_unstructured_stencil3d(mesh.nodes, mesh.tets)
        edges, trans = build_edge_flux_geometry3d(mesh.nodes, mesh.tets, el)
        rot = np.empty(mesh.n_tets(), dtype=object)
        for name, idx in mesh.volume_tags.items():
            rot[idx] = name
        C = evaluate_doping_at_nodes3d(mesh.nodes, mesh.tets, rot,
                                       {"p_region": -1e17, "n_region": 1e17})
        contacts = {"left_contact": mesh.face_tags["left_contact"],
                    "right_contact": mesh.face_tags["right_contact"]}
        # "auto" resolves to an iterative method for unstructured 3D (the
        # session correctly stays out of that path); ask for direct
        from pytcad import NewtonOptions
        run = lambda: solve_bias3d(mesh.nodes, mesh.tets, edges, vols, trans, C,
                                   contacts, {"left_contact": 0.5, "right_contact": 0.0},
                                   opts=NewtonOptions(linsolve="direct"))

    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "0")   # the spsolve reference
    psi_r, n_r, p_r, _, cur_r = run()
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "1")
    monkeypatch.setattr(linsolve.DirectSession, "MIN_UNKNOWNS", 0)
    made = _spy_sessions(monkeypatch)
    psi, n, p, _, cur = run()
    assert made and all(s.native_solves > 0 for s in made)
    assert all(s.analyses == 1 for s in made)
    np.testing.assert_allclose(psi, psi_r, rtol=0, atol=1e-9)
    for k in cur_r:
        assert abs(cur[k] - cur_r[k]) <= 1e-6 * abs(cur_r[k]) + 1e-30, (k, cur[k], cur_r[k])


def test_thermal_grid_agrees_with_default_path(monkeypatch):
    from pytcad.materials import SILICON
    from pytcad.thermal import ThermalBC
    from pytcad.thermal_grid import solve_lattice_temperature_grid
    x = np.linspace(0, 1e-3, 30); y = np.linspace(0, 5e-4, 20)
    H = np.zeros((y.size, x.size)); H[8:12, 10:20] = 1e5
    # y: isothermal sink at the bottom, adiabatic top; x: adiabatic sides
    bcs = [(ThermalBC("adiabatic"), ThermalBC("isothermal")),
           (ThermalBC("adiabatic"), ThermalBC("adiabatic"))]
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "0")   # the spsolve reference
    ref = solve_lattice_temperature_grid((y, x), H, SILICON, 300.0, bcs)
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "1")
    monkeypatch.setattr(linsolve.DirectSession, "MIN_UNKNOWNS", 0)
    made = _spy_sessions(monkeypatch)
    T = solve_lattice_temperature_grid((y, x), H, SILICON, 300.0, bcs)
    assert made and made[0].native_solves > 0
    np.testing.assert_allclose(T, ref, rtol=1e-10)


def test_device3d_bias_solve_agrees_with_default_path(monkeypatch):
    import warnings
    from pytcad import NewtonOptions
    from pytcad.device3d import Device3D
    from pytcad.mesh3d import Mesh3D

    def build():
        x = np.linspace(0.0, 2e-4, 21)
        y = np.linspace(0.0, 1e-4, 6)
        z = np.linspace(0.0, 1e-4, 6)
        X = np.broadcast_to(x[None, None, :], (z.size, y.size, x.size))
        dop = np.where(X < 1e-4, -1e17, 1e17)
        d = Device3D(Mesh3D(x, y, z), dop)
        jj, kk = np.meshgrid(np.arange(y.size), np.arange(z.size))
        d.add_contact("anode", i=np.zeros(jj.size, int), j=jj.ravel(), k=kk.ravel())
        d.add_contact("cathode", i=np.full(jj.size, x.size - 1), j=jj.ravel(), k=kk.ravel())
        return d

    opts = NewtonOptions(linsolve="direct")
    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "0")   # the spsolve reference
    ref = build(); ref.solve_equilibrium(opts); ref.solve_bias({"anode": 0.4}, opts)

    monkeypatch.setenv("PYTCAD_NATIVE_LINSOLVE", "1")
    monkeypatch.setattr(linsolve.DirectSession, "MIN_UNKNOWNS", 0)
    dev = build(); dev.solve_equilibrium(opts); dev.solve_bias({"anode": 0.4}, opts)
    s = dev.last_linsolve_session
    assert s is not None and s.native_solves > 0 and s.analyses == 1
    assert dev.last_converged and ref.last_converged
    np.testing.assert_allclose(dev.psi, ref.psi, rtol=0, atol=1e-9)
    ia, ib = dev.terminal_current("cathode"), ref.terminal_current("cathode")
    assert abs(ia - ib) <= 1e-6 * abs(ib), (ia, ib)

"""Gates for pytcad.device.Device1D's native-dispatch wrapper (Phase 1,
step 2 of ~/.claude/plans/eager-purring-fairy.md): a baseline-only
Models() config should transparently delegate to pytcad._core.Device1D
instead of the pure-Python Newton loop, with the identical public API
(psi/n/p/psi_V/n_cm3/p_cm3/E_field/Jn/Jp/last_converged/last_newton_err/
current_density/solve_equilibrium/solve_bias all still work) and
reconstruct-and-compare parity against the pure-Python path on the same
fixture -- CLAUDE.md's protocol for any change touching the frozen
numerical core.

This is additive: pytcad.device.Device1D's pure-Python solve path is
untouched code, only gained an early-return branch, so every pre-
existing test that exercises a non-baseline Models() config (tat,
impact, btbt, dg, heterojunction, ...) never reaches that branch and
keeps running exactly as before -- confirmed here by the
test_nonbaseline_configs_stay_on_python_path parametrization.

Phase 2 slice 1 (this revision) ported Fermi-Dirac statistics
(Models.fd) and incomplete ionization (Models.incomplete_ion) into the
native class too -- see test_fd_config_dispatches_to_native and
test_incomplete_ion_config_dispatches_to_native below, and
core/src/device1d/device1d.cpp's per-method comments for exactly which
Python branches each addition mirrors."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from pytcad import _accel
from pytcad.device import Device1D, Models, NewtonOptions
from pytcad.materials import SILICON
from pytcad.mesh import graded_mesh

pytestmark = pytest.mark.skipif(not _accel.HAVE_ACCEL, reason="pytcad._core not built")


@pytest.fixture(autouse=True)
def _opt_into_native_dispatch(monkeypatch):
    """Dispatch is OFF by default (see device.py's own comment at the
    _native construction site -- turning it on unconditionally broke
    several other milestones' bit-identical-anchor gates). These tests
    exist to exercise the dispatch path itself, so opt in for their
    duration only; every other test file in this suite is unaffected."""
    monkeypatch.setenv("PYTCAD_NATIVE_DEVICE1D", "1")


def _diode(models=None):
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    return Device1D(x, doping, T=300.0, material=SILICON, models=models), x, doping


def test_baseline_config_dispatches_to_native():
    dev, x, doping = _diode(Models(bgn=False, auger=True))
    assert dev._native is not None


@pytest.mark.parametrize("models", [
    Models(field_mobility=True),
    Models(dg=True),
    Models(surface_mobility=True),  # Device1D doesn't implement it either
                                     # way (2D-only feature), but the flag
                                     # being off-default must still block
                                     # native dispatch, not silently pass.
    Models(band_offset="affinity"),
    Models(S_n=1e3),
    Models(energy_balance=True),
])
def test_nonbaseline_configs_stay_on_python_path(models):
    """Every Models() flag Phase 1 doesn't implement must keep running
    the existing pure-Python Newton loop, unchanged -- the whole point
    of gating dispatch on _model_flags_beyond_baseline."""
    dev, x, doping = _diode(models)
    assert dev._native is None


@pytest.mark.parametrize("models", [
    Models(bgn=False, auger=True),
    Models(bgn=False, auger=True, fd=True),
    Models(bgn=False, auger=True, incomplete_ion=True),
    Models(bgn=False, auger=True, fd=True, incomplete_ion=True),
    Models(bgn=True, srh=True, tat=True),
    Models(bgn=False, auger=True, fd=True, tat=True),
], ids=["baseline", "fd", "incomplete_ion", "fd+incomplete_ion", "tat",
        "fd+tat"])
def test_native_dispatch_reconstructs_pure_python_result(models):
    """Reconstruct-and-compare (CLAUDE.md protocol): force the SAME
    fixture through the pure-Python path (by clearing _native after
    construction) and through the native path, and compare psi/n/p/J.
    Not bit-identical (different solver internals -- native uses a
    direct Eigen::SparseLU, Python uses scipy spsolve, and it is a
    fully independent Newton loop implementation), so this is a
    physical-agreement gate, not a golden digest. Measured agreement
    for all four configs is actually round-off (~1e-14), tighter than
    these tolerances ask -- kept loose/uniform across configs rather
    than tuned per-config to the measured number, since the point of
    this gate is "agrees physically", not "matches exactly this run"."""
    dev_native, x, doping = _diode(models)
    assert dev_native._native is not None
    dev_py, _, _ = _diode(models)
    dev_py._native = None  # force the pure-Python path on the identical fixture

    opts = NewtonOptions()
    dev_native.solve_equilibrium(opts)
    dev_py.solve_equilibrium(opts)
    np.testing.assert_allclose(dev_native.psi, dev_py.psi, rtol=1e-6, atol=1e-9)

    dev_native.solve_bias([0.3, 0.0], opts)
    dev_py.solve_bias([0.3, 0.0], opts)
    assert dev_native.last_converged and dev_py.last_converged
    np.testing.assert_allclose(dev_native.psi, dev_py.psi, rtol=1e-5, atol=1e-8)
    np.testing.assert_allclose(dev_native.n, dev_py.n, rtol=1e-4, atol=1e-12)
    np.testing.assert_allclose(dev_native.p, dev_py.p, rtol=1e-4, atol=1e-12)

    Jn_native, spread_native = dev_native.current_density()
    Jn_py, spread_py = dev_py.current_density()
    assert abs(Jn_native - Jn_py) / abs(Jn_py) < 1e-3, (Jn_native, Jn_py)
    assert spread_native < 1e-3, spread_native
    assert spread_py < 1e-3, spread_py


def test_fd_config_dispatches_to_native():
    dev, x, doping = _diode(Models(bgn=False, auger=True, fd=True))
    assert dev._native is not None


def test_incomplete_ion_config_dispatches_to_native():
    dev, x, doping = _diode(Models(bgn=False, auger=True, incomplete_ion=True))
    assert dev._native is not None


def test_fd_incomplete_ion_config_dispatches_to_native():
    dev, x, doping = _diode(
        Models(bgn=False, auger=True, fd=True, incomplete_ion=True))
    assert dev._native is not None


def test_tat_config_dispatches_to_native():
    dev, x, doping = _diode(Models(tat=True))
    assert dev._native is not None
    dev, x, doping = _diode(Models(tat=True, trap_et_rel=0.3))
    assert dev._native is not None


@pytest.mark.parametrize("flag", ["impact", "btbt"])
def test_generation_configs_dispatch_and_sync(flag):
    """M15/M16 local generation is native: dispatch triggers, and after
    solve_bias the wrapper exposes what the pure-Python path leaves
    behind (_ii_strength, strength-scaled _ii_gs_cache/_btbt_gs_cache,
    None for the flag that is off)."""
    dev, x, doping = _diode(Models(**{flag: True}))
    assert dev._native is not None
    dev.solve_bias([-1.0, 0.0])
    assert dev.last_converged and dev._ii_strength == 1.0
    on = dev._ii_gs_cache if flag == "impact" else dev._btbt_gs_cache
    off = dev._btbt_gs_cache if flag == "impact" else dev._ii_gs_cache
    assert on is not None and on.shape == (dev.N,) and np.all(np.isfinite(on))
    assert off is None


def test_nonlocal_configs_dispatch_and_sync():
    """M34 nonlocal flags are native: dispatch triggers; btbt_nonlocal's
    frozen paths come back as the same TunnelPaths build_1d produces,
    with the refresh outcome; impact_nonlocal leaves the path attributes
    at their pure-Python off values."""
    x = graded_mesh(1.0e-5, [5.0e-6], h_min=1e-8, h_max=2e-7)
    with pytest.warns(UserWarning, match="Doping exceeds"):
        dev = Device1D(x, np.where(x < 5.0e-6, -5e19, 5e19), T=300.0,
                       models=Models(bgn=False, btbt_nonlocal=True))
    assert dev._native is not None
    dev.solve_bias([-1.0, 0.0])
    assert dev.last_converged and dev.last_btbt_nl_stable is True
    assert dev._btbt_nl_paths.n_paths > 0
    ref = dev._btbt_nl_build_paths(dev.psi)
    assert np.array_equal(dev._btbt_nl_paths.start, ref.start)

    dev2, _, _ = _diode(Models(impact=True, impact_nonlocal=True))
    assert dev2._native is not None
    dev2.solve_bias([-2.0, 0.0])
    assert dev2.last_converged and dev2._ii_gs_cache is not None
    assert dev2.last_btbt_nl_stable is None and dev2.last_btbt_nl_refreshes == 0


def test_wrapper_state_is_the_warm_start():
    """continuation.py restores dev.psi/n/p after a failed step and calls
    solve_bias again; the native solve must start from THAT state, not
    from its own last internal one. Checked by writing a known state into
    the attributes and confirming a zero-iteration-change re-solve at the
    same bias with max_iter=1 converges -- only possible when Newton
    starts AT the solution (from the stale 0.5 V state it cannot)."""
    import warnings
    dev, x, doping = _diode(Models(bgn=False))
    assert dev._native is not None
    dev.solve_bias([0.3, 0.0])
    ref = (dev.psi.copy(), dev.n.copy(), dev.p.copy())
    dev.solve_bias([0.5, 0.0])            # native internal state moves on
    one = NewtonOptions(max_iter=1)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        dev.solve_bias([0.3, 0.0], one)   # stale start: must NOT converge
    assert not dev.last_converged
    dev.psi, dev.n, dev.p = ref           # caller restores the 0.3 V state
    dev.solve_bias([0.3, 0.0], one)
    assert dev.last_converged
    np.testing.assert_allclose(dev.psi, ref[0], rtol=0, atol=1e-10)


def test_current_density_reads_wrapper_attributes():
    """arc_length_sweep writes dev.Jn/dev.Jp from its own corrector with
    no solve; current_density must report THOSE, not the compiled
    object's last-solve cache (which froze every arc-length record at one
    value -- caught by test_m16_btbt's G-E gates under opt-in)."""
    dev, x, doping = _diode(Models(bgn=False))
    assert dev._native is not None
    dev.solve_bias([0.3, 0.0])
    dev.Jn = dev.Jn * 2.0
    dev.Jp = dev.Jp * 2.0
    j, _ = dev.current_density()
    assert j == pytest.approx(float(np.mean(dev.Jn + dev.Jp)), rel=1e-15)


def test_tat_frozen_probabilities_sync_to_wrapper():
    """test_m12_tat reads dev._Pn/_Pp after solve_bias; the native path
    must expose the SAME frozen arrays the pure-Python path computes
    (both underflow to exact 0.0 at realizable junction fields)."""
    dev, x, doping = _diode(Models(bgn=True, srh=True, tat=True))
    assert dev._native is not None
    dev_py, _, _ = _diode(Models(bgn=True, srh=True, tat=True))
    dev_py._native = None
    for d in (dev, dev_py):
        d.solve_bias([0.0, -0.5])
    assert dev._Pn is not None and dev._Pp is not None
    np.testing.assert_allclose(dev._Pn, dev_py._Pn, rtol=1e-13, atol=0.0)
    np.testing.assert_allclose(dev._Pp, dev_py._Pp, rtol=1e-13, atol=0.0)


def test_models_mutated_after_construction_falls_back_to_python():
    """test_m12_tat's fixture builds a plain device, THEN sets
    dev.models.tat = True. The compiled object was built without TAT and
    cannot see that mutation, so the wrapper must drop to the pure-
    Python path rather than silently solving the stale configuration."""
    dev, x, doping = _diode(Models(bgn=True, srh=True))
    assert dev._native is not None
    dev.models.tat = True
    dev.solve_bias([0.0, 0.2])
    assert dev._native is None
    assert dev.last_converged
    assert dev._Pn is not None  # the Python TAT path really ran


def test_native_path_public_api_shape():
    """Every attribute gui/services/solver_runner.py and examples/*.py
    are known to touch directly (see the Phase-0 attribute audit)
    still works identically through the native dispatch path."""
    dev, x, doping = _diode(Models(bgn=False, auger=True))
    assert dev._native is not None
    opts = NewtonOptions()
    dev.solve_equilibrium(opts)
    ret = dev.solve_bias([0.3, 0.0], opts)
    assert ret is dev  # matches the pure-Python path's own `return self`

    assert dev.psi.shape == (dev.N,)
    assert dev.n.shape == (dev.N,)
    assert dev.p.shape == (dev.N,)
    assert dev.psi_V.shape == (dev.N,)
    assert dev.n_cm3.shape == (dev.N,)
    assert dev.p_cm3.shape == (dev.N,)
    assert dev.E_field.shape == (dev.N - 1,)
    assert dev.Jn.shape == (dev.N - 1,)
    assert dev.Jp.shape == (dev.N - 1,)
    assert isinstance(dev.last_converged, bool) and dev.last_converged
    assert isinstance(dev.last_newton_err, float)
    j, spread = dev.current_density()
    assert np.isfinite(j) and np.isfinite(spread)

    # scaling constants the wrapper computes in Python and never hands
    # to _core -- x/N/h/VT/ni/eps/Ns/LD/J0/C must still be present.
    for attr in ("x", "N", "h", "VT", "ni", "eps", "Ns", "LD", "J0", "C"):
        assert hasattr(dev, attr), attr


def test_native_path_iv_sweep_and_band_diagram_still_work():
    dev, x, doping = _diode(Models(bgn=False, auger=True))
    assert dev._native is not None
    J = dev.iv_sweep([0.2, 0.3, 0.4], verbose=False)
    assert J.shape == (3,)
    assert np.all(np.isfinite(J))
    Ec, Ev, EFn, EFp = dev.band_diagram()
    for arr in (Ec, Ev, EFn, EFp):
        assert arr.shape == (dev.N,)
        assert np.all(np.isfinite(arr))


def test_schottky_contact_refuses_native_dispatch():
    from pytcad.device import SchottkyContact
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    dev = Device1D(x, doping, T=300.0, material=SILICON,
                   models=Models(bgn=False, auger=True),
                   schottky_left=SchottkyContact(phi_metal_eV=4.8))
    assert dev._native is None


def test_heterostructure_refuses_native_dispatch():
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    dev = Device1D(x, doping, T=300.0, material=[SILICON] * len(x),
                   models=Models(bgn=False, auger=True))
    assert dev._native is None

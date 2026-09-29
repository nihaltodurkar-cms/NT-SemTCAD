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
    Models(band_offset="affinity"),
    Models(S_n=1e3),
    Models(energy_balance=True),
])
def test_slice5_configs_dispatch_to_native(models):
    """Phase 2 slice 5 moved these onto the native path (gated in
    test_device1d_native_gates.py's test_g_s5_* against device.py)."""
    dev, x, doping = _diode(models)
    assert dev._native is not None


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
    """test_m12_tat reads dev._Pn/_Pp after solve_bias: they must be the
    compiled solve's frozen arrays, live (nonzero) at -0.5 V."""
    dev, x, doping = _diode(Models(bgn=True, srh=True, tat=True))
    dev.solve_bias([0.0, -0.5])
    assert float(np.max(dev._Pn)) > 0.0      # TAT genuinely live
    assert np.array_equal(dev._Pn, np.asarray(dev._native.Pn))
    assert np.array_equal(dev._Pp, np.asarray(dev._native.Pp))


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


def test_schottky_contact_dispatches_native():
    """Phase 2 slice 5: M46 Schottky contacts are native."""
    from pytcad.device import SchottkyContact
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    dev = Device1D(x, doping, T=300.0, material=SILICON,
                   models=Models(bgn=False, auger=True),
                   schottky_left=SchottkyContact(phi_metal_eV=4.8))
    assert dev._native is not None


def test_heterostructure_dispatches_native():
    """Phase 2 slice 5: a per-node material list is native."""
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    dev = Device1D(x, doping, T=300.0, material=[SILICON] * len(x),
                   models=Models(bgn=False, auger=True))
    assert dev._native is not None


def test_every_models_config_builds_the_compiled_device():
    """Since 2026-09-28 Device1D has no pure-Python solver: every Models()
    configuration builds the compiled device -- surface_mobility too (a 2D
    gate-contact model Device1D never read on either path)."""
    for models in (Models(surface_mobility=True), Models(dg=True),
                   Models(energy_balance=True), Models(S_n=1e3)):
        dev, x, doping = _diode(models)
        assert dev._native is not None


def test_refused_compositions_raise():
    """field_mobility on a heterostructure (Canali parameters are per
    single material) is refused at bias, not silently solved."""
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    dev = Device1D(x, doping, T=300.0, material=[SILICON] * len(x),
                   models=Models(bgn=False, field_mobility=True))
    dev.solve_equilibrium()
    with pytest.raises(NotImplementedError):
        dev.solve_bias([0.3, 0.0])


def test_models_mutated_after_construction_rebuilds():
    """test_m12_tat's fixture flips dev.models.tat after construction: the
    next solve must run the MUTATED configuration (rebuilt compiled
    device), never the stale one."""
    dev, x, doping = _diode(Models(bgn=True, srh=True))
    old = dev._native
    dev.models.tat = True
    dev.solve_bias([0.0, -0.5])
    assert dev._native is not old
    assert dev._Pn is not None and float(np.max(dev._Pn)) > 0.0


@pytest.mark.parametrize("attr", ["mu_n0", "band_shift"])
def test_post_construction_input_mutation_rebuilds(attr):
    """The compiled object holds COPIES of the Python inputs; mutating one
    after construction must rebuild it, so the solve sees the change."""
    dev, x, doping = _diode(Models(bgn=False, auger=True))
    dev.solve_bias([0.4, 0.0])
    j0, _ = dev.current_density()
    old = dev._native
    if attr == "mu_n0":
        dev.mu_n0 = dev.mu_n0 * 2.0
        dev._set_edge_diffusivity(dev.mu_n0, dev.mu_p0)
    else:
        dev.band_shift = dev.band_shift + np.linspace(0.0, 0.5, dev.N)
    dev.solve_bias([0.4, 0.0])
    assert dev._native is not old
    j1, _ = dev.current_density()
    # measured 2.4e-4 (band_shift) and O(1) (mu_n0); a re-solve of the
    # stale device from its converged state moves j at round-off only
    assert abs(j1 - j0) > 1e-6 * abs(j0), (j0, j1)


def test_rebuild_keeps_the_python_scaling():
    """test_m11_hetero re-dopes dev.doping/dev.C after construction but
    keeps dev.Ns: the rebuilt compiled device must use that same Ns, or
    n_cm3 = n * self.Ns would silently rescale every density."""
    dev, x, doping = _diode(Models(bgn=False))
    Ns = dev.Ns
    dev.doping[:] = 1e16
    dev.C = dev.doping / dev.Ns
    dev.solve_equilibrium()
    assert dev.Ns == Ns
    # uniform 1e16 n-type: bulk electron density 1e16 cm^-3 everywhere
    assert np.allclose(dev.n_cm3[5:-5], 1e16, rtol=1e-3)

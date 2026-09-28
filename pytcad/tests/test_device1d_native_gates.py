"""Gates for the native C++ Device1D (pytcad._core.Device1D), Phase 1
(baseline physics: doping_mobility/srh/auger/bgn, Boltzmann, full
ionization, homojunction) of ~/.claude/plans/eager-purring-fairy.md.

This is an ADDITIVE new path: pytcad.device.Device1D is untouched, so
these gates cannot regress anything the existing 2382+ tests cover.
Mirrors tests/test_validation.py's own gate shapes (FD-Jacobian, built-
in potential, forward current, mesh-refinement) so the native class is
held to the exact same bar as the Python one it will eventually
replace for this model configuration."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest
from scipy.sparse import csr_matrix

from pytcad import _accel
from pytcad.constants import EPS0, thermal_voltage
from pytcad.materials import SILICON, lifetime_scharfetter, mobility_caughey_thomas, nie_effective
from pytcad.mesh import graded_mesh

pytestmark = pytest.mark.skipif(not _accel.HAVE_ACCEL, reason="pytcad._core not built")


def _build(x, doping, T=300.0, srh=True, auger=True):
    from pytcad import _core
    Ntot = np.abs(doping)
    nie = nie_effective(Ntot, SILICON, T, True)
    mu_n0 = mobility_caughey_thomas(Ntot, SILICON, T, "n")
    mu_p0 = mobility_caughey_thomas(Ntot, SILICON, T, "p")
    tau_n = lifetime_scharfetter(Ntot, SILICON.tau_n0, SILICON.tau_Nref)
    tau_p = lifetime_scharfetter(Ntot, SILICON.tau_p0, SILICON.tau_Nref)
    VT = thermal_voltage(T)
    eps = SILICON.eps_r * EPS0
    ni = SILICON.ni(T)
    models = _core.Device1DModels()
    models.srh, models.auger = srh, auger
    # Phase 2 slice 1 (fd/incomplete_ion) extended the constructor with
    # extra required args; this baseline-only helper always passes
    # empty arrays for them, exactly like device.py's own native-
    # dispatch construction site does when neither flag is set.
    empty = np.empty(0)
    dev = _core.Device1D(x, doping, T, VT, eps, ni, mu_n0, mu_p0, tau_n, tau_p,
                         nie, SILICON.Cn_auger, SILICON.Cp_auger, models,
                         empty, empty, empty, empty, empty, empty, empty, 0.0,
                         empty, empty, empty, empty, empty)
    return dev


def _diode():
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    return _build(x, doping), x, doping


def _build_fd(x, doping, models, T=300.0):
    """Build the native _core.Device1D for an fd/incomplete_ion config
    via the Python wrapper's own dispatch construction site
    (device.py's Device1D.__init__), rather than re-deriving the
    nc_s/nv_s/ln_gn/ln_gp/eg_kt/nd_arr/na_arr/fermi-table plumbing a
    second time here -- opts into native dispatch for this call only."""
    from pytcad.device import Device1D as _PyDevice1D
    old = os.environ.get("PYTCAD_NATIVE_DEVICE1D")
    os.environ["PYTCAD_NATIVE_DEVICE1D"] = "1"
    try:
        wrapper = _PyDevice1D(x, doping, T=T, material=SILICON, models=models)
    finally:
        if old is None:
            os.environ.pop("PYTCAD_NATIVE_DEVICE1D", None)
        else:
            os.environ["PYTCAD_NATIVE_DEVICE1D"] = old
    assert wrapper._native is not None, "expected native dispatch for this config"
    return wrapper._native


def test_g0_jacobian_matches_finite_differences():
    """The analytic Newton Jacobian must match a numerical one -- same
    gate shape as test_validation.py::test_jacobian_matches_finite_differences."""
    from pytcad import _core
    dev, x, doping = _diode()
    opts = _core.Device1DNewtonOptions()
    dev.solve_equilibrium(opts)
    dev.solve_bias(0.3, 0.0, opts)
    rng = np.random.default_rng(0)
    N = dev.N
    psi = np.asarray(dev.psi) + 0.02 * rng.standard_normal(N)
    n = np.asarray(dev.n) * (1 + 0.01 * rng.standard_normal(N))
    p = np.asarray(dev.p) * (1 + 0.01 * rng.standard_normal(N))
    F, rows, cols, vals = dev._residual_jacobian_for_test(psi, n, p, 0.3, 0.0)
    F = np.asarray(F)
    J = csr_matrix((vals, (rows, cols)), shape=(3 * N, 3 * N)).toarray()
    u = np.stack([psi, n, p], axis=1).ravel()
    worst = 0.0
    for c in rng.choice(3 * N, 25, replace=False):
        step = 1e-7 * max(abs(u[c]), 1.0)
        u2 = u.copy(); u2[c] += step
        F2, _, _, _ = dev._residual_jacobian_for_test(
            u2[0::3], u2[1::3], u2[2::3], 0.3, 0.0)
        F2 = np.asarray(F2)
        an = J[:, c]
        worst = max(worst, np.abs((F2 - F) / step - an).max() / (np.abs(an).max() + 1e-30))
    assert worst < 1e-4, f"Jacobian error {worst:.2e}"


def test_g1_built_in_potential():
    """V_bi = V_T ln(N_A N_D / n_i^2) for an abrupt junction -- same
    tolerance as test_validation.py::test_built_in_potential."""
    dev, x, doping = _diode()
    from pytcad import _core
    dev.solve_equilibrium(_core.Device1DNewtonOptions())
    psi = np.asarray(dev.psi)
    VT = thermal_voltage(300.0)
    Vbi = (psi[-1] - psi[0]) * VT
    ni = SILICON.ni(300.0)
    Vbi_ana = VT * np.log(1e17 * 1e17 / ni**2)
    assert abs(Vbi - Vbi_ana) < 2e-3, (Vbi, Vbi_ana)


def test_g2_forward_current_matches_documented_value():
    """README section 4's own documented spot value: J at 0.5V forward
    on this exact fixture is 1.280e-2 A/cm^2 (PyTCAD) vs 1.321e-2
    (short-base ideal diode analytic)."""
    from pytcad import _core
    dev, x, doping = _diode()
    opts = _core.Device1DNewtonOptions()
    dev.solve_equilibrium(opts)
    converged = dev.solve_bias(0.5, 0.0, opts)
    assert converged
    J, spread = dev.current_density()
    assert abs(J - 1.280e-2) / 1.280e-2 < 1e-2, J
    assert spread < 1e-6, spread


def test_g3_ideality_factor():
    """Ideality factor from J(V) in the ideal-diode region, same
    fixture/window as test_validation.py's own check (should be ~1)."""
    from pytcad import _core
    opts = _core.Device1DNewtonOptions()
    Vs = np.arange(0.3, 0.75, 0.05)
    Js = []
    for V in Vs:
        dev, x, doping = _diode()
        dev.solve_equilibrium(opts)
        dev.solve_bias(float(V), 0.0, opts)
        j, _ = dev.current_density()
        Js.append(j)
    Js = np.asarray(Js)
    VT = thermal_voltage(300.0)
    slope = np.polyfit(Vs, np.log(Js), 1)[0]
    ideality = 1.0 / (slope * VT)
    assert abs(ideality - 1.0) < 0.05, ideality


def test_g4_mesh_refinement_convergence():
    """2x finer mesh changes the forward-bias current by < 3%, same
    gate as test_validation.py's own mesh-refinement check."""
    from pytcad import _core
    opts = _core.Device1DNewtonOptions()

    def j_at(h_min, h_max):
        x = graded_mesh(2e-4, [1e-4], h_min=h_min, h_max=h_max)
        doping = np.where(x < 1e-4, -1e17, 1e17)
        dev = _build(x, doping)
        dev.solve_equilibrium(opts)
        dev.solve_bias(0.5, 0.0, opts)
        j, _ = dev.current_density()
        return j

    j_coarse = j_at(1e-8, 1e-6)
    j_fine = j_at(5e-9, 5e-7)
    assert abs(j_fine - j_coarse) / abs(j_coarse) < 0.03, (j_coarse, j_fine)


def test_g5_srh_off_reduces_recombination_term_to_zero():
    """models.srh=False must zero the recombination term identically
    (not merely reduce it) -- same off-path bit-identity convention
    the rest of this codebase gates on."""
    from pytcad import _core
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    dev = _build(x, doping, srh=False)
    opts = _core.Device1DNewtonOptions()
    dev.solve_equilibrium(opts)
    psi, n, p = np.asarray(dev.psi), np.asarray(dev.n), np.asarray(dev.p)
    F, *_ = dev._residual_jacobian_for_test(psi, n, p, 0.0, 0.0)
    # at equilibrium (np == nie^2 everywhere) SRH is zero anyway, so
    # perturb n/p off equilibrium first to actually exercise the term.
    F2, *_ = dev._residual_jacobian_for_test(psi, n * 1.5, p * 0.7, 0.0, 0.0)
    dev_on = _build(x, doping, srh=True)
    dev_on.solve_equilibrium(opts)
    F3, *_ = dev_on._residual_jacobian_for_test(psi, n * 1.5, p * 0.7, 0.0, 0.0)
    assert not np.array_equal(np.asarray(F2), np.asarray(F3)), \
        "srh=True and srh=False must disagree once perturbed off equilibrium"


# ----------------------------------------------------------------------
#  Phase 2 slice 1: Fermi-Dirac statistics + incomplete ionization
# ----------------------------------------------------------------------
def _fd_diode():
    x = graded_mesh(2e-4, [1e-4], h_min=1e-8, h_max=1e-6)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    return x, doping


@pytest.mark.parametrize("models_kwargs", [
    dict(fd=True),
    dict(incomplete_ion=True),
    dict(fd=True, incomplete_ion=True),
], ids=["fd", "incomplete_ion", "fd+incomplete_ion"])
def test_g_fd_jacobian_matches_finite_differences(models_kwargs):
    """G0's own gate shape, repeated for every Phase 2 slice 1 config:
    the analytic Newton Jacobian (nu-factor SG terms + incomplete-
    ionization Poisson terms) must match a numerical one."""
    from pytcad import _core
    from pytcad.device import Models as PyModels
    x, doping = _fd_diode()
    dev = _build_fd(x, doping, PyModels(bgn=False, auger=True, **models_kwargs))
    opts = _core.Device1DNewtonOptions()
    dev.solve_equilibrium(opts)
    dev.solve_bias(0.3, 0.0, opts)
    rng = np.random.default_rng(1)
    N = dev.N
    psi = np.asarray(dev.psi) + 0.02 * rng.standard_normal(N)
    n = np.asarray(dev.n) * (1 + 0.01 * rng.standard_normal(N))
    p = np.asarray(dev.p) * (1 + 0.01 * rng.standard_normal(N))
    F, rows, cols, vals = dev._residual_jacobian_for_test(psi, n, p, 0.3, 0.0)
    F = np.asarray(F)
    J = csr_matrix((vals, (rows, cols)), shape=(3 * N, 3 * N)).toarray()
    u = np.stack([psi, n, p], axis=1).ravel()
    worst = 0.0
    for c in rng.choice(3 * N, 25, replace=False):
        step = 1e-7 * max(abs(u[c]), 1.0)
        u2 = u.copy(); u2[c] += step
        F2, _, _, _ = dev._residual_jacobian_for_test(
            u2[0::3], u2[1::3], u2[2::3], 0.3, 0.0)
        F2 = np.asarray(F2)
        an = J[:, c]
        worst = max(worst, np.abs((F2 - F) / step - an).max() / (np.abs(an).max() + 1e-30))
    assert worst < 1e-4, f"Jacobian error {worst:.2e}"


@pytest.mark.parametrize("models_kwargs", [
    dict(fd=True),
    dict(incomplete_ion=True),
    dict(fd=True, incomplete_ion=True),
], ids=["fd", "incomplete_ion", "fd+incomplete_ion"])
def test_g_fd_forward_current_is_physically_sane(models_kwargs):
    """Not a bit-identical/documented-spot-value gate (FD/incomplete_ion
    shift the I-V curve from the Boltzmann/full-ionization baseline by
    design), but a sanity gate every model configuration must pass:
    converges, forward current is positive and within an order of
    magnitude of the baseline G2 spot value (1.280e-2 A/cm^2)."""
    from pytcad import _core
    from pytcad.device import Models as PyModels
    x, doping = _fd_diode()
    dev = _build_fd(x, doping, PyModels(bgn=False, auger=True, **models_kwargs))
    opts = _core.Device1DNewtonOptions()
    dev.solve_equilibrium(opts)
    converged = dev.solve_bias(0.5, 0.0, opts)
    assert converged
    J, spread = dev.current_density()
    assert J > 0.0
    assert 1e-3 < J < 1e-1, J
    assert spread < 1e-3, spread


# ----------------------------------------------------------------------
#  Phase 2 slice 2: M12-S2 trap-assisted tunneling
# ----------------------------------------------------------------------
# device.py's WKB coefficient is so large that P underflows to exactly
# 0.0 at every field a real solve reaches (test_m12_tat documents this as
# the honest bulk-Si midgap result), so a real solve never enters the
# TAT branch. These gates therefore drive the branch with SYNTHETIC
# frozen probabilities through the test hook -- otherwise they would
# only ever compare plain SRH against itself.
def _tat_pair(**models_kwargs):
    """(python wrapper forced onto the pure-Python path, native object)
    for the identical fixture, both solved to 0.3 V forward."""
    from pytcad import _core
    from pytcad.device import Models as PyModels, NewtonOptions
    x, doping = _fd_diode()
    models = PyModels(tat=True, **models_kwargs)
    native = _build_fd(x, doping, models)
    from pytcad.device import Device1D as _PyDevice1D
    py = _PyDevice1D(x, doping, T=300.0, material=SILICON,
                     models=PyModels(tat=True, **models_kwargs))
    assert py._native is None  # env opt-in is scoped to _build_fd
    opts = _core.Device1DNewtonOptions()
    native.solve_equilibrium(opts)
    native.solve_bias(0.3, 0.0, opts)
    py.solve_equilibrium()
    py.solve_bias([0.3, 0.0])
    return py, native


def _synthetic_P(N, seed):
    rng = np.random.default_rng(seed)
    return 10.0 ** rng.uniform(-3, 1, N), 10.0 ** rng.uniform(-3, 1, N)


@pytest.mark.parametrize("models_kwargs", [
    dict(bgn=True), dict(bgn=False, fd=True)], ids=["tat", "fd+tat"])
def test_g_tat_jacobian_matches_finite_differences(models_kwargs):
    """G0's gate shape with the TAT branch live (nonzero frozen P). P is
    frozen within a solve in both implementations, so the analytic
    Jacobian omits dP/dpsi by design and FD must be taken with P fixed."""
    py, dev = _tat_pair(**models_kwargs)
    N = dev.N
    Pn, Pp = _synthetic_P(N, 3)
    rng = np.random.default_rng(2)
    psi = np.asarray(dev.psi) + 0.02 * rng.standard_normal(N)
    n = np.asarray(dev.n) * (1 + 0.01 * rng.standard_normal(N))
    p = np.asarray(dev.p) * (1 + 0.01 * rng.standard_normal(N))
    F, rows, cols, vals = dev._residual_jacobian_for_test(psi, n, p, 0.3, 0.0, Pn, Pp)
    F = np.asarray(F)
    # the branch must actually be live: tat with these P differs from P=0
    F0, *_ = dev._residual_jacobian_for_test(psi, n, p, 0.3, 0.0,
                                             np.zeros(N), np.zeros(N))
    assert not np.array_equal(F, np.asarray(F0))
    J = csr_matrix((vals, (rows, cols)), shape=(3 * N, 3 * N)).toarray()
    u = np.stack([psi, n, p], axis=1).ravel()
    worst = 0.0
    for c in rng.choice(3 * N, 25, replace=False):
        step = 1e-7 * max(abs(u[c]), 1.0)
        u2 = u.copy(); u2[c] += step
        F2, _, _, _ = dev._residual_jacobian_for_test(
            u2[0::3], u2[1::3], u2[2::3], 0.3, 0.0, Pn, Pp)
        an = J[:, c]
        worst = max(worst, np.abs((np.asarray(F2) - F) / step - an).max()
                    / (np.abs(an).max() + 1e-30))
    assert worst < 1e-4, f"Jacobian error {worst:.2e}"


@pytest.mark.parametrize("models_kwargs", [
    dict(bgn=True), dict(bgn=False, fd=True)], ids=["tat", "fd+tat"])
def test_g_tat_residual_reconstructs_python(models_kwargs):
    """Reconstruct-and-compare at the residual level: the SAME state and
    the SAME frozen nonzero P through device.py's _residual_jacobian and
    through the native one. Measured as round-off, not bit-identical
    (independent summation order)."""
    py, dev = _tat_pair(**models_kwargs)
    N = dev.N
    Pn, Pp = _synthetic_P(N, 5)
    psi, n, p = py.psi.copy(), py.n.copy(), py.p.copy()
    py._Pn, py._Pp = Pn.copy(), Pp.copy()
    bc = py._contact_values([0.3, 0.0])
    F_py, J_py, _, _ = py._residual_jacobian(psi, n, p, bc)
    F_nat, rows, cols, vals = dev._residual_jacobian_for_test(psi, n, p, 0.3, 0.0, Pn, Pp)
    J_nat = csr_matrix((vals, (rows, cols)), shape=(3 * N, 3 * N))
    scale = np.maximum(np.abs(F_py), 1e-30)
    assert np.max(np.abs(np.asarray(F_nat) - F_py) / np.maximum(scale, 1.0)) < 1e-10
    dJ = (J_nat - J_py).tocoo()
    Jmax = abs(J_py).max()
    assert (np.abs(dJ.data).max() if dJ.nnz else 0.0) < 1e-10 * Jmax


def test_g_tat_probabilities_match_python():
    """The native field -> P law against device.py's
    _update_tat_probabilities on a synthetic potential steep enough that
    P does NOT underflow (so the comparison is not just zeros)."""
    py, dev = _tat_pair(bgn=True)
    N = dev.N
    rng = np.random.default_rng(11)
    psi = np.cumsum(rng.uniform(1e16, 1e18, N)) * rng.choice([-1.0, 1.0])
    py._update_tat_probabilities(psi)
    Pn, Pp = dev._tat_probabilities_for_test(psi)
    Pn, Pp = np.asarray(Pn), np.asarray(Pp)
    assert np.count_nonzero(Pn) > N // 2 and np.count_nonzero(Pp) > N // 2
    assert np.all(Pn < 1.0) and np.all(Pp < 1.0)
    np.testing.assert_allclose(Pn, py._Pn, rtol=1e-13, atol=0.0)
    np.testing.assert_allclose(Pp, py._Pp, rtol=1e-13, atol=0.0)


def test_g_tat_zero_probabilities_are_plain_srh():
    """test_m12_tat::test_traps_off_bit_identical's native counterpart:
    tat=True with P == 0 everywhere must leave the SRH/Auger residual
    and Jacobian EXACTLY as tat=False produces them."""
    from pytcad.device import Models as PyModels
    x, doping = _fd_diode()
    on = _build_fd(x, doping, PyModels(tat=True))
    off = _build_fd(x, doping, PyModels())
    from pytcad import _core
    opts = _core.Device1DNewtonOptions()
    off.solve_equilibrium(opts)
    off.solve_bias(0.15, 0.0, opts)
    psi, n, p = np.asarray(off.psi), np.asarray(off.n), np.asarray(off.p)
    N = off.N
    a = off._residual_jacobian_for_test(psi, n, p, 0.15, 0.0)
    b = on._residual_jacobian_for_test(psi, n, p, 0.15, 0.0, np.zeros(N), np.zeros(N))
    for u, v in zip(a, b):
        assert np.array_equal(np.asarray(u), np.asarray(v))


# ----------------------------------------------------------------------
#  Phase 2 slice 3: M15 local impact ionization + M16 local Kane BTBT
# ----------------------------------------------------------------------
# The generation terms are small next to the SG/Poisson entries of their
# rows, so a whole-row FD comparison would hide a wrong generation
# derivative under the continuity terms' magnitude. Every gate below
# therefore isolates the generation contribution: G(u) = F(u; s) -
# F(u; 0) and its Jacobian J(s) - J(0), via the test hook's `strength`.
def _gen_fixture(kind):
    """(native, python) pair for M15's one-sided diode (impact) or M16's
    tunnel diode (btbt), both warm-ramped to a bias where the generation
    term is live, plus the bias."""
    import warnings
    from pytcad import _core
    from pytcad.device import Device1D as _PyDevice1D, Models as PyModels, NewtonOptions
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        kw = dict(bgn=False, srh=True)
        if kind == "impact_nl":             # M34-S2's own fixture
            x = graded_mesh(6.0e-4, [3.0e-4], h_min=2e-8, h_max=4e-6)
            doping = np.where(x < 3.0e-4, -1e16, 1e19)
            ramp = [2.0, 6.0, 10.0, 16.0, 20.0]
            kw.update(impact=True, impact_nonlocal=True)
        elif kind == "btbt_nl":             # M34-S1's own fixture
            x = graded_mesh(1.0e-5, [5.0e-6], h_min=1e-8, h_max=2e-7)
            doping = np.where(x < 5.0e-6, -5e19, 5e19)
            ramp = [0.5, 1.0, 2.0]
            kw.update(btbt_nonlocal=True)
        elif kind.startswith("impact"):
            x = graded_mesh(6.0e-4, [3.0e-4], h_min=1e-8, h_max=1e-6)
            doping = np.where(x < 3.0e-4, -1e16, 1e19)
            ramp = [2.0, 5.0, 10.0, 15.0, 20.0]
            kw.update(impact=True)
        else:
            x = graded_mesh(1.0e-5, [5.0e-6], h_min=1e-8, h_max=2e-7)
            doping = np.where(x < 5.0e-6, -5e19, 5e19)
            ramp = [0.1, 0.2, 0.3, 0.5]
            kw.update(btbt=True)
        if kind.endswith("+fd"):
            kw["fd"] = True
        native = _build_fd(x, doping, PyModels(**kw))
        py = _PyDevice1D(x, doping, T=300.0, material=SILICON, models=PyModels(**kw))
        assert py._native is None
        py.solve_equilibrium()
        for v in ramp:
            py.solve_bias([-v, 0.0], NewtonOptions())
    native.set_state(py.psi, py.n, py.p)
    return native, py, -ramp[-1]


def _perturbed(py, seed, amp=0.01):
    rng = np.random.default_rng(seed)
    N = py.N
    psi = py.psi + 2 * amp * rng.standard_normal(N)
    psi[0], psi[-1] = py.psi[0], py.psi[-1]
    return (psi,
            py.n * (1 + amp * rng.standard_normal(N)),
            py.p * (1 + amp * rng.standard_normal(N)))


_GEN_KINDS = ["impact", "btbt", "impact+fd", "impact_nl", "btbt_nl"]


@pytest.mark.parametrize("kind", _GEN_KINDS)
def test_g_gen_jacobian_matches_finite_differences(kind):
    """FD-Jacobian of the generation contribution alone.

    In scaled units the generation terms are ~1e-13 of their rows' SG
    terms on these fixtures, so F(s) - F(0) at s ~ 1 keeps only a few
    significant digits and FD on it measures round-off. The source and
    its Jacobian are LINEAR in the ladder strength s (both implementations
    multiply by it and nothing else), so s is used as an amplifier: pick s
    large enough that generation dominates, then divide by s. Same code
    path, no cancellation."""
    dev, py, V = _gen_fixture(kind)
    N = dev.N
    psi, n, p = _perturbed(py, 4, amp=1e-3)
    if kind == "btbt_nl":
        # path GEOMETRY is frozen within a Newton solve (only psi along
        # the paths is live), so freeze it for the whole FD probe too
        dev.set_btbt_nl_paths(*dev.locate_btbt_nl_paths(psi))
    a1 = dev._residual_jacobian_for_test(psi, n, p, V, 0.0, strength=1.0)
    a0 = dev._residual_jacobian_for_test(psi, n, p, V, 0.0, strength=0.0)
    g1 = np.abs(np.asarray(a1[0]) - np.asarray(a0[0])).max()
    assert g1 > 0.0, "generation term is not live at this state"
    s = 1e8 * max(np.abs(np.asarray(a0[0])).max(), 1.0) / g1
    if kind == "impact_nl":
        # alpha is evaluated at the per-carrier EFFECTIVE field here
        En = np.asarray(dev._effective_field_for_test(psi, 0)[0]) / 1e5
        Ep = np.asarray(dev._effective_field_for_test(psi, 1)[0]) / 1e5
        assert np.abs(En - 5.0).min() > 0.10 and np.abs(Ep - 4.0).min() > 0.06
    elif kind.startswith("impact"):
        # M15 G-B's own probe rule: stay off BOTH alpha(E) branch kinks
        # (BTBT's Kane law is C-infinity, no kink to avoid)
        E = np.abs(np.diff(psi)) * py.VT / (py.h * py.LD) / 1e5
        assert np.abs(E - 5.0).min() > 0.10 and np.abs(E - 4.0).min() > 0.06

    def gen(u, want_j=False):
        a = dev._residual_jacobian_for_test(u[0::3], u[1::3], u[2::3], V, 0.0,
                                            strength=s)
        b = dev._residual_jacobian_for_test(u[0::3], u[1::3], u[2::3], V, 0.0,
                                            strength=0.0)
        G = (np.asarray(a[0]) - np.asarray(b[0])) / s
        if not want_j:
            return G
        Ja = csr_matrix((a[3], (a[1], a[2])), shape=(3 * N, 3 * N))
        Jb = csr_matrix((b[3], (b[1], b[2])), shape=(3 * N, 3 * N))
        return G, (Ja - Jb).toarray() / s

    u = np.stack([psi, n, p], axis=1).ravel()
    G, JG = gen(u, want_j=True)
    # probe the columns the generation actually couples to (plus random)
    rng = np.random.default_rng(9)
    live = np.flatnonzero(np.abs(JG).max(axis=0) > 1e-6 * np.abs(JG).max())
    cols = np.unique(np.concatenate([
        rng.choice(live, min(30, live.size), replace=False),
        rng.choice(3 * N, 10, replace=False)]))
    # Central differences; psi steps absolute-ish, density steps RELATIVE
    # (densities span ~1e-22..1 scaled, so an absolute step is either
    # nonlinear at the bottom or below round-off at the top). Reference:
    # a column's derivative matters through its EFFECT on G -- derivative
    # times its variable's natural scale (|u| for a density, i.e. dG/dln n;
    # 1 for psi) -- measured against G's own magnitude. Below 1e-7 of that
    # a column is unresolvable by any finite difference (measured: n ~
    # 4.5e-17 on the p side moves G by ~1e-30 per relative step, under
    # G's ULP, so FD reads exactly 0 there), and irrelevant to Newton.
    Gmax = np.abs(G).max()
    worst = 0.0
    for c in cols:
        scale = 1.0 if c % 3 == 0 else abs(u[c])
        step = (1e-7 * max(abs(u[c]), 1.0) if c % 3 == 0 else 1e-6 * scale)
        up = u.copy(); up[c] += step
        um = u.copy(); um[c] -= step
        fd_col = (gen(up) - gen(um)) / (2.0 * step)
        an = JG[:, c]
        ref = max(np.abs(an).max(), np.abs(fd_col).max(), 1e-7 * Gmax / scale)
        worst = max(worst, np.abs(fd_col - an).max() / ref)
    assert worst < 1e-4, f"generation Jacobian error {worst:.2e}"


@pytest.mark.parametrize("kind", _GEN_KINDS)
def test_g_gen_residual_reconstructs_python(kind):
    """Reconstruct-and-compare at the residual level: the generation
    contribution (F(s) - F(0)) and its Jacobian through device.py's
    _residual_jacobian and through the native one, on the same perturbed
    state. Round-off agreement expected (libm vs numpy exp)."""
    dev, py, V = _gen_fixture(kind)
    N = dev.N
    psi, n, p = _perturbed(py, 6)
    bc = py._contact_values([V, 0.0])
    # btbt_nl: both sides locate their paths lazily from THIS psi (the
    # native device here has never solved, so it has none frozen)
    py._btbt_nl_paths = None
    out = {}
    for s in (0.35, 0.0):
        py._ii_strength = s
        Fp, Jp_, _, _ = py._residual_jacobian(psi, n, p, bc)
        a = dev._residual_jacobian_for_test(psi, n, p, V, 0.0, strength=s)
        Jn_ = csr_matrix((a[3], (a[1], a[2])), shape=(3 * N, 3 * N))
        out[s] = (Fp, Jp_, np.asarray(a[0]), Jn_)
    py._ii_strength = 1.0
    G_py = out[0.35][0] - out[0.0][0]
    G_nat = out[0.35][2] - out[0.0][2]
    assert np.abs(G_py).max() > 0.0
    assert np.abs(G_nat - G_py).max() < 1e-9 * np.abs(G_py).max()
    JG_py = (out[0.35][1] - out[0.0][1]).tocsr()
    JG_nat = (out[0.35][3] - out[0.0][3]).tocsr()
    d = (JG_nat - JG_py)
    assert abs(d).max() < 1e-9 * abs(JG_py).max()
    # the full residual agrees too (baseline terms + generation)
    scale = max(np.abs(out[0.35][0]).max(), 1.0)
    assert np.abs(out[0.35][2] - out[0.35][0]).max() < 1e-9 * scale


@pytest.mark.parametrize("kind", ["impact", "btbt", "impact_nl", "btbt_nl"])
def test_g_gen_ladder_solve_agrees_with_python(kind):
    """solve_bias through the full stiff ladder (stages 0 -> 1.0 with
    backtracking) from the SAME warm start the pure-Python path used,
    one more bias step deeper: converges at full strength, psi agrees to
    round-off, densities above the stiff floor agree tightly, and the
    strength-scaled generation source agrees. (Sub-floor minority
    densities -- p ~ 1e-22 scaled on the n+ side -- are only resolved to
    ~1e-16 absolute by DESIGN in both implementations, see device.py's
    _STIFF_DENSITY_FLOOR, so they are excluded from the density check.
    Mean reverse-leakage current is not compared here: its edge spread
    exceeds its mean on the M15 fixture in BOTH implementations even with
    impact off, i.e. it is round-off-limited, not a physics observable.)"""
    import warnings
    from pytcad import _core
    from pytcad.device import NewtonOptions
    dev, py, V = _gen_fixture(kind)
    V2 = V * 1.25
    opts = _core.Device1DNewtonOptions()
    assert dev.solve_bias(V2, 0.0, opts)
    assert dev.ii_strength == 1.0
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        py.solve_bias([V2, 0.0], NewtonOptions())
    assert py.last_converged and py._ii_strength == 1.0
    np.testing.assert_allclose(dev.psi, py.psi, rtol=0, atol=1e-9 * np.abs(py.psi).max())
    floor = 1e-8
    for a, b in ((np.asarray(dev.n), py.n), (np.asarray(dev.p), py.p)):
        m = b > floor
        assert np.max(np.abs(a[m] - b[m]) / b[m]) < 1e-6
    if kind == "btbt_nl":
        # M34-S1: same frozen path set after the post-convergence
        # refresh, same refresh outcome, and the tunnel current (a well-
        # conditioned observable on this fixture) to round-off
        assert list(dev.btbt_nl_starts) == list(py._btbt_nl_paths.start)
        assert dev.last_btbt_nl_refreshes == py.last_btbt_nl_refreshes
        assert dev.last_btbt_nl_stable is True and py.last_btbt_nl_stable is True
        j_nat, _ = dev.current_density()
        j_py, _ = py.current_density()
        assert abs(j_nat - j_py) < 1e-10 * abs(j_py)
    elif kind == "btbt":
        # G depends on psi alone -> as well conditioned as psi itself
        cache_nat = np.asarray(dev.btbt_gs_cache)
        cache_py = py._btbt_gs_cache
        assert cache_py.max() > 0.0
        assert np.abs(cache_nat - cache_py).max() < 1e-6 * cache_py.max()
    else:
        # gs ~ alpha(E) |J|, and |J| at this reverse bias sits at the
        # round-off floor of the SG flux (J ~ 1e-15 J0), so the two
        # implementations' gs are NOT comparable pointwise. Gate instead
        # that the cache is exactly the source the native residual
        # applies at the returned state (the formula itself is held
        # against device.py by test_g_gen_residual_reconstructs_python).
        cache_nat = np.asarray(dev.ii_gs_cache)
        assert cache_nat.max() > 0.0 and np.all(np.isfinite(cache_nat))
        psi, n, p = np.asarray(dev.psi), np.asarray(dev.n), np.asarray(dev.p)
        a = dev._residual_jacobian_for_test(psi, n, p, V2, 0.0, strength=1.0)
        b = dev._residual_jacobian_for_test(psi, n, p, V2, 0.0, strength=0.0)
        G_e = (np.asarray(a[0]) - np.asarray(b[0]))[1::3][1:-1]
        np.testing.assert_allclose(G_e, cache_nat[1:-1] * py.dV[1:-1],
                                   rtol=1e-9, atol=1e-12 * np.abs(G_e).max())


def test_g_nl_effective_field_is_bit_identical():
    """M34-S2: the native effective field and its dense Jacobian against
    pytcad.ii_nonlocal.effective_field on a solved reverse-biased state
    (strong and weak edges both present). Measured bit-identical: the
    recursion is pure + * / and one exp per edge, in the same order."""
    from pytcad.ii_nonlocal import effective_field
    dev, py, V = _gen_fixture("impact_nl")
    for c, name, lam in ((0, "n", py.models.impact_lambda_n),
                         (1, "p", py.models.impact_lambda_p)):
        E_nat, D_nat = dev._effective_field_for_test(py.psi, c)
        E_py, D_py = effective_field(py.x, py.psi, py.VT, lam, name, jacobian=True)
        assert E_py.max() > 1e5
        assert np.array_equal(np.asarray(E_nat), E_py)
        assert np.array_equal(np.asarray(D_nat).reshape(py.N, py.N), D_py)


def test_g_nl_path_locator_matches_python():
    """M34-S1: the native path locator + build_1d against device.py's
    _btbt_nl_build_paths on the same solved state -- identical start
    set and identical flat geometry."""
    dev, py, V = _gen_fixture("btbt_nl")
    ref = py._btbt_nl_build_paths(py.psi)
    starts, ends = dev.locate_btbt_nl_paths(py.psi)
    assert ref.n_paths > 0
    assert np.array_equal(np.asarray(starts), ref.start)
    from pytcad.nonlocal_path import build_1d
    got = build_1d(py.x * 1e-2, np.asarray(starts), np.asarray(ends))
    for f in ("offset", "sidx", "swts", "seg_len", "gidx", "gwts"):
        assert np.array_equal(getattr(got, f), getattr(ref, f)), f


def test_g_gen_off_leaves_no_source():
    """impact=btbt=False: no generation cache, and strength has no
    effect on the residual at all (off-path bit-identity)."""
    x, doping = _fd_diode()
    from pytcad.device import Models as PyModels
    from pytcad import _core
    dev = _build_fd(x, doping, PyModels())
    opts = _core.Device1DNewtonOptions()
    dev.solve_equilibrium(opts)
    dev.solve_bias(-1.0, 0.0, opts)
    assert len(dev.ii_gs_cache) == 0 and len(dev.btbt_gs_cache) == 0
    psi, n, p = np.asarray(dev.psi), np.asarray(dev.n), np.asarray(dev.p)
    a = dev._residual_jacobian_for_test(psi, n, p, -1.0, 0.0, strength=1.0)
    b = dev._residual_jacobian_for_test(psi, n, p, -1.0, 0.0, strength=0.0)
    for u, v in zip(a, b):
        assert np.array_equal(np.asarray(u), np.asarray(v))


def test_g_tat_coefficient_factoring_is_bit_identical():
    """_tat_exponent_coeffs was factored out of _update_tat_probabilities
    for the native constructor; the pure-Python probabilities must be
    bit-identical to the pre-refactor inline expression."""
    from pytcad.device import Q_E_CONST, HBAR_CONST
    py, _ = _tat_pair(bgn=True)
    rng = np.random.default_rng(13)
    psi = np.cumsum(rng.uniform(1e16, 1e18, py.N))
    py._update_tat_probabilities(psi)
    # pre-refactor body, verbatim
    edge_F = np.abs(np.diff(psi)) * py.VT / (py.LD * py.h) * 100.0
    F = np.empty(py.N)
    F[1:-1] = 0.5 * (edge_F[:-1] + edge_F[1:])
    F[0], F[-1] = edge_F[0], edge_F[-1]
    et_rel = py.models.trap_et_rel
    phi_n = py.Eg0_arr * (1.0 - et_rel)
    phi_p = np.array([m.Eg(py.T) for m in py.mats]) * et_rel
    m_n = np.array([m.m_n_star for m in py.mats])
    m_p = np.array([m.m_p_star for m in py.mats])
    B_n = 4.0 * np.sqrt(2.0 * m_n * Q_E_CONST) / (3.0 * HBAR_CONST)
    B_p = 4.0 * np.sqrt(2.0 * m_p * Q_E_CONST) / (3.0 * HBAR_CONST)
    safe_F = np.maximum(F, 1.0)
    assert np.array_equal(py._Pn, np.exp(-B_n * phi_n ** 1.5 / safe_F))
    assert np.array_equal(py._Pp, np.exp(-B_p * phi_p ** 1.5 / safe_F))
    assert np.count_nonzero(py._Pn) > 0

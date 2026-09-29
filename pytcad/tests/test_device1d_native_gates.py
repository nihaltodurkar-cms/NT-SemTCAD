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


def _py_only(*args, **kwargs):
    """A wrapper-level Device1D (pytcad.device) -- since 2026-09-28 a thin
    shell over the same compiled device, so this is the public-API route
    to it, not an independent reference."""
    from pytcad.device import Device1D as _PyDevice1D
    return _PyDevice1D(*args, **kwargs)


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
# At realizable junction fields bulk-Si midgap P is ~1e-14 or smaller
# (test_m12_tat), so a real solve's TAT branch is numerically plain
# SRH+Auger. These gates therefore drive the branch with SYNTHETIC frozen
# probabilities of O(1e-3..10) through the test hook, so the tunneling
# terms themselves are exercised, not just the branch's P ~ 0 limit.
# (Until 2026-09-28 device.py's WKB coefficient lacked the m0 factor and
# P underflowed to exactly 0.0 everywhere -- see _tat_exponent_coeffs.)
def _tat_pair(**models_kwargs):
    """(python wrapper forced onto the pure-Python path, native object)
    for the identical fixture, both solved to 0.3 V forward."""
    from pytcad import _core
    from pytcad.device import Models as PyModels, NewtonOptions
    x, doping = _fd_diode()
    models = PyModels(tat=True, **models_kwargs)
    native = _build_fd(x, doping, models)
    from pytcad.device import Device1D as _PyDevice1D
    py = _py_only(x, doping, T=300.0, material=SILICON,
                     models=PyModels(tat=True, **models_kwargs))
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
        py = _py_only(x, doping, T=300.0, material=SILICON, models=PyModels(**kw))
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


# ======================================================================
# Phase 2 slice 5: heterostructures (eps(x), the M33-S1 affinity gauge,
# M33-S2 thermionic flux), M14 S_n/S_p Robin contacts, M46 Schottky
# contacts, lagged field mobility, M44 energy balance, M20 DG equilibrium.
# ======================================================================
import dataclasses
import warnings

from pytcad.device import Device1D as _Dev, Models as _M, SchottkyContact


def _s5_mesh():
    x = graded_mesh(2e-4, [1e-4], h_min=1e-7, h_max=4e-6, ratio=1.25)
    return x, np.where(x < 1e-4, -1e17, 1e17)


def _s5_hetero_mats(x, **right_kw):
    right = dataclasses.replace(SILICON, name="s5-right", **right_kw)
    return [SILICON if xi < 1e-4 else right for xi in x]


def _s5_make(kind):
    """Constructor kwargs for one slice-5 configuration."""
    x, dop = _s5_mesh()
    het = dict(chi=4.35, eps_r=12.0, Eg0=1.3, Cn_auger=3e-31)
    if kind == "hetero_nie":
        return dict(x=x, doping=dop, material=_s5_hetero_mats(x, **het),
                    models=_M(bgn=False))
    if kind == "hetero_affinity":
        return dict(x=x, doping=dop, material=_s5_hetero_mats(x, **het),
                    models=_M(bgn=False, band_offset="affinity"))
    if kind == "hetero_fd":
        return dict(x=x, doping=dop, material=_s5_hetero_mats(x, **het),
                    models=_M(bgn=False, fd=True))
    if kind.startswith("thermionic"):
        # 0.3 eV offset (|u| ~ 11 at the interface, the realistic
        # barrier) plus +-0.03 eV (|u| ~ 1): the emission slots' min()
        # branches are u > 0 / w < 0 for a positive offset and the
        # reverse for a negative one, and at |u| ~ 11 the active
        # derivative slot is ~e^-11 of its row -- a sign error there is
        # invisible to FD (measured by mutation, 2026-09-28), so the
        # small offsets are what actually exercise every branch.
        chi = {"thermionic": 4.35, "thermionic_up": 4.08, "thermionic_dn": 4.02}[kind]
        return dict(x=x, doping=np.full_like(x, 1e17),
                    material=_s5_hetero_mats(x, chi=chi),
                    models=_M(bgn=False, band_offset="affinity", thermionic=True))
    if kind == "S":
        return dict(x=x, doping=dop, models=_M(bgn=False, S_n=1e4, S_p=1e3))
    if kind == "S_fd":
        return dict(x=x, doping=dop, models=_M(bgn=False, fd=True, S_n=1e4, S_p=1e4))
    if kind in ("schottky", "schottky_robin"):
        from pytcad.schottky import richardson_a_star
        xs = graded_mesh(2e-4, [0.0], h_min=1e-6, h_max=4e-6)
        a = richardson_a_star("Si", "n") if kind == "schottky_robin" else None
        return dict(x=xs, doping=np.full_like(xs, 1e16), models=_M(bgn=False),
                    schottky_left=SchottkyContact(4.8, a))
    if kind == "field_mobility":
        return dict(x=x, doping=dop, models=_M(bgn=False, field_mobility=True))
    if kind == "energy_balance":
        return dict(x=x, doping=dop, models=_M(bgn=False, energy_balance=True))
    if kind == "dg":
        return dict(x=x, doping=dop, models=_M(bgn=False, dg=True))
    raise KeyError(kind)


_S5_BIAS = {"thermionic": 0.1, "thermionic_up": 0.1, "thermionic_dn": 0.1,
            "schottky": 0.2, "schottky_robin": 0.2}
_S5_KINDS = ["hetero_nie", "hetero_affinity", "hetero_fd", "thermionic",
             "thermionic_up", "thermionic_dn", "S", "S_fd", "schottky",
             "schottky_robin", "field_mobility", "energy_balance"]


def _s5_pair(kind):
    """(pure-Python device, native-dispatched device) for `kind`."""
    old = os.environ.get("PYTCAD_NATIVE_DEVICE1D")
    try:
        os.environ["PYTCAD_NATIVE_DEVICE1D"] = "0"
        py = _Dev(**_s5_make(kind))
        os.environ["PYTCAD_NATIVE_DEVICE1D"] = "1"
        nat = _Dev(**_s5_make(kind))
    finally:
        if old is None:
            os.environ.pop("PYTCAD_NATIVE_DEVICE1D", None)
        else:
            os.environ["PYTCAD_NATIVE_DEVICE1D"] = old
    assert nat._native is not None, kind
    return py, nat


def _s5_state(dev, seed):
    """A perturbed state off the converged one (so every term is live)."""
    rng = np.random.default_rng(seed)
    N = dev.N
    return (dev.psi + 0.05 * rng.standard_normal(N),
            dev.n * np.exp(0.1 * rng.standard_normal(N)),
            dev.p * np.exp(0.1 * rng.standard_normal(N)))


def _row_scaled_fd_error(Ffun, u, J, steps, colscale, rows=None):
    """Worst |central FD - analytic| over EVERY column, measured as an
    EFFECT ON F: entry (r, c) is weighted by column c's natural variation
    colscale[c] (1 for psi, |u_c| for a density) and divided by row r's
    largest such effect. A raw per-row entry scale is blind to the
    psi-columns of a minority-carrier row (they carry a factor p ~ 1e-14
    that the density columns do not -- measured: a sign flip in the hole
    thermionic slot passed it, 2026-09-28). Each entry's FD round-off
    bound, 1e-14 * (row's largest |J_rc u_c|) / step_c, is subtracted
    first: a minority-density column of an O(1) Poisson row is otherwise
    pure noise (measured FD 0 or 10 against the exact, linear dV = 3.2).
    Entries FD cannot resolve are left to the reconstruct gate, which
    compares against device.py exactly."""
    worst = 0.0
    eff = np.abs(J) * colscale[None, :]
    rowscale = eff.max(axis=1) + 1e-300
    termscale = (np.abs(J) * np.abs(u)[None, :]).max(axis=1)
    sel = np.arange(J.shape[0]) if rows is None else np.asarray(rows)
    for c in range(u.size):
        up = u.copy(); up[c] += steps[c]
        um = u.copy(); um[c] -= steps[c]
        fd = (Ffun(up) - Ffun(um)) / (2.0 * steps[c])
        noise = 1e-14 * termscale / steps[c]
        excess = np.maximum(np.abs(fd - J[:, c]) - noise, 0.0) * colscale[c]
        worst = max(worst, float((excess / rowscale)[sel].max()))
    return worst


def _s5_solved(dev, kind):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        dev.solve_equilibrium()
        dev.solve_bias([_S5_BIAS.get(kind, 0.4), 0.0])
    return dev


@pytest.mark.parametrize("kind", ["hetero_affinity", "thermionic", "thermionic_up",
                                  "thermionic_dn", "S", "S_fd", "schottky_robin"])
def test_g_s5_jacobian_matches_finite_differences(kind):
    """Central-difference FD-Jacobian of the native assembly over every
    column, row-scaled: the eps(x)/band-shift SG terms, the thermionic
    slot replacement, and the Robin contact rows including their fd
    chain (restricted to those four rows for the contact kinds)."""
    _, nat = _s5_pair(kind)
    _s5_solved(nat, kind)
    V = [_S5_BIAS.get(kind, 0.4), 0.0]
    psi, n, p = _s5_state(nat, 5)
    N = nat.N
    u = np.stack([psi, n, p], axis=1).ravel()

    def F(uu):
        return np.asarray(nat._native._residual_jacobian_for_test(
            uu[0::3], uu[1::3], uu[2::3], V[0], V[1])[0])

    _, r, c, v = nat._native._residual_jacobian_for_test(psi, n, p, V[0], V[1])
    J = csr_matrix((v, (r, c)), shape=(3 * N, 3 * N)).toarray()
    is_psi = np.arange(3 * N) % 3 == 0
    steps = np.where(is_psi, 1e-7, 1e-6 * np.abs(u))
    rows = None
    if kind in ("S", "S_fd", "schottky_robin"):
        rows = [1, 2, 3 * (N - 1) + 1, 3 * (N - 1) + 2]   # the Robin rows
    worst = _row_scaled_fd_error(F, u, J, steps, np.where(is_psi, 1.0, np.abs(u)), rows)
    assert worst < 1e-5, f"{kind}: FD-Jacobian error {worst:.2e}"


def test_g_s5_energy_balance_jacobian_matches_finite_differences():
    """The M44 theta block (rows/cols 3N..4N-1) against central FD, with
    the lagged inputs held fixed exactly as device.py's derivation says
    (the psi/n/p columns of the theta rows are genuinely zero)."""
    _, nat = _s5_pair("energy_balance")
    _s5_solved(nat, "energy_balance")
    N = nat.N
    psi, n, p = _s5_state(nat, 6)
    rng = np.random.default_rng(7)
    theta = 1.0 + 0.5 * rng.random(N)
    lag = (n * 1.01, 1e-3 * rng.standard_normal(N - 1), 1e-4 * rng.standard_normal(N))

    def full(th):
        return nat._native._residual_jacobian_eb_for_test(
            psi, n, p, 0.4, 0.0, th, *lag)

    _, r, c, v = full(theta)
    J = csr_matrix((v, (r, c)), shape=(4 * N, 4 * N)).toarray()[3 * N:, 3 * N:]
    worst = _row_scaled_fd_error(lambda th: np.asarray(full(th)[0])[3 * N:], theta,
                                 J, np.full(N, 1e-6), np.ones(N))
    assert worst < 1e-6, f"energy-balance FD-Jacobian error {worst:.2e}"


def test_g_s5_dg_jacobian_matches_finite_differences():
    """M20 coupled (psi, Lambda_n, Lambda_p) Jacobian against central FD
    at a perturbed state off the converged DG equilibrium, and the
    native residual equals device.py's _dg_residual_jacobian_eq."""
    _, nat = _s5_pair("dg")
    nat.solve_equilibrium()
    N = nat.N
    rng = np.random.default_rng(8)
    psi = nat.psi + 0.05 * rng.standard_normal(N)
    Ln = nat._dg_Lam_n + 1e-3 * rng.standard_normal(N)
    Lp = nat._dg_Lam_p + 1e-3 * rng.standard_normal(N)
    gamma = 0.7
    Fn, r, c, v = nat._native._dg_residual_jacobian_for_test(psi, Ln, Lp, gamma)
    J = csr_matrix((v, (r, c)), shape=(3 * N, 3 * N)).toarray()
    Fp, Jp = nat._dg_residual_jacobian_eq(psi, Ln, Lp, nat._contact_values([0.0, 0.0]),
                                          gamma=gamma)
    assert np.abs(np.asarray(Fn) - Fp).max() <= 1e-14 * np.abs(Fp).max()
    assert np.abs(J - Jp.toarray()).max() <= 1e-14 * np.abs(J).max()
    u = np.stack([psi, Ln, Lp], axis=1).ravel()

    def F(uu):
        return np.asarray(nat._native._dg_residual_jacobian_for_test(
            uu[0::3], uu[1::3], uu[2::3], gamma)[0])

    is_psi = np.arange(3 * N) % 3 == 0
    steps = np.where(is_psi, 1e-7, 1e-7 * nat.VT)
    worst = _row_scaled_fd_error(F, u, J, steps, np.where(is_psi, 1.0, nat.VT))
    assert worst < 1e-5, f"DG FD-Jacobian error {worst:.2e}"


def test_g_s5_refusals():
    """Unvalidated compositions are refused: DG+fd at equilibrium, and a
    Robin Schottky contact with S_n != 0 at bias."""
    from pytcad.schottky import richardson_a_star
    old = os.environ.get("PYTCAD_NATIVE_DEVICE1D")
    os.environ["PYTCAD_NATIVE_DEVICE1D"] = "1"
    try:
        x, dop = _s5_mesh()
        d = _Dev(x, dop, models=_M(bgn=False, dg=True, fd=True))
        with pytest.raises(NotImplementedError):
            d.solve_equilibrium()
        xs = graded_mesh(2e-4, [0.0], h_min=1e-6, h_max=4e-6)
        d2 = _Dev(xs, np.full_like(xs, 1e16), models=_M(bgn=False, S_n=1e4),
                  schottky_left=SchottkyContact(4.8, richardson_a_star("Si", "n")))
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            d2.solve_equilibrium()
        with pytest.raises(NotImplementedError):
            d2.solve_bias([0.1, 0.0])
    finally:
        if old is None:
            os.environ.pop("PYTCAD_NATIVE_DEVICE1D", None)
        else:
            os.environ["PYTCAD_NATIVE_DEVICE1D"] = old


# ======================================================================
# Compiled material evaluation (tcad/device1d/inputs.hpp) vs materials.py
# ======================================================================
def _reference_inputs(dev):
    """Device1D's derived inputs recomputed from materials.py / constants.py
    directly -- the pre-2026-09-29 Python __init__ arithmetic, kept here as
    the independent reference (materials.py is still what Device2D/Device3D
    evaluate)."""
    from pytcad import hydrodynamic
    from pytcad.constants import EPS0, KB, KB_EV, Q, thermal_voltage
    from pytcad.device import emission_velocity
    from pytcad.materials import (lifetime_scharfetter, mobility_caughey_thomas,
                                  nie_effective)
    T, mats, Ntot = dev.T, dev.mats, dev.Ntot
    r = {"VT": thermal_voltage(T)}
    r["eps_arr"] = np.array([m.eps_r * EPS0 for m in mats])
    r["ni"] = mats[0].ni(T)
    r["Ns"] = max(float(np.abs(dev.doping).max()), r["ni"])
    r["LD"] = np.sqrt(r["eps_arr"][0] * r["VT"] / (Q * r["Ns"]))
    r["J0"] = Q * 1.0 * r["Ns"] / r["LD"]
    r["R0"] = 1.0 * r["Ns"] / r["LD"] ** 2
    xs = dev.x / r["LD"]
    r["h"] = np.diff(xs)
    r["C"] = dev.doping / r["Ns"]
    N = dev.N
    for k in ("nie", "mu_n0", "mu_p0", "tau_n", "tau_p", "nc_s", "nv_s", "eg_kt"):
        r[k] = np.empty(N)
    seen = []
    for mm in mats:
        if not any(mm is m2 for m2 in seen):
            seen.append(mm)
    for m in seen:
        nodes = np.array([mm is m for mm in mats])
        nt = Ntot[nodes]
        r["nie"][nodes] = nie_effective(nt, m, T, dev.models.bgn)
        r["mu_n0"][nodes] = (mobility_caughey_thomas(nt, m, T, "n")
                             if dev.models.doping_mobility else m.mu_n_max)
        r["mu_p0"][nodes] = (mobility_caughey_thomas(nt, m, T, "p")
                             if dev.models.doping_mobility else m.mu_p_max)
        r["tau_n"][nodes] = lifetime_scharfetter(nt, m.tau_n0, m.tau_Nref)
        r["tau_p"][nodes] = lifetime_scharfetter(nt, m.tau_p0, m.tau_Nref)
        r["nc_s"][nodes] = m.Nc(T) / r["Ns"]
        r["nv_s"][nodes] = m.Nv(T) / r["Ns"]
        r["eg_kt"][nodes] = m.Eg(T) / (KB_EV * T)
    r["nie_s"] = r["nie"] / r["Ns"]
    r["ln_gn"] = np.log(r["nc_s"] / r["nie_s"])
    r["ln_gp"] = np.log(r["nv_s"] / r["nie_s"])
    if dev.models.band_offset == "affinity":
        s = r["ln_gn"] + np.array([m.chi for m in mats]) / r["VT"]
        r["band_shift"] = s - s[0]
    hm = lambda a: 2.0 * a[:-1] * a[1:] / (a[:-1] + a[1:])
    r["dn_edge"] = hm(r["mu_n0"]) * r["VT"] / 1.0
    r["dp_edge"] = hm(r["mu_p0"]) * r["VT"] / 1.0
    if dev.models.thermionic:
        r["_te_Kn"] = hm(emission_velocity(r["nc_s"] * r["Ns"], T)) * r["LD"] / 1.0
        r["_te_Kp"] = hm(emission_velocity(r["nv_s"] * r["Ns"], T)) * r["LD"] / 1.0
    r["_ALPHA_RELAX"] = (1.5 * KB * T * r["Ns"] * r["LD"]
                         / (hydrodynamic.TAU_W_N * r["J0"] * r["VT"]))
    r["_KAPPA0"] = (2.5 * (KB * KB / Q) * r["Ns"] * T * T
                    / (r["LD"] * r["J0"] * r["VT"]))
    return r


def _mat_kinds():
    from pytcad.materials import GAAS, GE
    x = graded_mesh(2e-4, [1e-4], h_min=1e-7, h_max=4e-6, ratio=1.25)
    dop = np.where(x < 1e-4, -1e17, 1e18)
    right = dataclasses.replace(SILICON, name="mr", chi=4.35, eps_r=12.0, Eg0=1.3)
    side = [SILICON if xi < 1e-4 else right for xi in x]
    return {
        "homojunction": dict(x=x, doping=dop),
        "bgn+mob off, 350K": dict(x=x, doping=dop, T=350.0,
                                  models=_M(bgn=False, doping_mobility=False)),
        "degenerate fd": dict(x=x, doping=np.where(x < 1e-4, -1e20, 1e20), models=_M(fd=True)),
        "Si/GaAs": dict(x=x, doping=dop, material=[SILICON if xi < 1e-4 else GAAS for xi in x]),
        "Ge/GaAs": dict(x=x, doping=dop, material=[GE if xi < 1e-4 else GAAS for xi in x]),
        "affinity": dict(x=x, doping=dop, material=side, models=_M(band_offset="affinity")),
        "thermionic": dict(x=x, doping=np.full_like(x, 1e17), material=side,
                           models=_M(band_offset="affinity", thermionic=True)),
    }


@pytest.mark.parametrize("kind", list(_mat_kinds()))
def test_g_materials_compiled_inputs_match_materials_py(kind):
    """The compiled material evaluation (scaling, nie with Slotboom BGN,
    Caughey-Thomas mobility, Scharfetter lifetime, band-DOS, affinity band
    shift, thermionic velocities, M44 constants) equals materials.py's.
    Measured 2026-09-29: bit-identical in every case below (same libm);
    1e-14 leaves room for a numpy build whose vectorized exp/log differ in
    the last bit, far below any physical effect."""
    import warnings as _w
    with _w.catch_warnings():
        _w.simplefilter("ignore", UserWarning)
        dev = _Dev(**_mat_kinds()[kind])
    ref = _reference_inputs(dev)
    for k, v in ref.items():
        got = np.asarray(getattr(dev, k), dtype=float)
        np.testing.assert_allclose(got, np.asarray(v, dtype=float), rtol=1e-14, atol=0.0,
                                   err_msg=f"{kind}: {k}")


def test_g_materials_tat_and_schottky_match_python_formulas():
    """TAT exponent numerators (B = 4 sqrt(2 m* m0 q)/(3 hbar), SI) and the
    M46 Schottky contact densities, compiled vs their Python formulas."""
    from pytcad.btbt import M0_SI
    from pytcad.constants import KB_EV
    from pytcad.device import HBAR_CONST, Q_E_CONST
    x = graded_mesh(2e-4, [1e-4], h_min=1e-7, h_max=4e-6, ratio=1.25)
    dev = _Dev(x, np.where(x < 1e-4, -1e17, 1e17), models=_M(tat=True, trap_et_rel=0.4))
    kn, kp = dev._tat_exponent_coeffs()
    m = dev.mats[0]
    Bn = 4.0 * np.sqrt(2.0 * m.m_n_star * M0_SI * Q_E_CONST) / (3.0 * HBAR_CONST)
    Bp = 4.0 * np.sqrt(2.0 * m.m_p_star * M0_SI * Q_E_CONST) / (3.0 * HBAR_CONST)
    np.testing.assert_allclose(kn, Bn * (m.Eg0 * 0.6) ** 1.5, rtol=1e-14)
    np.testing.assert_allclose(kp, Bp * (m.Eg(dev.T) * 0.4) ** 1.5, rtol=1e-14)
    xs = graded_mesh(2e-4, [0.0], h_min=1e-6, h_max=4e-6)
    for dop, phi in ((1e16, 4.8), (-1e16, 4.6)):
        d = _Dev(xs, np.full_like(xs, dop), models=_M(bgn=False),
                 schottky_left=SchottkyContact(phi))
        (_, n0, p0), _ = d._contact_values([0.0, 0.0])
        kT, nie = KB_EV * d.T, d.nie_s[0]
        phi_Bn = phi - d.mats[0].chi
        if dop > 0:
            n_ref = d.nc_s[0] * np.exp(-phi_Bn / kT); p_ref = nie * nie / n_ref
        else:
            p_ref = d.nv_s[0] * np.exp(-(d.mats[0].Eg(d.T) - phi_Bn) / kT); n_ref = nie * nie / p_ref
        np.testing.assert_allclose([n0, p0], [n_ref, p_ref], rtol=1e-14)


def test_g_materials_constants_match_constants_py():
    """The compiled physical constants ARE pytcad.constants'."""
    from pytcad import _accel
    from pytcad.constants import EPS0, HBAR, KB, KB_EV, M0, Q
    assert tuple(_accel.core.MATERIAL_CONSTANTS) == (Q, KB, KB_EV, EPS0, HBAR, M0)

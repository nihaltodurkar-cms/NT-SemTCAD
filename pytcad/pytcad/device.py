"""1D drift-diffusion device simulator (the "device TCAD" half of the tool).

THE EQUATIONS
-------------
We solve the classical van Roosbroeck system, self-consistently, in steady
state:

    Poisson        d/dx ( eps dpsi/dx ) = -q ( p - n + N_D^+ - N_A^- )
    electrons      dJn/dx = +q R
    holes          dJp/dx = -q R
    constitutive   Jn = q mu_n n E + q D_n dn/dx = q mu_n n dpsi/dx? ...

written in the drift-diffusion form with Einstein relation D = mu kT/q:

    Jn = q D_n ( -n dpsi/dx / V_T + dn/dx ) * (-1)   [sign per convention]
    Jp = -q D_p ( p dpsi/dx / V_T + dp/dx )

Symbols:
    psi   electrostatic potential [V]        n, p   carrier densities [cm^-3]
    Jn,Jp current densities [A/cm^2]         R      net recombination [cm^-3 s^-1]
    N_D^+, N_A^-  ionised dopant densities [cm^-3]  (full ionisation assumed)
    eps   permittivity [F/cm]                V_T = kT/q

Assumptions and their limits:
  * Boltzmann statistics -- breaks down above ~1e19 cm^-3 (degeneracy);
    the code warns you when the doping crosses that.
  * Full dopant ionisation -- fails at cryogenic temperature.
  * Classical (no quantisation) -- an inversion layer in a modern MOSFET is
    a ~2 nm quantum well; the classical result puts the charge centroid at
    the interface and overestimates gate capacitance by ~10-20%.
  * Steady state, isothermal, no impact ionisation or tunnelling.

DISCRETISATION
--------------
Box (finite-volume) integration on a non-uniform 1D mesh.  Currents on the
interfaces use the Scharfetter-Gummel scheme, which integrates the
drift-diffusion equation exactly under the assumption that J and E are
constant across one cell:

    Jn_{i+1/2} = (q D_n / h) [ n_{i+1} B(d) - n_i B(-d) ],  d = (psi_{i+1}-psi_i)/V_T

with the Bernoulli function B(x) = x / (e^x - 1).  This is the single most
important numerical ingredient: naive central differencing of the drift term
oscillates and goes negative as soon as the potential drop across a cell
exceeds ~2 V_T (52 mV), which happens everywhere in a depletion region.

SCALING
-------
Newton on the raw variables is hopeless: psi ~ 1, n ~ 1e20, R ~ 1e25.  We use
the de Mari scaling
    psi -> psi/V_T,  n,p -> n/n_i,  x -> x/L_D,  L_D = sqrt(eps V_T/(q n_i))
which brings every residual to order unity.
"""

import warnings

from dataclasses import dataclass, replace

import numpy as np

from . import _accel

# M12-S2 physical constants for the WKB escape factors
Q_E_CONST = 1.602176634e-19       # C
HBAR_CONST = 1.054571817e-34      # J s
M_E_CONST = 9.1093837015e-31      # kg
from scipy.sparse import csr_matrix


def emission_velocity(N_dos_cm3, T):
    """M33-S2: thermionic emission velocity [cm/s] for a band whose
    effective DOS is `N_dos_cm3` (Nc for electrons, Nv for holes).

        v = sqrt(kT / (2 pi m_DOS))

    m_DOS is recovered from the material's OWN band DOS through
    `N = 2 (2 pi m kT / h^2)^{3/2}`, so this needs no new material
    constant and cannot drift away from the Nc/Nv the rest of the
    solver uses. Algebraically it is the familiar `v = A* T^2 / (q N)`
    with `A* = 4 pi q m k^2 / h^3` -- the same formula, expressed
    through the mass that N itself implies.

    IT DOES NOT REPRODUCE A TABULATED A*, AND THAT IS EXPECTED RATHER
    THAN A DEFECT. Measured for silicon at 300 K: this returns
    2.575e6 cm/s where `richardson_a_star("Si","n") * T^2 / (q Nc)`
    gives 4.950e6 -- a factor 1.92. The cause is exactly what
    `schottky.py`'s own module docstring warns about: A* is governed by
    the RICHARDSON mass, not the DOS mass, and silicon's six-valley
    conduction band separates the two. Nc = 2.86e19 implies
    m_DOS = 1.09 m0, while A* = 252 A/(cm^2 K^2) implies 2.1 m0, and
    2.1/1.09 = 1.92. So a tabulated A* is the more accurate number for
    Si SPECIFICALLY, while this form is the one that stays consistent
    with the Nc the solver actually uses and is defined for every
    material including alloys. Treat the velocity as good to about a
    factor of two on silicon; see M33-INTERFACE-PLAN.md section 7.

    Deriving m_DOS from N rather than reading a tabulated A* also
    sidesteps a real trap: that table is keyed on SHORT names ("Si",
    "GaAs") while Semiconductor.name holds full ones ("Silicon"), so
    richardson_a_star(mat.name, "n") raises KeyError for every real
    material object -- and an alloy like AlGaAs has no entry at all.
    """
    N_si = np.asarray(N_dos_cm3, dtype=float) * 1.0e6          # m^-3
    h = 2.0 * np.pi * HBAR_CONST
    kT = KB_EV * Q_E_CONST * T                                 # J
    m_dos = (N_si / 2.0) ** (2.0 / 3.0) * h * h / (2.0 * np.pi * kT)
    return np.sqrt(kT / (2.0 * np.pi * m_dos)) * 100.0         # cm/s

from . import linsolve

from .constants import KB, KB_EV, Q, thermal_voltage
from .fermi import (
    FERMI_ETA_MAX, FERMI_ETA_MIN, f_half, f_half_inv, f_mhalf,
)
from .ionization import Q_E as _II_Q
from .btbt import Q_SI as _NL_Q
from .nonlocal_path import build_1d as _nl_build_1d
from .nonlocal_path import evaluate as _nl_evaluate
from .ii_nonlocal import LAMBDA_E_SLOTBOOM_CM as _II_LAMBDA_E

# M15 R1b, ATTEMPT 3 (2026-08-28): impact-ionization generation is
# coupled DIRECTLY into the Newton residual/Jacobian every iterate
# (dG/dpsi, dG/dn, dG/dp folded into _residual_jacobian, chain-ruled
# through the same SG flux partials already computed there) -- no
# frozen source, no outer fixed-point loop.  This Jacobian is UNCHANGED
# from attempts 1 and 2 (both FD-Jacobian-validated); what changed is
# giving pytcad.continuation.arc_length_sweep's corrector its OWN
# generation-strength ramp (see arc_length_sweep's `strength_stages`
# parameter), rather than relying on solve_bias's ladder, which the
# corrector never calls into (that composition gap was attempt 2's
# failure).  A generation-strength continuation ladder is kept for
# Newton robustness navigating the stiff avalanche onset -- see
# _II_STAGES below -- but it now ramps a single scalar multiplying the
# LIVE, fully-coupled term, not a cached array.
_II_STAGES = (0.0, 0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 1.0)
# M34-S7: the stiff paths' line search halves at most this many times
# (lam >= 2^-10) before taking the full step.  A smaller step cannot
# cover the Newton correction within max_iter = 100, and at a merit on
# its round-off floor such steps "pass" the decrease test by noise --
# measured: M16's tunnel diode at -0.2V accepted lam = 4.8e-7 every
# iteration (merit 1.3e-28 -> 1.3e-28) and never moved.  Was 40.
_LS_MAX_HALVINGS = 10
# M34-S7: the line search is a globalization device for the stiff onset
# (updates of O(1)); once the full Newton correction is below this (psi
# within 1e-3 VT, densities within 0.1%, where the exponentials are
# linear) Newton takes full steps.  Near convergence the merit sits on
# its round-off floor and accepts partial steps by noise, which stops
# the quadratic finish -- measured: M15's diode at -40V, stage 0.7, took
# lam = 1/8..1/64 for 100 iterations at merit 4.2e-26 with a ~1e-8
# correction left, where full steps close (1e-8 -> 1e-15 at -30V).
_LS_NEWTON_REGION = 1e-3
# M34-S7: density floor of the stiff paths' update test (M11-S5's 1e-10
# elsewhere).  The test demands |dn| < tol_update * floor below it, i.e.
# 1e-18 at 1e-10 -- tighter than double precision resolves there:
# measured, sub-floor densities in converged II solves sit in a round-off
# limit cycle of 2-5e-18 (M15 at -32V: p = 3.1e-11; M34-S2 at -20V: p
# 1.1e-17 <-> 1.6e-17 on the n+ side, period 2) with the residual at
# round-off and the current fixed to every digit.  At 1e-8 a sub-floor
# density carries <= 1e-8 of the local charge (M11-S5's own argument) and
# its tolerance, 1e-16, is 20x the measured cycle.
_STIFF_DENSITY_FLOOR = 1e-8
# The leading 0.0 stage is a plain drift-diffusion Newton solve (no
# generation at all) at the NEW bias before any coupling turns on.  It
# replaces the old frozen-source model's implicit protection against
# the contact-stamping field spike: solve_bias stamps psi[0]/psi[-1] to
# the new bias while interior nodes still hold the OLD bias's converged
# profile, so the cell adjacent to the contact reads a transient field
# of order (bias step)/(cell width) -- MV/cm scale for the nm-scale
# contact cells this milestone's test devices use -- until Newton
# relaxes it away.  The frozen model never saw this because it computed
# gs from the smooth PRE-stamp state once and cached it; live coupling
# has no such protection, so without a generation-free relaxation pass
# first, alpha(E) evaluated at that transient spike injects enormous
# spurious generation at iteration 0 and Newton can lock onto a bogus
# high-field state pinned at the contact instead of the real solution
# (verified: E-field at the contact-adjacent node reached ~4e6 V/cm at
# a bias where the impact=False device -- solving the identical contact
# stamp with no generation term at all -- settles at ~2.6e-8 V/cm).

# R1b coupling also chain-rules dG/dn, dG/dp through sign(Jn)/sign(Jp)
# (section 1's spec: "including sign(J) factors, valid away from J=0
# crossings").  In practice an edge current crosses zero SOMEWHERE in
# every biased diode (electron and hole current trade off along the
# device), so a literal sign()/abs() makes |Jn|/|Jp| non-differentiable
# at points Newton's own iterates land on or near -- not a rare probe-
# state edge case but a routine occurrence that stalled the Newton
# backtracking outright (verified: an iterate at a node with a ~1e-9-
# scaled Jp sitting on a ~50-scaled slope, i.e. a hair from its zero
# crossing, made every trial step a non-descent direction).  Both
# |J| and sign(J) are smoothed with a fixed tiny regularizer:
#   smooth_abs(J)  = sqrt(J^2 + eps^2)
#   smooth_sign(J) = J / sqrt(J^2 + eps^2)
# eps is _II_J_EPS_REL times the LARGER of the two edge-current arrays'
# max magnitude for that residual evaluation, so it scales with whatever
# current regime the device is in and only perturbs the immediate
# neighbourhood of an exact zero crossing (a 1e-6 relative deviation
# everywhere else is far below the 5e-5 FD-Jacobian gate and orders of
# magnitude below the physics being resolved).
_II_J_EPS_REL = 1e-6


def _ii_smooth_abs(J, eps):
    return np.sqrt(J * J + eps * eps)


def _ii_smooth_sign(J, eps):
    return J / np.sqrt(J * J + eps * eps)
from .materials import (
    SILICON, Semiconductor,
)

# Moved to kernels.py -- pure, mesh-free, vectorized primitives that
# device2d/device3d/unstructured_dd*/moscap were importing out of this
# module.  Re-exported here so every existing import site keeps working.
from .kernels import (  # noqa: E402  (re-export, must follow the imports above)
    D0_REF, bernoulli, dbernoulli, fd_density, fd_ddensity_deta,
)

# M44: coupled electron energy balance reuses M29's already-gated local
# closure inverse (effective_field_from_temperature) and TAU_W_N -- see
# M44-HYDRODYNAMIC-PLAN.md Slice 0, Finding 3.
from . import hydrodynamic as _hydro


def fd_node_factors(nc_s, nv_s, n, p):
    """nu-factor SG quantities on ARBITRARILY shaped density grids
    (shared by the 1D/2D/3D cores; plan section 3.2bis).

    L_x = ln nu_x with nu = F(eta) exp(-eta); w_x = dL/d(density) in
    the cancellation-safe form (F'/F - 1)/(Nc_s F').  For eta <= -30
    both are set to EXACT 0.0 (deep-Boltzmann edges reproduce the
    Boltzmann scheme bit-for-bit), and those nodes never enter
    f_half_inv at all."""
    thr = float(f_half(-30.0))
    Ln = np.zeros_like(n)
    Lp = np.zeros_like(p)
    wn = np.zeros_like(n)
    wp = np.zeros_like(p)
    # broadcast DOS against the density grid so scalar-DOS cores
    # (2D/3D) and per-node arrays (1D heterojunctions) both work
    den_n = np.broadcast_to(np.asarray(nc_s, dtype=float), np.shape(n))
    den_p = np.broadcast_to(np.asarray(nv_s, dtype=float), np.shape(p))
    mn = (n / den_n) > thr
    mp = (p / den_p) > thr
    if bool(mn.any()):
        en = f_half_inv(np.maximum(n[mn], 1e-300) / den_n[mn])
        Fn = f_half(en)
        dFn = f_mhalf(en)
        Ln[mn] = np.log(Fn) - en
        wn[mn] = (dFn / Fn - 1.0) / (den_n[mn] * dFn)
    if bool(mp.any()):
        ep = f_half_inv(np.maximum(p[mp], 1e-300) / den_p[mp])
        Fp = f_half(ep)
        dFp = f_mhalf(ep)
        Lp[mp] = np.log(Fp) - ep
        wp[mp] = (dFp / Fp - 1.0) / (den_p[mp] * dFp)
    return Ln, Lp, wn, wp


def ionized_dE_kt(T):
    """Shallow-dopant ionization energy in units of kT.

    45 meV hydrogenic B/P/As, the single M13 number -- shared so the
    1D core, the 2D/3D cores (M41) and the neutrality roots cannot
    drift apart."""
    return 0.045 / (KB_EV * T)


def ionized_eta_doping(nd, na, eta_n, eta_p, ded_kt):
    """Net IONIZED doping from the reduced Fermi energies (M13).

        N_D+ = N_D / (1 + 2 e^{eta_n + dE/kT})
        N_A- = N_A / (1 + 4 e^{eta_p + dE/kT})

    Returns (cion, dcion_deta_n, dcion_deta_p) with cion = ND+ - NA-,
    all scaled the same way nd/na are.  Split out of Device1D's
    `_ionized_C` (M41) so the 2D/3D cores and every neutrality-root
    bisection evaluate ONE formula; the exponent clamps are the 1D
    ones, unchanged."""
    ed_n = np.exp(np.minimum(eta_n + ded_kt, 700.0))
    ea_p = np.exp(np.minimum(eta_p + ded_kt, 700.0))
    ndp = nd / (1.0 + 2.0 * ed_n)
    nam = na / (1.0 + 4.0 * ea_p)
    dndp_deta = -2.0 * ed_n / (1.0 + 2.0 * ed_n) ** 2 * nd
    dnam_deta = -4.0 * ea_p / (1.0 + 4.0 * ea_p) ** 2 * na
    return ndp - nam, dndp_deta, -dnam_deta


def ionized_doping(nd, na, n, p, nc_s, nv_s, T):
    """Net ionized doping and its derivatives wrt the SLOT DENSITIES.

    The density chain is d(eta)/d(density) = 1/(Nc_s F'(eta)), with the
    exact tail derivative exp(eta) below the validated range
    (consistent with fd_density's piecewise policy).  Independent of
    the `fd` flag by design -- see Models.incomplete_ion.

    Returns (cion, d cion/dn, d cion/dp)."""
    en = f_half_inv(np.maximum(n, 1e-300) / nc_s)
    ep = f_half_inv(np.maximum(p, 1e-300) / nv_s)
    cion, dc_den, dc_dep = ionized_eta_doping(nd, na, en, ep,
                                              ionized_dE_kt(T))
    tail_n = np.exp(np.minimum(en, 700.0))
    tail_p = np.exp(np.minimum(ep, 700.0))
    den_n = np.where(en >= FERMI_ETA_MIN,
                     f_mhalf(np.clip(en, FERMI_ETA_MIN,
                                     FERMI_ETA_MAX)), tail_n)
    den_p = np.where(ep >= FERMI_ETA_MIN,
                     f_mhalf(np.clip(ep, FERMI_ETA_MIN,
                                     FERMI_ETA_MAX)), tail_p)
    detn = 1.0 / np.maximum(nc_s * den_n, 1e-300)
    detp = 1.0 / np.maximum(nv_s * den_p, 1e-300)
    return cion, dc_den * detn, dc_dep * detp


def fd_ohmic_values(C, nc_s, nv_s, ln_gn, eg_kt, V, VT, ion=None):
    """FD ohmic-contact values for ARBITRARY node sets (vectorized
    bisection; the exact Boltzmann closed form is recovered as
    F -> exp).  All inputs broadcast against each other; C is the SCALED
    net doping at the contact nodes.  Returns (psi0, n0, p0) scaled.

    `ion`, if given, is (nd, na, T): the neutrality root then balances
    the net IONIZED doping ND+(e) - NA-(e) instead of C (M13 incomplete
    ionization, lifted to 2D/3D by M41).  `ion=None` is the full-
    ionization path every pre-M41 caller uses, bit-identical."""
    C = np.asarray(C, dtype=float)

    def dens(e):
        return fd_density(nc_s, np.minimum(e, FERMI_ETA_MAX)), \
            fd_density(nv_s, np.minimum(-e - eg_kt, FERMI_ETA_MAX))

    lo = -eg_kt - (FERMI_ETA_MAX - FERMI_ETA_MIN) - 1.0
    hi = np.full(np.shape(C), float(FERMI_ETA_MAX))

    def g(e):
        n_, p_ = dens(e)
        if ion is None:
            return n_ - p_ - C
        # the eta-space form, evaluated at the SAME clamped etas the
        # densities above use -- identical to Device1D's own
        # _fd_neutral_eta, which this replaces for 2D/3D contacts
        nd, na, T_ = ion
        c_, _, _ = ionized_eta_doping(
            nd, na, np.minimum(e, FERMI_ETA_MAX),
            np.minimum(-e - eg_kt, FERMI_ETA_MAX), ionized_dE_kt(T_))
        return n_ - p_ - c_

    flo, fhi = g(lo), g(hi)
    if np.any(flo > 0) or np.any(fhi < 0):
        raise ValueError(
            "FD contact neutrality root not bracketed at a contact "
            "(doping outside the model's validated regime?)")
    for _ in range(300):
        mid = 0.5 * (lo + hi)
        left = g(mid) < 0
        lo = np.where(left, mid, lo)
        hi = np.where(left, hi, mid)
        if np.all(hi - lo < 3e-15 * (1.0 + np.abs(lo))):
            break
    e0 = 0.5 * (lo + hi)
    if np.any(e0 > FERMI_ETA_MAX - 2.0):
        raise ValueError(
            "FD contact eta beyond the validated range; refusing "
            "(M13 G7 applicability limit).")
    n0, p0 = dens(e0)
    psi0 = V / VT + e0 + ln_gn
    return psi0, n0, p0


# ----------------------------------------------------------------------
#  Solver options
# ----------------------------------------------------------------------
@dataclass
class Models:
    doping_mobility: bool = True
    field_mobility: bool = False   # lagged; enable for high-field devices
    srh: bool = True
    # M12-S2: trap-assisted tunneling (Hurkx-style field-enhanced SRH,
    # plan-specified form with WKB escape probabilities in the
    # denominator).  Default OFF => bit-identical to plain SRH.
    tat: bool = False
    trap_et_rel: float = 0.5          # trap level as fraction of Eg
    # M13 phase 2: Fermi-Dirac carrier statistics (parabolic-band
    # F_{1/2}, nu-factor generalized SG -- plan section 3.2bis).
    # Default OFF => bit-identical to Boltzmann (G6a goldens).
    fd: bool = False
    # M13 phase 2: incomplete dopant ionization (shallow B/P/As,
    # degeneracy factors g_D=2 / g_A=4, DeltaE=45 meV).  Independent
    # of fd; 1D only in this milestone.  Hydrogenic model: invalid
    # above the Mott transition (~4e18 cm^-3) and for compensated
    # profiles (net-doping input carries no species split).
    incomplete_ion: bool = False
    # M15: local van Overstraeten-de Man impact ionization.  Default
    # OFF => bit-identical to the plain solver (goldens).
    impact: bool = False
    # M34-S2: nonlocal (effective-field) impact ionization -- M15's
    # alpha evaluated at a per-carrier effective field from the
    # relaxation equation lambda dE_eff/ds + E_eff = |E| along the
    # carrier's drift direction (pytcad/ii_nonlocal.py; Slotboom et al.,
    # IEDM 1991: lambda_e = 650 A).  Requires impact=True.  lambda_p =
    # lambda_n is a named simplification.  Default OFF => the local M15
    # model, bit-identical.  1D only (Device2D/3D implement `impact`
    # since M34-S6 but still refuse this flag).
    impact_nonlocal: bool = False
    impact_lambda_n: float = _II_LAMBDA_E     # cm
    impact_lambda_p: float = _II_LAMBDA_E     # cm
    # M16: local Kane band-to-band tunneling (Hurkx 1992 Si
    # coefficients, G = A F^2 exp(-B/F)).  Default OFF => bit-identical
    # to the plain solver (goldens).  1D only; Device2D/3D raise.
    btbt: bool = False
    # M34-S1: nonlocal PATH Kane BTBT (Esseni et al. 2017,
    # doi:10.1088/1361-6641/aa6fca, eqs 9/11/12 -- see
    # M34-S1-PLAN.md).  Homojunction 1D ONLY (raises otherwise).
    # INDEPENDENT of `btbt` above: this is a first-principles WKB path
    # integral, NOT calibrated against and not numerically comparable
    # to KANE_A_SI/KANE_B_SI's empirical Hurkx fit -- see
    # pytcad/btbt.py's module docstring.  Reduces exactly to its own
    # published uniform-field closed form (eq 8) -- gated in
    # tests/test_model_benchmarks.py.  Window node spans frozen once
    # per solve_bias call (same cadence `_update_tat_probabilities`
    # uses); psi across each span is live in the residual AND the
    # Jacobian.  Default OFF => bit-identical to the plain solver
    # (goldens).
    btbt_nonlocal: bool = False
    # M20: density-gradient quantum correction (Ancona-Stafford form,
    # quantum potential on the slaved equilibrium densities -- see
    # pytcad/dg.py and M20-DENSITY-GRADIENT-PLAN.md).  Default OFF =>
    # bit-identical (goldens).  EQUILIBRIUM-ONLY in this milestone:
    # solve_bias raises on dg=True (DG transport is out of scope);
    # Device2D/Device3D raise on construction.
    dg: bool = False
    dg_gamma: float = 1.0        # Ancona calibration factor (1 = Bohm)
    auger: bool = True
    bgn: bool = True               # bandgap narrowing
    # M14: surface / inversion-layer mobility (Lombardi CVT).
    # Default OFF => bit-identical to the solver without surface
    # scattering (golden gate G-D).  Applied lagged in the Newton
    # loop on 2D devices with a gate contact; raises in 1D/3D.
    surface_mobility: bool = False
    # M14: driving-force choice for high-field mobility in 2D.
    # "field" (default): parallel electric field E (existing behavior).
    # "quasi_fermi": grad(quasi-Fermi) = grad(phi_n) or grad(phi_p),
    # the Sentaurus convention for multi-directional current flow.
    driving_force: str = "field"
    # M33-S1: which band-alignment gauge the heterojunction edge terms
    # use.  "nie" (default) is the PRE-M33 behaviour, kept bit-identical:
    # transport is parameterised by the effective intrinsic
    # concentration alone, which encodes Nc/Nv/Eg but NOT electron
    # affinity, so it splits a band offset symmetrically between Ec and
    # Ev.  "affinity" uses the physical band edges, so chi actually
    # reaches the equations.  See pytcad/M33-INTERFACE-PLAN.md sec 2 for
    # the measurement that found the gap (a 0.5 eV chi step moved the
    # solution by EXACTLY zero).
    band_offset: str = "nie"
    # M33-S2: thermionic-emission interface flux at an abrupt
    # heterointerface, replacing the drift-diffusion (Scharfetter-
    # Gummel) flux on material-change edges ONLY. Requires
    # band_offset="affinity": TE's entire content is the flux limit
    # imposed by dEc, so running it in a gauge that cannot represent
    # dEc would be calibrating a barrier the equations do not have.
    thermionic: bool = False
    # M14: surface recombination velocity at contacts [cm/s], Robin BC
    # Jn.n_hat = q*S_n*(n-n0), Jp.n_hat = q*S_p*(p-p0). S_n = S_p = 0
    # (default) => no surface recombination, bit-identical to the plain
    # Dirichlet contact (verified: (1.0 + 0.0) == 1.0 exactly, no
    # branching needed in the residual/Jacobian). Wired in Device1D and
    # Device2D; Device3D raises (never in the M14 plan's scope for this
    # feature -- see device3d.py's own guard, not this shared one).
    S_n: float = 0.0
    S_p: float = 0.0
    # M44: coupled electron energy balance (Tn), appended as a 4th DOF
    # block (indices 3*N..4*N-1) rather than reindexing the existing
    # 3*N psi/n/p system -- the base block's math and indices are
    # UNCHANGED by this flag (default False => bit-identical, verified
    # by reconstruct-and-compare, see M44-HYDRODYNAMIC-PLAN.md).
    # Electron-only (hole energy balance, Tp, is out of scope -- see
    # the plan's "Scope decision"). The new Tn row's own coefficients
    # that come from psi/n (the local field and current used in the
    # Joule-heating source and the Tn-dependent thermal conductivity)
    # are LAGGED one outer Newton iterate, exactly like
    # Models.field_mobility's own already-accepted mu_n/mu_p lag --
    # this keeps the Tn block's Jacobian an honest, FD-verified
    # TRIDIAGONAL-IN-THETA block with no fabricated cross-derivatives
    # into the psi/n/p columns (those columns are genuinely zero: the
    # lagged coefficients are plain numpy snapshots, not functions of
    # the current Newton iterate, so a full FD-Jacobian sweep over
    # psi/n/p/theta correctly finds no dependence there).
    energy_balance: bool = False
    # When energy_balance is on, its OWN mobility path (Tn -> effective
    # field via hydrodynamic.effective_field_from_temperature -> the
    # SAME materials.mobility_field Canali model) is used instead of
    # field_mobility's local-field mobility -- these are two different
    # sources of the same target quantity, mixing them isn't
    # physically meaningful. Set both flags with intent, not by
    # accident: energy_balance always wins, field_mobility is ignored,
    # not silently composed.

    def __post_init__(self):
        # driving_force is declared and documented as controlling real
        # physics but has no consumer: Canali/mobility_field() (the only
        # place a "driving force" argument exists) is unconditionally
        # NotImplementedError in Device2D/Device3D, and Device1D's plain
        # "field" convention is the only one implemented anywhere.
        # Refuse loudly rather than silently no-op, same as
        # impact/incomplete_ion do for a dimensionality that can't honor
        # them.
        if self.thermionic and self.band_offset != "affinity":
            raise ValueError(
                "Models(thermionic=True) requires band_offset='affinity'. "
                "Thermionic emission is a statement about the band "
                "discontinuity; the legacy 'nie' gauge cannot represent "
                "one (a chi step moves nothing there), so the barrier "
                "would be fictitious. Refusing rather than silently "
                "modelling a barrier of zero.")
        if self.band_offset not in ("nie", "affinity"):
            raise ValueError(
                f"Models.band_offset={self.band_offset!r} is not "
                "recognised -- use 'nie' (legacy symmetric-nie gauge) "
                "or 'affinity' (physical band edges). Refusing rather "
                "than silently picking one.")
        if self.driving_force != "field":
            raise NotImplementedError(
                f"Models.driving_force={self.driving_force!r} is not "
                "implemented -- only the default 'field' driving force "
                "is wired into the mobility model.")
        if self.energy_balance and (self.impact or self.btbt
                                    or self.btbt_nonlocal):
            raise NotImplementedError(
                "Models(energy_balance=True) combined with impact/btbt/"
                "btbt_nonlocal is not implemented in M44 Slice 1: those "
                "flags drive solve_bias's stiff-generation strength-"
                "ladder + backtracking line search, which does not yet "
                "account for the new Tn unknown (the line search's own "
                "merit function is evaluated on a 3*N-only residual "
                "call). Refusing rather than silently ignoring Tn's "
                "update inside that path.")


@dataclass
class NewtonOptions:
    max_iter: int = 100
    tol_update: float = 1e-8       # max scaled update
    tol_residual: float = 1e-10
    max_dpsi: float = 5.0          # damping cap on scaled potential update
    verbose: bool = False
    # M22: linear-solve method for the Newton update.  "direct" is
    # scipy spsolve, EXACTLY -- the default, bit-identical to every
    # pre-M22 solve (gated: tests/test_m22_linsolve.py G1).  "gmres" /
    # "bicgstab" precondition with ILU and are gated to agree with the
    # direct solution within linsolve_rtol (G3), never to return a
    # non-converged iterate silently (G4).
    #
    # M31 P5-1 Phase D: "auto" resolves to a concrete method via
    # linsolve.select_auto's evidence table (real Phase A measurements
    # only -- never a guess), independently at every call site that
    # supports it (every coupled solve_bias, plus device3d.py's
    # structured-3D and the two SCALAR unstructured equilibrium paths).
    # A (dim, unstructured, coupled) combination that has never been
    # measured resolves to "direct" (Gate D-3's explicit refusal, not a
    # default guess).
    #
    # 2026-09-13: default changed "direct" -> "auto" at the user's
    # explicit request (make the PETSc-backed C++ path the default
    # wherever the evidence table says it wins; unmeasured shapes keep
    # falling back to plain "direct" via Gate D-3). This breaks bit-
    # identity for any test that assumed NewtonOptions()'s default
    # equals scipy spsolve exactly (M22 G1's own documented guarantee) --
    # PETSc/iterative solves agree with "direct" only to linsolve_rtol,
    # not bit-for-bit. Any caller that still needs the old guarantee
    # must pass linsolve="direct" explicitly.
    linsolve: str = "auto"
    linsolve_rtol: float = 1e-10
    # M31 P5-1 Phase B: expose the preconditioner flavor and block size
    # that every COUPLED (psi/n/p-interleaved) Newton loop's
    # `solve_linear` call previously hardcoded (`block_size=3`, no
    # `precond`) -- see M31-P5-1-SOLVER-SELECTION-PLAN.md section 3.
    # Defaults reproduce that hardcoding EXACTLY (Gate B-1): a caller
    # that never sets these two fields gets bit-identical behavior to
    # every pre-Phase-B solve, on every fixture, ACCEL on or off.
    #
    # Only the coupled solves read this field -- the SCALAR
    # Poisson-equilibrium solves (Device1D/2D/3D's own
    # solve_equilibrium, unstructured_poisson.py,
    # unstructured_dd3d.py's equilibrium sub-solve, moscap.py) never
    # hardcoded a block_size (there is no psi/n/p interleaving to
    # block on) and continue not passing one, regardless of what this
    # field holds -- setting block_size here has NO EFFECT on those
    # solves. That asymmetry is deliberate, not an oversight: passing
    # block_size=3 into a one-unknown-per-node system would group three
    # unrelated nodes' potentials into a fake "block", which is simply
    # wrong, not merely unhelpful.
    precond: str = "auto"
    block_size: int | None = 3
    # OPT-IN SuperLU column ordering (scipy spsolve's permc_spec) for
    # STRUCTURED Device2D.solve_bias's direct Newton solve -- nothing
    # else reads it. None (default) is the exact pre-existing call, no
    # permc_spec passed. Measured end to end, 2026-09-25 (Windows, full
    # benchmark sizes), "MMD_AT_PLUS_A" vs the default COLAMD: B3 4.09s
    # -> 2.56s, B6 0.57s -> 0.41s -- but B10 (nonlocal BTBT) 13.3s ->
    # 49.4s and B8 (unstructured) 3.6s -> 870.6s. The best ordering
    # depends on the matrix structure, so this is never a default.
    direct_ordering: str | None = None

    def __post_init__(self):
        # Refuse an unreachable preconditioner flavor loudly rather than
        # silently ignoring it -- this project has already been bitten
        # once by a silently-ignored NewtonOptions.linsolve (the reason
        # M31 P5-0 exists: opts.linsolve reached nothing until the
        # unstructured cores were rewired to read it). Mirrors
        # linsolve.solve_linear's own `_PRECOND` contract exactly, so a
        # value this accepts can never be rejected one layer down.
        if self.precond not in ("auto", "block_jacobi", "schur"):
            raise ValueError(
                f"NewtonOptions.precond={self.precond!r} is not a known "
                "preconditioner flavor -- choose from 'auto', "
                "'block_jacobi', 'schur' (linsolve.solve_linear's own "
                "precond= contract).")
        if self.block_size is not None and (
                not isinstance(self.block_size, int) or self.block_size <= 0):
            raise ValueError(
                f"NewtonOptions.block_size={self.block_size!r} must be "
                "a positive int or None.")
        if self.direct_ordering is not None and self.direct_ordering not in (
                "COLAMD", "MMD_AT_PLUS_A", "MMD_ATA", "NATURAL"):
            raise ValueError(
                f"NewtonOptions.direct_ordering={self.direct_ordering!r} is "
                "not a SuperLU column ordering -- choose from None, "
                "'COLAMD', 'MMD_AT_PLUS_A', 'MMD_ATA', 'NATURAL'.")


@dataclass
class SchottkyContact:
    """M46-S1/S2: a metal-semiconductor (Schottky) contact spec for
    Device1D's `schottky_left`/`schottky_right` constructor params --
    couples pytcad.schottky's already-validated barrier-height physics
    (Sze & Ng ch. 3) into the device Newton core, per ARCHITECTURE.md's
    M46 scope note ("couple schottky.py into a device core first").

    phi_metal_eV : metal work function [eV] (Schottky-Mott rule input).
    A_star       : Richardson constant [A/(cm^2 K^2)]. `None` (the
                   default) selects M46-S1's DIRICHLET approximation
                   (below); a real value selects M46-S2's ROBIN
                   (thermionic-emission-limited) boundary condition,
                   both described below. Either way this is the
                   MAJORITY carrier's own effective Richardson
                   constant (see schottky.py's RICHARDSON_A_STAR_TABLE
                   for published per-material/per-carrier values) --
                   the caller's responsibility to pick the one that
                   matches whichever carrier this node's doping sign
                   makes majority, exactly as schottky_iv's own A_star
                   argument already requires.

    A_star=None (M46-S1, DIRICHLET approximation): the contact node's
    majority-carrier density is pinned at its barrier-limited
    equilibrium value (Nc or Nv times exp(-phi_B/kT)), referenced
    through the SAME psi0 = V/VT + ln(n0/nie) formula _contact_values
    already uses for an ohmic contact, just with a barrier-derived n0
    instead of a doping-derived one. Reproduces the correct built-in
    potential / depletion physics and the qualitative rectifying
    asymmetry a real Schottky junction shows, but not a finite
    interface recombination velocity.

    A_star given (M46-S2, ROBIN boundary condition): the majority
    carrier's Dirichlet row is REPLACED by a flux-balance equation,
    J_majority(edge) + v_R*(n_or_p(node) - n0_or_p0) = 0, with
    v_R = A* T^2 / (q * Nc_or_Nv) -- thermionic emission (Sze & Ng)
    restated as a surface recombination velocity, mirroring EXACTLY
    the equation shape M14's own Models(S_n=..., S_p=...) surface-
    recombination Robin BC already implements and gates (device.py's
    "Dirichlet contacts (Robin on n/p...)" block); n0/p0 is the SAME
    barrier-limited value the Dirichlet approximation above uses. The
    minority carrier stays Dirichlet at its mass-action value,
    unchanged. Refused in combination with a nonzero Models.S_n/S_p
    (both would compete for the same boundary row; S_n/S_p are global
    to both contacts in this module's own existing design)."""
    phi_metal_eV: float
    A_star: float = None


# ----------------------------------------------------------------------
#  Device
# ----------------------------------------------------------------------
class Device1D:
    """A 1D two-terminal semiconductor device with ohmic contacts.

    Parameters
    ----------
    x        : node positions [cm], ascending
    doping   : net doping N_D - N_A at each node [cm^-3] (positive = n-type)
    Ntotal   : total ionised impurity concentration for mobility/lifetime
               models [cm^-3]; defaults to |doping|
    """

    def __init__(self, x, doping, Ntotal=None, T=300.0,
                 material: Semiconductor = SILICON,
                 models: Models = None,
                 schottky_left: "SchottkyContact" = None,
                 schottky_right: "SchottkyContact" = None):
        self.schottky_left = schottky_left
        self.schottky_right = schottky_right
        self.x = np.asarray(x, dtype=float)
        self.N = self.x.size
        self.doping = np.asarray(doping, dtype=float)
        self.Ntot = np.abs(self.doping) if Ntotal is None else np.asarray(Ntotal, float)
        self.T = T
        # M11-S3: a single Semiconductor keeps the classic behavior; a
        # per-node sequence defines a heterostructure.  All material
        # fields below become node arrays in that case, and eps(x)
        # enters the Poisson flux form.
        #
        # M33 CORRECTION: this comment used to say "chi/Eg enter the
        # currents through position-dependent nie". That is true of Eg
        # and FALSE of chi. `nie` is sqrt(Nc*Nv)*exp(-Eg/2kT) and
        # contains no affinity at all, so under the default
        # band_offset="nie" gauge a step in chi changes the solution by
        # EXACTLY zero (measured: 0.000e+00 for a 0.5 eV step) and the
        # band offset actually solved is a symmetric split of dEg.
        # Set Models(band_offset="affinity") for the physical band
        # edges -- see M33-INTERFACE-PLAN.md section 2.
        if isinstance(material, Semiconductor):
            self.mats = [material] * len(np.atleast_1d(doping))
        else:
            self.mats = [m for m in material]
            if len(self.mats) != len(np.atleast_1d(doping)):
                raise ValueError(
                    "material list length must match the mesh")
            if not all(isinstance(m, Semiconductor) for m in self.mats):
                raise TypeError("material entries must be Semiconductor")
        self.mat = self.mats[0] if isinstance(material, Semiconductor) \
            else material
        self.models = models or Models()

        if self.Ntot.max() > 1e19 and not getattr(self.models, "fd", False):
            warnings.warn(
                "Doping exceeds ~1e19 cm^-3: Boltzmann statistics used here "
                "overestimate the carrier density. Treat results in the "
                "degenerate regions as qualitative."
            )

        # Unvalidated model compositions are refused up front (before any
        # evaluation), exactly as before the materials moved to C++.
        if self.models.band_offset == "affinity":
            if getattr(self.models, "fd", False) or                     getattr(self.models, "incomplete_ion", False):
                raise NotImplementedError(
                    "Models(band_offset='affinity') with fd/"
                    "incomplete_ion is refused: the FD eta-space "
                    "contact solver and the neutral-guess bisection "
                    "both carry their own ln(Nc/nie) offsets, and "
                    "composing them with the affinity shift has not "
                    "been derived or gated here. Refusing rather than "
                    "shipping an unvalidated composition (the M20 "
                    "dg+fd precedent).")
            if getattr(self.models, "dg", False):
                raise NotImplementedError(
                    "Models(band_offset='affinity', dg=True) is refused "
                    "(unvalidated composition).")
        if self.models.thermionic and getattr(self.models, "impact", False):
            # The impact source is built from the drift-diffusion edge
            # currents; composing a TE interface flux with it was not
            # derived or gated.
            raise NotImplementedError(
                "Models(thermionic=True, impact=True) is refused: the "
                "frozen impact-ionization source is built from the "
                "drift-diffusion edge currents and does not know about "
                "the thermionic interface flux (unvalidated "
                "composition).")
        # M34-S2: the nonlocal effective field only modifies the local
        # impact model's coefficients, so it needs that model on.
        if getattr(self.models, "impact_nonlocal", False):
            if not getattr(self.models, "impact", False):
                raise ValueError(
                    "Models(impact_nonlocal=True) modifies the impact-"
                    "ionization coefficients and needs Models(impact=True) "
                    "as well.")
            for lam in (self.models.impact_lambda_n,
                        self.models.impact_lambda_p):
                if not lam > 0.0:
                    raise ValueError(
                        f"impact_lambda_n/p must be > 0 cm, got {lam}")

        # Every derived input -- the scaling (Ns/LD/J0/R0), the scaled
        # geometry, the per-node material fields (nie with Slotboom BGN,
        # Caughey-Thomas mobilities, Scharfetter lifetimes, band-DOS
        # arrays), the M33 affinity band shift and thermionic edge
        # arrays, the edge diffusivities and the M44 constants -- is
        # evaluated by the compiled device (tcad/device1d/inputs.hpp;
        # gated bit-identical to materials.py, which Device2D/Device3D
        # still use). They land on the same attributes as always, so
        # callers may read -- or mutate -- them; a mutation rebuilds the
        # solver from them (_native_current).
        _accel.require_accel()
        ev = self._material_inputs()
        for k in ("VT", "ni", "Ns", "LD", "J0", "R0", "eps"):
            setattr(self, k, float(ev[k]))
        for k in ("eps_arr", "chi_arr", "Eg0_arr", "xs", "h", "dV", "C", "nie",
                  "mu_n0", "mu_p0", "tau_n", "tau_p", "nie_s", "nc_s", "nv_s",
                  "ln_gn", "ln_gp", "eg_kt", "band_shift", "nd_arr", "na_arr",
                  "dn_edge", "dp_edge"):
            setattr(self, k, np.asarray(ev[k], dtype=float))
        self._te_edge = np.asarray(ev["te_edge"], dtype=bool)
        if self.models.thermionic:
            for k in ("te_Kn", "te_Kp", "te_dlnNc", "te_dlnNv", "te_rNc", "te_rNv"):
                setattr(self, "_" + k, np.asarray(ev[k], dtype=float))
        self._ALPHA_RELAX = float(ev["alpha_relax"])
        self._KAPPA0 = float(ev["kappa0"])

        # M34-S1: nonlocal path BTBT is homojunction-only (S1 scope).
        if getattr(self.models, "btbt_nonlocal", False) and self._te_edge.any():
            raise NotImplementedError(
                "Models(btbt_nonlocal=True) is homojunction-only "
                "(M34-S1 scope) -- this device has a heterointerface. "
                "Refusing rather than silently applying a "
                "single-material formula across a hetero boundary.")
        self.Tn = None       # physical carrier temperature [K], None off

        self.psi = None
        self.n = None
        self.p = None
        # M15 frozen impact-ionization field/source (per bias solve;
        # cleared by solve_equilibrium -- no generation at V=0 gauge)
        self._ii_E = None
        self._ii_gs = None
        # M15: last generation source array _residual_jacobian actually
        # computed and integrated (live, fully-coupled -- see R1b fix
        # above), kept for introspection/tests.  None whenever
        # Models.impact is False; never read back into the residual.
        self._ii_gs_cache = None
        # M15 generation-strength continuation multiplier; see _II_STAGES.
        self._ii_strength = 1.0
        # M16: last BTBT generation source array [physical cm^-3 s^-1]
        # the residual actually integrated (live, fully-coupled like
        # the M15 R1b II source).  None whenever Models.btbt is False;
        # never read back into the residual.
        self._btbt_gs_cache = None
        # M34-S1: frozen nonlocal-BTBT tunnel windows -- located once
        # per solve_bias call (lazily, on first residual evaluation,
        # same None-cache cadence as `_Pn`/`_Pp` below), then held
        # fixed through that call's Newton iterations.  Reset to None
        # at the same points `_btbt_gs_cache` is reset.
        self._btbt_nl_paths = None
        # M12-S2 frozen-field WKB escape probabilities (None until the
        # first TAT-enabled residual evaluation freezes them)
        self._Pn = None
        self._Pp = None
        # M22 phase 2: convergence status of the last solve_bias call.
        self.last_converged = None
        # M34-S1: outcome of solve_bias's post-convergence path refresh
        self.last_btbt_nl_refreshes = 0
        self.last_btbt_nl_stable = None
        self.last_newton_err = None

        # Every solve runs in the compiled pytcad._core.Device1D (the
        # C++ port, ~/.claude/plans/eager-purring-fairy.md). Since
        # 2026-09-28 there is NO pure-Python solver: at the user's
        # explicit request the Python Newton loops, residual/Jacobian and
        # equilibrium solves were removed (the M43 phase-4 precedent), so
        # _core is REQUIRED for Device1D -- `import pytcad` still works
        # without it, constructing a Device1D raises ImportError with the
        # build command. The materials evaluation above is compiled too
        # (since 2026-09-29); _residual_jacobian, _contact_values and
        # friends below are thin calls into the compiled device, so
        # transient.py / continuation.py / ac.py run on the C++ assembly.
        self._native = None
        self._build_native()

    _MATERIAL_FIELDS = (
        "eps_r", "chi", "Eg0", "varshni_alpha", "varshni_beta", "Nc300", "Nv300",
        "mu_n_min", "mu_n_max", "mu_n_Nref", "mu_n_alpha", "mu_n_Texp",
        "mu_p_min", "mu_p_max", "mu_p_Nref", "mu_p_alpha", "mu_p_Texp",
        "vsat_n", "vsat_p", "beta_n", "beta_p", "tau_n0", "tau_p0", "tau_Nref",
        "Cn_auger", "Cp_auger", "bgn_E0", "bgn_N0", "m_n_star", "m_p_star")

    def _material_inputs(self, Ns=0.0, tat=None, btbt_nonlocal=None):
        """The compiled evaluation of every derived input (see __init__)
        for the CURRENT mesh/doping/T/materials/models. Nodes sharing one
        Semiconductor OBJECT share a material index (identity, as ever: an
        edge between different objects is a heterointerface). Ns > 0 keeps
        that scale instead of max(|doping|, ni); tat/btbt_nonlocal force those
        evaluations regardless of self.models."""
        core = _accel.core
        uniq, node_mat = [], []
        for m in self.mats:
            for k, u in enumerate(uniq):
                if u is m:
                    node_mat.append(k)
                    break
            else:
                uniq.append(m)
                node_mat.append(len(uniq) - 1)
        params = []
        for m in uniq:
            mp = core.MaterialParams()
            for f in self._MATERIAL_FIELDS:
                setattr(mp, f, float(getattr(m, f)))
            params.append(mp)
        sides = (self.schottky_left, self.schottky_right)
        return core.device1d_inputs(
            np.ascontiguousarray(self.x, dtype=float),
            np.ascontiguousarray(self.doping, dtype=float),
            np.ascontiguousarray(self.Ntot, dtype=float), float(self.T), params,
            node_mat, bool(self.models.bgn), bool(self.models.doping_mobility),
            self.models.band_offset == "affinity", bool(self.models.thermionic),
            bool(self.models.tat if tat is None else tat),
            float(self.models.trap_et_rel),
            bool(self.models.btbt_nonlocal if btbt_nonlocal is None else btbt_nonlocal),
            float(_hydro.TAU_W_N),
            bool(self.models.fd or self.models.incomplete_ion),
            [s is not None for s in sides],
            [0.0 if s is None else float(s.phi_metal_eV) for s in sides],
            float(Ns))

    def _build_native(self):
        """(Re)build the compiled device from the CURRENT Python inputs."""
        native_models = _accel.core.Device1DModels()
        for f in ("srh", "auger", "fd", "incomplete_ion", "tat", "impact", "btbt",
                  "impact_nonlocal", "btbt_nonlocal", "thermionic", "field_mobility",
                  "energy_balance", "dg"):
            setattr(native_models, f, bool(getattr(self.models, f)))
        # The model-dependent material quantities for the CURRENT models
        # (TAT exponent numerators, nonlocal-BTBT band parameters incl. the
        # u > 1 refusal, Schottky contact densities), compiled, at this
        # object's scale.
        ev = self._material_inputs(Ns=self.Ns)
        nl_params = None
        if self.models.impact_nonlocal or self.models.btbt_nonlocal:
            nl_params = _accel.core.Device1DNonlocalParams()
            nl_params.lambda_n_cm = self.models.impact_lambda_n
            nl_params.lambda_p_cm = self.models.impact_lambda_p
            if self.models.btbt_nonlocal:
                nl_params.Eg_eV = ev["nl_Eg_eV"]
                nl_params.Eg_J = ev["nl_Eg_J"]
                nl_params.mr_kg, nl_params.mc_kg, nl_params.mv_kg = (
                    ev["nl_mr"], ev["nl_mc"], ev["nl_mv"])
                nl_params.u = ev["nl_u"]
                nl_params.hbar = HBAR_CONST
                nl_params.q = _NL_Q
        tat_kn, tat_kp = ((np.asarray(ev["tat_kn"]), np.asarray(ev["tat_kp"]))
                          if self.models.tat else (None, None))
        # FD / incomplete ionization: the per-node band-DOS arrays above
        # plus pytcad.fermi's (module-cached) quadrature table; empty
        # arrays when neither flag is set (never read then).
        empty = np.empty(0)
        if self.models.fd or self.models.incomplete_ion:
            from . import fermi as _fermi
            fe, fg, fgp, fq, fqp = _fermi._table()
            ded_kt = ionized_dE_kt(self.T)
            fd_arrays = (self.nc_s, self.nv_s, self.ln_gn, self.ln_gp, self.eg_kt,
                         self.nd_arr, self.na_arr)
        else:
            fe = fg = fgp = fq = fqp = empty
            ded_kt = 0.0
            fd_arrays = (empty,) * 7
        self._native = _accel.core.Device1D(
            self.x, self.doping, self.T, self.VT, self.eps, self.ni,
            self.mu_n0, self.mu_p0, self.tau_n, self.tau_p, self.nie,
            self.mats[0].Cn_auger, self.mats[0].Cp_auger, native_models,
            *fd_arrays, ded_kt, fe, fg, fgp, fq, fqp,
            tat_kn=tat_kn, tat_kp=tat_kp, nl=nl_params, ex=self._native_extras(ev))
        # NewtonOptions(verbose=True) progress lines go through Python's
        # print, so sys.stdout redirection (the GUI's tee and live
        # progress tap) sees them, in the historical device.py formats.
        self._native.set_logger(print)
        # The compiled object holds COPIES of the Models() and of every
        # Python-side input; callers mutate those after construction
        # (test_m12_tat sets dev.models.tat, test_m11_hetero re-dopes
        # dev.doping/C, test_m33 scales dev._te_Kn), so _native_current()
        # rebuilds from the current attributes whenever either changed.
        self._native_models = replace(self.models)
        self._native_input_snapshot = self._native_inputs(copy=True)

    _NATIVE_INPUT_ATTRS = (
        "x", "doping", "Ntot", "C", "T", "VT", "Ns", "mu_n0", "mu_p0",
        "tau_n", "tau_p", "nie", "nie_s", "nc_s", "nv_s", "ln_gn", "ln_gp",
        "eg_kt", "nd_arr", "na_arr", "eps_arr", "band_shift", "_te_edge",
        "_te_Kn", "_te_Kp", "_te_dlnNc", "_te_dlnNv", "_te_rNc", "_te_rNv",
        "_ALPHA_RELAX", "_KAPPA0", "schottky_left", "schottky_right", "mats")

    def _native_inputs(self, copy=False):
        out = []
        for a in self._NATIVE_INPUT_ATTRS:
            v = getattr(self, a, None)
            if isinstance(v, np.ndarray):
                v = v.copy() if copy else v
            elif isinstance(v, list):
                v = list(v)                      # identity-compared entries
            elif v is not None and not np.isscalar(v):
                v = replace(v) if copy else v    # SchottkyContact dataclass
            out.append(v)
        return out

    def _native_inputs_unchanged(self):
        for old, new in zip(self._native_input_snapshot, self._native_inputs()):
            if isinstance(old, np.ndarray):
                if not (isinstance(new, np.ndarray) and old.shape == new.shape
                        and np.array_equal(old, new)):
                    return False
            elif isinstance(old, list):
                if not (isinstance(new, list) and len(old) == len(new)
                        and all(a is b for a, b in zip(old, new))):
                    return False
            elif old != new:
                return False
        return True

    # ------------------------------------------------------------------
    def _native_extras(self, ev):
        """Phase 2 slice 5 inputs of the native Device1D, all evaluated
        here from the SAME arrays/formulas the pure-Python path uses
        (materials stay Python): eps(x) edge weights, the M33-S1 band
        shift, per-node Auger coefficients, the M33-S2 thermionic edge
        arrays, M46 Schottky contact densities, the per-side scaled
        Robin velocities, and the field-mobility/M44/M20 constants."""
        ex = _accel.core.Device1DExtras()
        ex.Ns = float(self.Ns)   # keep this object's scaling (see Extras.Ns)
        ex.et = self._eps_tilde_edge().tolist()
        ex.band_shift = np.asarray(self.band_shift, dtype=float).tolist()
        ex.Cn_auger = [float(m.Cn_auger) for m in self.mats]
        ex.Cp_auger = [float(m.Cp_auger) for m in self.mats]
        if self.models.thermionic:
            ex.te_edge = self._te_edge.astype(float).tolist()
            ex.te_Kn = self._te_Kn.tolist()
            ex.te_Kp = self._te_Kp.tolist()
            ex.te_dlnNc = self._te_dlnNc.tolist()
            ex.te_dlnNv = self._te_dlnNv.tolist()
            ex.te_rNc = self._te_rNc.tolist()
            ex.te_rNv = self._te_rNv.tolist()
        # M46-S1 Schottky contacts: barrier-limited (n0, p0), evaluated by
        # the compiled material inputs (FD/incomplete-ion contacts ignore
        # Schottky specs, the historical behaviour, kept).
        for k in (0, 1):
            if ev["contact_override"][k]:
                ex.set_contact_override(k, float(ev["contact_n0"][k]),
                                        float(ev["contact_p0"][k]))
        sides = (self.schottky_left, self.schottky_right)
        # M14 S_n/S_p and M46-S2 Robin Schottky velocities per side,
        # resolved exactly as _residual_jacobian's contact loop resolves
        # them (each side from the global S_n/S_p, independently).
        S_n_s = self.models.S_n * self.LD / D0_REF
        S_p_s = self.models.S_p * self.LD / D0_REF
        for k, node in enumerate((0, self.N - 1)):
            S_n_loc, S_p_loc = S_n_s, S_p_s
            sch = sides[k]
            if sch is not None and sch.A_star is not None:
                Nc_or_Nv = (self.mats[node].Nc(self.T) if self.C[node] >= 0.0
                            else self.mats[node].Nv(self.T))
                v_R_s = (sch.A_star * self.T * self.T / (Q * Nc_or_Nv)) \
                    * self.LD / D0_REF
                if self.C[node] >= 0.0:
                    S_n_loc = v_R_s
                else:
                    S_p_loc = v_R_s
            ex.set_surface_velocity(k, float(S_n_loc), float(S_p_loc))
        if (self.models.field_mobility or self.models.energy_balance):
            # single-material Canali parameters (solve_bias refuses these
            # flags on a heterostructure before they are ever used)
            m0 = self.mats[0]
            ex.vsat_n, ex.beta_n = m0.vsat_n, m0.beta_n
            ex.vsat_p, ex.beta_p = m0.vsat_p, m0.beta_p
        if self.models.energy_balance:
            ex.alpha_relax = self._ALPHA_RELAX
            ex.kappa0 = self._KAPPA0
            ex.tau_w = _hydro.TAU_W_N
            ex.kB = _hydro.KB
            ex.q_hydro = _hydro.Q
        if self.models.dg:
            from . import dg as _dg
            ex.m_n_star = [float(m.m_n_star) for m in self.mats]
            ex.m_p_star = [float(m.m_p_star) for m in self.mats]
            ex.dg_gamma = float(getattr(self.models, "dg_gamma", 1.0))
            ex.dg_hbar, ex.dg_m0, ex.dg_q = _dg.HBAR, _dg.M0, _dg.Q
        return ex

    def _eps_tilde_edge(self):
        """Harmonic-mean scaled permittivity on edges, normalized by the
        FIRST material's eps so a uniform device gives exactly 1.0
        everywhere and every residual reduces to its original form."""
        et_n = self.eps_arr / self.eps_arr[0]
        return 2.0 * et_n[:-1] * et_n[1:] / (et_n[:-1] + et_n[1:])

    def _set_edge_diffusivity(self, mu_n, mu_p):
        """Einstein relation D = mu V_T; harmonic mean onto the interfaces.

        Harmonic averaging (rather than arithmetic) is the right choice for a
        flux-continuous quantity across an abrupt change in mobility.
        """
        def hmean(a):
            return 2.0 * a[:-1] * a[1:] / (a[:-1] + a[1:])
        self.dn_edge = hmean(mu_n) * self.VT / D0_REF
        self.dp_edge = hmean(mu_p) * self.VT / D0_REF

    # ------------------------------------------------------------------
    #  M13 Fermi-Dirac core helpers
    # ------------------------------------------------------------------
    def _fd_factors(self, n, p):
        """nu-factor SG quantities per node (plan section 3.2bis) -- the
        module-level fd_node_factors (shared with Device2D/Device3D) at
        this device's band-DOS arrays."""
        return fd_node_factors(self.nc_s, self.nv_s, n, p)

    # ------------------------------------------------------------------
    #  Thin calls into the compiled device (pytcad._core.Device1D)
    # ------------------------------------------------------------------
    def _native_current(self):
        """The compiled device, rebuilt first if self.models or any
        Python-side input changed since it was built (see _build_native)."""
        if (self.models != self._native_models
                or not self._native_inputs_unchanged()):
            self._build_native()
        return self._native

    def _contact_values(self, V):
        """[(psi0, n0, p0) left, (psi0, n0, p0) right] at bias V (volts):
        ohmic local neutrality (Boltzmann closed form, or the FD eta-space
        root under fd/incomplete_ion), M46 Schottky barrier-limited
        densities, psi0 referenced through the M33-S1 band shift."""
        left, right = self._native_current().contact_values(float(V[0]), float(V[1]))
        return [tuple(left), tuple(right)]

    def _update_tat_probabilities(self, psi=None):
        """M12-S2 frozen-field WKB escape probabilities P_n/P_p from the
        node fields of psi (self.psi, else zeros); frozen per solve_bias."""
        if psi is None:
            psi = self.psi if self.psi is not None else np.zeros(self.N)
        Pn, Pp = self._native_current().tat_probabilities(
            np.ascontiguousarray(psi, dtype=float))
        self._Pn, self._Pp = np.asarray(Pn), np.asarray(Pp)

    def _tat_exponent_coeffs(self):
        """psi-independent WKB exponent numerators B_n phi_n^1.5 and
        B_p phi_p^1.5 (SI, V/m), B = 4 sqrt(2 m* m0 q)/(3 hbar) -- the
        compiled material evaluation (tcad/device1d/inputs.cpp)."""
        ev = self._material_inputs(Ns=self.Ns, tat=True)
        return np.asarray(ev["tat_kn"]), np.asarray(ev["tat_kp"])

    def _ii_compute_E_from_state(self, psi):
        """Node-centred |E| [V/cm] from psi: mean of the two adjacent
        edge fields (the boundary edge's at the ends) -- the field the
        local impact/BTBT models are evaluated at."""
        c_edge = self.VT / (self.LD * self.h)
        e_mag = np.abs(np.diff(psi)) * c_edge
        E_node = np.empty(self.N)
        E_node[0], E_node[-1] = e_mag[0], e_mag[-1]
        E_node[1:-1] = 0.5 * (e_mag[:-1] + e_mag[1:])
        return E_node

    def _ii_compute_gs_frozen(self, psi, n, p, alpha_n, alpha_p):
        """Impact generation source Kgen*(alpha_n*Sn + alpha_p*Sp) [scaled]
        for GIVEN coefficient arrays, Sn/Sp the node-centred sums of the
        smoothed incident-edge |J| of the compiled device's own edge
        currents at (psi, n, p). No state is touched."""
        bc = self._contact_values([0.0, 0.0])
        out = self._assemble(psi, n, p, bc, self._frozen_inputs(psi, touch=False))
        Jn, Jp = np.asarray(out[4]), np.asarray(out[5])
        j_eps = _II_J_EPS_REL * max(float(np.abs(Jn).max()),
                                    float(np.abs(Jp).max()), 1e-300)
        aJn = _ii_smooth_abs(Jn, j_eps) * self.J0
        aJp = _ii_smooth_abs(Jp, j_eps) * self.J0
        Sn = np.empty(self.N); Sn[0], Sn[-1] = aJn[0], aJn[-1]
        Sn[1:-1] = aJn[:-1] + aJn[1:]
        Sp = np.empty(self.N); Sp[0], Sp[-1] = aJp[0], aJp[-1]
        Sp[1:-1] = aJp[:-1] + aJp[1:]
        return 0.5 / (_II_Q * self.R0) * (alpha_n * Sn + alpha_p * Sp)

    def _btbt_nl_params(self):
        """M34-S1: (Eg [J], mr, mc, mv [kg]) of the (homojunction)
        material -- the compiled material evaluation."""
        ev = self._material_inputs(Ns=self.Ns, btbt_nonlocal=True)
        return ev["nl_Eg_J"], ev["nl_mr"], ev["nl_mc"], ev["nl_mv"]

    def _btbt_nl_build_paths(self, psi):
        """M34-S1: the nonlocal-BTBT tunnel paths located at psi (the
        compiled locator), as nonlocal_path.build_1d's TunnelPaths."""
        starts, ends = self._native_current().locate_btbt_nl_paths(
            np.ascontiguousarray(psi, dtype=float))
        return _nl_build_1d(self.x * 1e-2, np.asarray(starts, dtype=int),
                            np.asarray(ends, dtype=int))

    def _btbt_nl_eval(self, psi):
        """M34-S1: evaluate the frozen paths at a live psi (compiled
        nonlocal_path.evaluate)."""
        Eg_J, mr, mc, mv = self._btbt_nl_params()
        return _nl_evaluate(self._btbt_nl_paths, psi, self.VT, Eg_J, mr, mc, mv)

    @staticmethod
    def _path_spans(paths):
        """(starts, ends) of a 1D TunnelPaths (every stencil has K = 1)."""
        if paths is None or paths.n_paths == 0:
            return [], []
        last = np.asarray(paths.offset[1:]) - 1
        return ([int(v) for v in paths.start],
                [int(v) for v in np.asarray(paths.sidx)[last, 0]])

    def _frozen_inputs(self, psi, touch=True):
        """(Pn, Pp, starts, ends) the residual is evaluated with -- the
        frozen TAT probabilities and nonlocal paths, filled lazily from
        psi when absent (touch=False computes without storing them)."""
        Pn = Pp = None
        if self.models.tat:
            if self._Pn is None or self._Pp is None:
                Pn, Pp = self._native_current().tat_probabilities(
                    np.ascontiguousarray(psi, dtype=float))
                Pn, Pp = np.asarray(Pn), np.asarray(Pp)
                if touch:
                    self._Pn, self._Pp = Pn, Pp
            else:
                Pn, Pp = self._Pn, self._Pp
        elif touch:
            self._Pn = self._Pp = None
        starts, ends = [], []
        if self.models.btbt_nonlocal:
            paths = self._btbt_nl_paths
            if paths is None:
                paths = self._btbt_nl_build_paths(psi)
                if touch:
                    self._btbt_nl_paths = paths
            starts, ends = self._path_spans(paths)
        return Pn, Pp, starts, ends

    def _assemble(self, psi, n, p, bc, frozen, theta=None, n_lag=None,
                  Jn_lag=None, Qheat_lag=None):
        nat = self._native_current()
        # dn_edge/dp_edge are the source of truth (field mobility / M44
        # overwrite them per Newton iterate; callers may set them).
        nat.set_edge_diffusivity(np.ascontiguousarray(self.dn_edge, dtype=float),
                                 np.ascontiguousarray(self.dp_edge, dtype=float))
        Pn, Pp, starts, ends = frozen
        f64 = lambda a: None if a is None else np.ascontiguousarray(a, dtype=float)
        return nat.assemble(
            f64(psi), f64(n), f64(p), [float(v) for v in bc[0]],
            [float(v) for v in bc[1]], f64(Pn), f64(Pp),
            float(getattr(self, "_ii_strength", 1.0)), starts, ends,
            f64(theta), f64(n_lag), f64(Jn_lag), f64(Qheat_lag))

    def _residual_jacobian(self, psi, n, p, bc, theta=None, n_lag=None,
                           Jn_lag=None, Qheat_lag=None):
        """(F, J csr, Jn, Jp) of the coupled psi/n/p system (plus the M44
        theta block when theta is given, with its lagged inputs) at an
        arbitrary state and contact values bc -- assembled by the compiled
        device. Also records _dirichlet_rows and the strength-scaled
        generation sources (_ii_gs_cache / _btbt_gs_cache) exactly as the
        old Python assembly did; the frozen TAT probabilities and nonlocal
        paths are filled lazily from psi when absent."""
        if any(s is not None and s.A_star is not None
               for s in (self.schottky_left, self.schottky_right)) and (
                self.models.S_n != 0.0 or self.models.S_p != 0.0):
            raise NotImplementedError(
                "Models(S_n!=0 or S_p!=0) combined with a Robin-mode "
                "SchottkyContact (A_star given) is refused: both "
                "mechanisms would compete for the same boundary row "
                "(M46-S2 scope; unvalidated composition).")
        F, rows, cols, vals, Jn, Jp, drows, ii_gs, btbt_gs = self._assemble(
            psi, n, p, bc, self._frozen_inputs(psi), theta, n_lag, Jn_lag, Qheat_lag)
        F = np.asarray(F)
        M = F.size
        J = csr_matrix((np.asarray(vals), (np.asarray(rows), np.asarray(cols))),
                       shape=(M, M))
        self._dirichlet_rows = np.asarray(drows, dtype=int)
        self._ii_gs_cache = np.asarray(ii_gs) if self.models.impact else None
        self._btbt_gs_cache = np.asarray(btbt_gs) if self.models.btbt else None
        return F, J, np.asarray(Jn), np.asarray(Jp)

    def _dg_residual_jacobian_eq(self, psi, Lam_n, Lam_p, bc, gamma=None):
        """M20 coupled (psi, Lambda_n, Lambda_p) equilibrium residual and
        Jacobian (interleaved per node) -- (F [3N], J [3N x 3N] csr)."""
        gamma = getattr(self.models, "dg_gamma", 1.0) if gamma is None else gamma
        f64 = lambda a: np.ascontiguousarray(a, dtype=float)
        F, rows, cols, vals = self._native_current().dg_assemble(
            f64(psi), f64(Lam_n), f64(Lam_p), [float(v) for v in bc[0]],
            [float(v) for v in bc[1]], float(gamma))
        N = self.N
        self._dg_dirichlet_rows_eq = np.array(
            [0, 3 * (N - 1), 1, 2, 3 * (N - 1) + 1, 3 * (N - 1) + 2], dtype=int)
        return np.asarray(F), csr_matrix(
            (np.asarray(vals), (np.asarray(rows), np.asarray(cols))), shape=(3 * N, 3 * N))

    # ------------------------------------------------------------------
    #  Solves
    # ------------------------------------------------------------------
    @staticmethod
    def _native_newton_options(opts: NewtonOptions):
        """The NewtonOptions fields the compiled solver reads. linsolve/
        precond/block_size/direct_ordering have no counterpart: 1D always
        solves with a direct Eigen::SparseLU (see solve_bias)."""
        native_opts = _accel.core.Device1DNewtonOptions()
        native_opts.max_iter = opts.max_iter
        native_opts.tol_update = opts.tol_update
        native_opts.max_dpsi = opts.max_dpsi
        native_opts.verbose = opts.verbose
        return native_opts

    def _sync_from_native(self):
        nat = self._native
        self.psi = np.asarray(nat.psi)
        self.n = np.asarray(nat.n)
        self.p = np.asarray(nat.p)
        self.last_converged = nat.last_converged
        self.last_newton_err = nat.last_newton_err
        if self.models.tat and len(nat.Pn):
            self._Pn = np.asarray(nat.Pn)
            self._Pp = np.asarray(nat.Pp)
        # field_mobility / energy_balance leave the last iterate's lagged
        # edge diffusivities behind (transient/AC assemble with them), and
        # M44 the carrier temperature.
        if self.models.field_mobility or self.models.energy_balance:
            self.dn_edge = np.asarray(nat.dn_edge)
            self.dp_edge = np.asarray(nat.dp_edge)
        if self.models.energy_balance:
            Tn = np.asarray(nat.Tn)
            self.Tn = Tn if Tn.size else None

    def solve_equilibrium(self, opts: NewtonOptions = None):
        """Zero-bias Poisson solve with the carriers slaved to psi (the
        M20 coupled (psi, Lambda_n, Lambda_p) density-gradient solve when
        Models.dg). Clears every per-bias cache; with energy_balance sets
        Tn = T (zero current => theta == 1 exactly)."""
        opts = opts or NewtonOptions()
        dg = getattr(self.models, "dg", False)
        if dg and getattr(self.models, "fd", False):
            raise NotImplementedError(
                "Models(dg=True, fd=True) is refused: the DG correction "
                "and FD statistics compose through a joint density law "
                "that has not been derived/validated here "
                "(M20-DENSITY-GRADIENT-PLAN.md sec 5).")
        if dg and getattr(self.models, "incomplete_ion", False):
            raise NotImplementedError(
                "Models(dg=True, incomplete_ion=True) is refused "
                "(unvalidated composition; M20 plan sec 5).")
        nat = self._native_current()
        nat.set_edge_diffusivity(np.ascontiguousarray(self.dn_edge, dtype=float),
                                 np.ascontiguousarray(self.dp_edge, dtype=float))
        nat.solve_equilibrium(self._native_newton_options(opts))
        self._sync_from_native()
        self._ii_gs = None
        self._ii_gs_cache = None
        self._btbt_gs_cache = None
        self._btbt_nl_paths = None
        if dg:
            self._dg_Lam_n = np.asarray(nat.dg_Lam_n)
            self._dg_Lam_p = np.asarray(nat.dg_Lam_p)
        else:
            self._dg_Lam_n = None
            self._dg_Lam_p = None
        if self.models.energy_balance:
            self.Tn = np.full(self.N, self.T)
        if not nat.last_eq_converged:
            warnings.warn(
                "M20 DG equilibrium coupled-Newton solve did not converge "
                "(gamma continuation stalled)." if dg
                else "Equilibrium Poisson solve did not converge.")
        return self

    def solve_bias(self, V, opts: NewtonOptions = None):
        """Solve at applied bias V = [V_left, V_right] (volts), warm-
        started from self.psi/n/p (and self.Tn with energy_balance). Stiff
        generation (impact/btbt/btbt_nonlocal) runs the strength ladder
        with backtracking; btbt_nonlocal re-locates its frozen paths after
        convergence. Convergence is reported in last_converged /
        last_newton_err (and a warning), not raised."""
        opts = opts or NewtonOptions()
        # M20: DG is EQUILIBRIUM-ONLY (DG transport is out of scope).
        if getattr(self.models, "dg", False):
            raise NotImplementedError(
                "Models(dg=True) is equilibrium-only in M20: solve_bias "
                "would need DG inside the Scharfetter-Gummel currents "
                "(DG transport), which is out of scope.  Use the "
                "MOSCapacitor(dg=True) C-V path for the quantum-corrected "
                "inversion layer.")
        if (self.models.field_mobility or self.models.energy_balance) and \
                not isinstance(self.mat, Semiconductor):
            raise NotImplementedError(
                "Models(field_mobility/energy_balance) on a heterostructure "
                "is not implemented: the Canali velocity-saturation "
                "parameters are defined per single material.")
        if any(s is not None and s.A_star is not None
               for s in (self.schottky_left, self.schottky_right)) and (
                self.models.S_n != 0.0 or self.models.S_p != 0.0):
            raise NotImplementedError(
                "Models(S_n!=0 or S_p!=0) combined with a Robin-mode "
                "SchottkyContact (A_star given) is refused: both "
                "mechanisms would compete for the same boundary row "
                "(M46-S2 scope; unvalidated composition).")
        # 1D always solves with a direct LU (auto -> direct was already
        # the measured choice: iterative methods were ~300x slower or did
        # not converge on 1D coupled Jacobians). An explicit iterative
        # request is honoured as direct and SAID so in the run record.
        if opts.linsolve == "auto":
            self.last_auto_method = "direct"
            self.last_auto_reason = ("1D coupled solves always use the "
                                     "compiled direct LU (Eigen::SparseLU)")
        elif opts.linsolve != "direct" or opts.precond is not None or \
                opts.block_size is not None or opts.direct_ordering is not None:
            self.last_auto_method = "direct"
            self.last_auto_reason = (
                f"requested linsolve={opts.linsolve!r} (precond="
                f"{opts.precond!r}, block_size={opts.block_size!r}, "
                f"direct_ordering={opts.direct_ordering!r}) is not "
                "available for Device1D; solved with the compiled direct LU")
        else:
            self.last_auto_method = None
            self.last_auto_reason = None
        if opts.verbose and self.last_auto_reason:
            print(f"    solve_bias  {opts.linsolve} -> direct "
                  f"({self.last_auto_reason})")
        if self.psi is None:
            self.solve_equilibrium(opts)
        nat = self._native_current()
        # The Python attributes are the warm start's source of truth:
        # continuation.py (and any caller) may have reassigned
        # dev.psi/n/p/Tn/dn_edge since the last solve.
        nat.set_state(np.ascontiguousarray(self.psi, dtype=float),
                      np.ascontiguousarray(self.n, dtype=float),
                      np.ascontiguousarray(self.p, dtype=float))
        nat.set_edge_diffusivity(np.ascontiguousarray(self.dn_edge, dtype=float),
                                 np.ascontiguousarray(self.dp_edge, dtype=float))
        if self.models.energy_balance:
            nat.set_Tn(None if self.Tn is None
                       else np.ascontiguousarray(self.Tn, dtype=float))
        nat.solve_bias(float(V[0]), float(V[1]), self._native_newton_options(opts))
        self._sync_from_native()
        # What the final residual evaluation leaves behind: the ladder
        # stage the solve ended on, strength-scaled sources (None when the
        # flag is off), the frozen nonlocal paths and refresh outcome.
        self._ii_strength = nat.ii_strength
        self._ii_gs_cache = (np.asarray(nat.ii_gs_cache)
                             if self.models.impact else None)
        self._btbt_gs_cache = (np.asarray(nat.btbt_gs_cache)
                               if self.models.btbt else None)
        self.last_btbt_nl_refreshes = nat.last_btbt_nl_refreshes
        self.last_btbt_nl_stable = nat.last_btbt_nl_stable
        self._btbt_nl_paths = (
            _nl_build_1d(self.x * 1e-2,
                         np.asarray(nat.btbt_nl_starts, dtype=int),
                         np.asarray(nat.btbt_nl_ends, dtype=int))
            if self.models.btbt_nonlocal else None)
        if not self.last_converged:
            warnings.warn(f"Newton did not converge at V={V}; "
                          f"last update {self.last_newton_err:.2e}")
        self.Jn = np.asarray(nat.Jn)
        self.Jp = np.asarray(nat.Jp)
        return self

    # --- physical-unit accessors -------------------------------------
    @property
    def psi_V(self):
        """Electrostatic potential [V]."""
        return self.psi * self.VT

    @property
    def n_cm3(self):
        """Electron density [cm^-3]."""
        return self.n * self.Ns

    @property
    def p_cm3(self):
        """Hole density [cm^-3]."""
        return self.p * self.Ns

    @property
    def E_field(self):
        """Electric field on the mesh interfaces [V/cm]."""
        return -(self.psi[1:] - self.psi[:-1]) * self.VT / (self.h * self.LD)

    # ------------------------------------------------------------------
    def current_density(self):
        """Total current density [A/cm^2].

        In 1D steady state Jn + Jp is exactly constant; the spread across
        interfaces is a useful convergence diagnostic and is returned too.
        """
        # No native branch: self.Jn/self.Jp are the source of truth on
        # every path (the native solve syncs them; continuation.py's
        # arc-length corrector writes them directly without any solve --
        # reading the compiled object's cache here returned the LAST
        # native solve's current for every arc-length record).
        Jt = self.Jn + self.Jp
        return float(np.mean(Jt)), float(np.std(Jt) / (np.abs(np.mean(Jt)) + 1e-30))

    def iv_sweep(self, voltages, terminal=0, opts: NewtonOptions = None,
                 verbose=True):
        """Ramp bias and record J(V).  The previous solution seeds the next
        bias point -- essential for convergence beyond a few hundred mV."""
        opts = opts or NewtonOptions()
        self.solve_equilibrium(opts)
        J = []
        for V in voltages:
            bias = [V, 0.0] if terminal == 0 else [0.0, V]
            self.solve_bias(bias, opts)
            j, spread = self.current_density()
            J.append(j)
            if verbose:
                print(f"  V = {V:+.3f} V   J = {j:+.6e} A/cm^2   "
                      f"(continuity spread {spread:.1e})")
        return np.array(J)

    # ------------------------------------------------------------------
    def band_diagram(self):
        """Conduction/valence band edges and quasi-Fermi levels [eV]."""
        VT = self.VT
        chi_arr = np.array([m.chi for m in self.mats])
        Eg_arr = np.array([m.Eg(self.T) for m in self.mats])
        Ec = -self.psi * VT - chi_arr
        Ev = Ec - Eg_arr
        Nc_arr = np.array([m.Nc(self.T) for m in self.mats])
        Nv_arr = np.array([m.Nv(self.T) for m in self.mats])
        if getattr(self.models, "fd", False):
            # M13: physical-statistics quasi-Fermi levels -- a Boltzmann
            # log would misplace E_F by many kT in degenerate regions.
            en = f_half_inv(np.maximum(self.n_cm3, 1e-300) / Nc_arr)
            ep = f_half_inv(np.maximum(self.p_cm3, 1e-300) / Nv_arr)
            EFn = Ec + KB_EV * self.T * en
            EFp = Ev - KB_EV * self.T * ep
            return Ec, Ev, EFn, EFp
        EFn = Ec + VT * np.log(np.maximum(self.n * self.Ns, 1e-30)
                               / Nc_arr)
        EFp = Ev - VT * np.log(np.maximum(self.p * self.Ns, 1e-30)
                               / Nv_arr)
        return Ec, Ev, EFn, EFp


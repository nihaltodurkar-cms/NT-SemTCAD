"""M15 impact ionization: the |J| regularizer must sit above each edge
current's own round-off.

On the depletion edge of the n+ side of a reverse-biased diode the SG
electron flux Jn = a (n_R B+ - n_L B-) is the difference of two terms
~1e14-1e15 times larger than the result: the computed Jn is pure
round-off.  With only the global regularizer (1e-6 x the largest edge
current, itself round-off sized) the smoothed sign of such an edge
current was round-off's sign, so dG/dn there flipped (+0.09 -> -0.09)
under a 1e-14 relative change of n and differed between the container
and the Windows (MinGW) build at the SAME state.  Near breakdown that
decided whether the arc-length corrector accepted a step: Windows
traced past the M15 G-D fold (41.6 V vs 35.7 V).
"""
import os
import sys
import warnings

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_m15_ionization import _diode, _ramp  # noqa: E402
from pytcad.ionization import alpha_n, alpha_p  # noqa: E402


def _state_at_20v():
    dev = _diode(impact=True, na=2e16)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        dev.solve_equilibrium()
        _ramp(dev, np.arange(2.0, 21.0, 2.0))
    assert dev.last_converged
    return dev, dev._contact_values([-20.0, 0.0])


def _noisy_n(dev, seed=0):
    """n perturbed at round-off scale (1e-14 relative)."""
    rng = np.random.default_rng(seed)
    return dev.n * (1.0 + 1e-14 * rng.standard_normal(dev.N))


def test_round_off_edges_exist_in_this_state():
    """Precondition: the state really has edges whose Jn is round-off
    (otherwise the two gates below would pass vacuously)."""
    dev, bc = _state_at_20v()
    _, _, Jn, _ = dev._residual_jacobian(dev.psi, dev.n, dev.p, bc)
    _, _, Jn2, _ = dev._residual_jacobian(dev.psi, _noisy_n(dev), dev.p, bc)
    # a 1e-14 relative input change moves some edge current by more
    # than its own magnitude: its value is noise
    assert np.any(np.abs(Jn2 - Jn) > np.abs(Jn) + 1e-300)


def test_ii_jacobian_does_not_follow_round_off():
    dev, bc = _state_at_20v()
    _, J, _, _ = dev._residual_jacobian(dev.psi, dev.n, dev.p, bc)
    _, J2, _, _ = dev._residual_jacobian(dev.psi, _noisy_n(dev), dev.p, bc)
    jmax = abs(J).max()
    worst = abs(J2 - J).max()
    # a regularized |J| cannot be exactly flat in round-off (its slope
    # is J / sqrt(J^2 + eps^2)); 1e-6 of the Jacobian's scale is far
    # below anything a Newton step or its convergence test resolves
    assert worst <= 1e-6 * jmax, (
        f"a 1e-14 relative change of n moved a Jacobian entry by {worst:.3e} "
        f"(|J|max {jmax:.3e}): the II derivative follows round-off")


def test_ii_generation_does_not_follow_round_off():
    dev, bc = _state_at_20v()
    dev._residual_jacobian(dev.psi, dev.n, dev.p, bc)
    gs = dev._ii_gs_cache.copy()
    dev._residual_jacobian(dev.psi, _noisy_n(dev), dev.p, bc)
    gs2 = dev._ii_gs_cache
    # Before the per-edge floor this change was 8.5x the source's OWN
    # maximum (the junction's generation was round-off). What remains
    # (~2e-3) is the genuine round-off of partly resolved edge currents,
    # which any |J|-driven generation inherits.
    assert np.abs(gs2 - gs).max() <= 1e-2 * np.abs(gs).max()


def test_frozen_helper_matches_the_assembled_source():
    """_ii_compute_gs_frozen (the Python helper M34-S2's tests use) must
    regularize |J| exactly as the compiled assembly does."""
    dev, bc = _state_at_20v()
    dev._residual_jacobian(dev.psi, dev.n, dev.p, bc)
    E = dev._ii_compute_E_from_state(dev.psi)
    gs = dev._ii_compute_gs_frozen(dev.psi, dev.n, dev.p, alpha_n(E), alpha_p(E))
    # the strength-1 assembled source at the same state (the helper uses
    # zero-bias contact values, which only touch the Dirichlet rows)
    ref = dev._ii_gs_cache
    np.testing.assert_allclose(gs[1:-1], ref[1:-1], rtol=1e-12, atol=0.0)

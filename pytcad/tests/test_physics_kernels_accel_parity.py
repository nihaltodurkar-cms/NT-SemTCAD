"""Compiled physics kernels (_core.bernoulli/dbernoulli/
recombination_boltzmann, core/src/physics/kernels.cpp) vs their Python
oracles -- pytcad.kernels.bernoulli/dbernoulli and
materials.recombination() with np_eq=None -- np.array_equal, not just
"close". First slice of the Device1D native-port plan
(~/.claude/plans/eager-purring-fairy.md Phase 0)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from pytcad import _accel
from pytcad.kernels import bernoulli as bernoulli_py, dbernoulli as dbernoulli_py
from pytcad.materials import SILICON, recombination

pytestmark = pytest.mark.skipif(not _accel.HAVE_ACCEL,
                                reason="pytcad._core not built")


@pytest.mark.parametrize("scale", [1e-6, 1e-3, 1.0, 10.0, 100.0, 800.0])
def test_bernoulli_bit_identical_to_oracle(scale):
    rng = np.random.default_rng(int(scale * 1000) + 1)
    x = rng.uniform(-1.0, 1.0, 4001) * scale
    # exercise the exact small-x branch boundary (|x| < 1e-4) explicitly
    x = np.concatenate([x, [1e-4, -1e-4, 0.0, 9.999e-5, -9.999e-5]])
    ref = bernoulli_py(x)
    got = np.asarray(_accel.core.bernoulli(x))
    assert got.shape == ref.shape
    assert np.array_equal(got, ref), np.flatnonzero(got != ref)[:5]


@pytest.mark.parametrize("scale", [1e-6, 1e-3, 1.0, 10.0, 100.0, 800.0])
def test_dbernoulli_bit_identical_to_oracle(scale):
    rng = np.random.default_rng(int(scale * 1000) + 2)
    x = rng.uniform(-1.0, 1.0, 4001) * scale
    x = np.concatenate([x, [1e-4, -1e-4, 0.0, 9.999e-5, -9.999e-5]])
    ref = dbernoulli_py(x)
    got = np.asarray(_accel.core.dbernoulli(x))
    assert got.shape == ref.shape
    assert np.array_equal(got, ref), np.flatnonzero(got != ref)[:5]


@pytest.mark.parametrize("auger", [True, False])
def test_recombination_boltzmann_bit_identical_to_oracle(auger):
    rng = np.random.default_rng(3 if auger else 4)
    N = 500
    n = rng.lognormal(0, 10, N)
    p = rng.lognormal(0, 10, N)
    nie = rng.uniform(1e3, 1e11, N)
    tau_n = rng.uniform(1e-9, 1e-5, N)
    tau_p = rng.uniform(1e-9, 1e-5, N)
    mat = SILICON
    ref_R, ref_dRdn, ref_dRdp = recombination(n, p, nie, tau_n, tau_p, mat, auger=auger)
    got_R, got_dRdn, got_dRdp = _accel.core.recombination_boltzmann(
        n, p, nie, tau_n, tau_p, mat.Cn_auger, mat.Cp_auger, auger)
    for name, r, g in zip(("R", "dRdn", "dRdp"), (ref_R, ref_dRdn, ref_dRdp),
                          (got_R, got_dRdn, got_dRdp)):
        g = np.asarray(g)
        assert g.shape == r.shape, name
        assert np.array_equal(g, r), (name, np.flatnonzero(g != r)[:5])


def test_recombination_boltzmann_refuses_mismatched_length():
    with pytest.raises(ValueError):
        _accel.core.recombination_boltzmann(
            np.ones(5), np.ones(4), np.ones(5), np.ones(5), np.ones(5),
            1e-31, 1e-31, True)

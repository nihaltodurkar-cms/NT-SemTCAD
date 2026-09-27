"""Regression tests for the v0.4 final-review findings (I-1..I-6 and the
cheap Minors).  Each test names its finding ID; see the review report.

PySide6/QML removed from this repo: I-1 through I-3, I-5, I-6, and M-3
(all AppController/JobRunner/MplCanvasItem regressions) were removed
with it. I-4 and M-4/M-5/M-6 are pure gui.services logic, kept.
"""
import numpy as np
import pytest

from gui.services import solver_runner
from gui.services.device_spec import (
    ContactSpec, DeviceSpec, DopingSpec, MeshSpec, SweepSpec,
)


# ----------------------------------------------------------------------
# I-4: an all-diverged sweep must fall back to equilibrium fields
# ----------------------------------------------------------------------
def test_all_diverged_sweep_returns_equilibrium_fallback():
    x = np.linspace(0.0, 2e-4, 30)
    spec = DeviceSpec(
        mesh=MeshSpec(dimensionality=1, axes={"x": x.tolist()}),
        doping=DopingSpec(kind="array",
                          values=np.where(x < 1e-4, -1e17, 1e17).tolist()),
        contacts=[
            ContactSpec(name="left", kind="ohmic", nodes={"i": [0]}, V=0.0),
            ContactSpec(name="right", kind="ohmic", nodes={"i": [29]}, V=0.0),
        ],
        bias={"right": 0.0},
        sweep=SweepSpec(contact="left", start=0.0, stop=0.3, step=0.1),
    )
    mesh_obj = solver_runner.build_mesh(spec.mesh)
    doping, ntotal = solver_runner.build_doping(spec.doping, spec.mesh.shape())
    device = solver_runner.build_device(spec, mesh_obj, doping, ntotal)
    device.solve_equilibrium()
    fallback = solver_runner.extract_result(device, spec, solved_bias=False)

    hopeless = __import__("pytcad.device", fromlist=["NewtonOptions"]) \
        .NewtonOptions(max_iter=2, tol_update=1e-30, verbose=False)
    fields, series = solver_runner.run_sweep(device, spec, hopeless,
                                             fallback_fields=fallback)
    assert not bool(series["sweep__converged"].any())
    assert fields is fallback, (
        "all-diverged sweep must return the pre-sweep equilibrium snapshot, "
        "never a diverged state presented as a biased result")


# ----------------------------------------------------------------------
# M-4 / M-5 / M-6: validation tightening
# ----------------------------------------------------------------------
def test_validate_values_caps_point_count():
    with pytest.raises(ValueError, match="points"):
        SweepSpec(contact="d", start=0.0, stop=1.0, step=1e-9).validate_values()


def test_devicespec_from_dict_uses_strict_sweep_parse():
    d = {"mesh": {"dimensionality": 1, "axes": {"x": [0.0, 1.0]}},
         "doping": {"kind": "array", "values": [1e17, 1e17], "ntotal": None},
         "sweep": {"contact": "d", "start": 0.0}}       # missing stop/step
    with pytest.raises(ValueError, match="sweep"):
        DeviceSpec.from_dict(d)

"""Regression test for a real, pre-existing bug found during the QML
architecture cleanup pass (2026-09-04) and confirmed unrelated to that
work (byte-identical diff on app_controller.py/result_store.py at the
time): AppController.solverEngineLabel crashed with

    AttributeError: 'SpecResultStore' object has no attribute 'has_record'

whenever a device was loaded (via loadExample()) but not yet solved --
i.e. whenever self._store is a SpecResultStore (a structure preview),
not an NpzResultStore (an actual solve output).

Root cause: has_record()/run_record() were added directly to
NpzResultStore (gui/services/result_store.py) without also adding them
to the ResultStore ABC as protocol members with honest defaults --
unlike every sibling capability (has_sweep/sweep_result, has_transient/
transient_result, has_band_diagram/band_diagram), which the ABC's own
docstring says is exactly the intended pattern: "Sweep and solved-
result support are protocol members with honest defaults rather than
abstractmethods: most stores legitimately carry neither." has_record/
run_record simply never got that treatment. SpecResultStore (and any
other non-Npz store) inherited no default and crashed instead of
answering honestly.

PySide6/QML removed from this repo: the two AppController-driven
regression tests (loadExample()/solverEngineLabel before and after a
real solve) were removed with it -- the root cause itself, and its fix,
live entirely in gui/services/result_store.py, gated below directly.
"""
from gui.services.result_store import SpecResultStore


def test_spec_result_store_answers_has_record_honestly_instead_of_crashing():
    # ResultStore's own ABC docstring: "most stores legitimately carry
    # neither" -- a structure preview is exactly such a store.
    from gui.services.device_spec import DeviceSpec, MeshSpec, DopingSpec, ContactSpec
    spec = DeviceSpec(
        mesh=MeshSpec(dimensionality=1, axes={"x": [0.0, 1e-4]}),
        doping=DopingSpec(kind="array", values=[-1e16, -1e16]),
        contacts=[ContactSpec(name="a", kind="ohmic", nodes={"i": [0]}, V=0.0),
                  ContactSpec(name="b", kind="ohmic", nodes={"i": [1]}, V=0.0)],
        bias={"a": 0.0, "b": 0.0})
    store = SpecResultStore(spec)
    assert store.has_record() is False
    assert store.run_record() is None

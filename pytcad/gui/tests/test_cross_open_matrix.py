"""NATIVE-DESKTOP-PLAN.md section 20.4/24: the cross-open byte-identical
matrix disclosed as remaining scope at S9/section 22/23 -- running the
real `project.save`/`project.load` RPC path (backend_service/server.py,
the exact functions the native app's BackendClient calls, not a
lower-level project_store.py shortcut) across every shipped structure
example, both directions:

  1. in-memory model -> project.save -> project.load -> the reloaded
     model matches the original (save-then-load fidelity);
  2. project.save -> project.load -> project.save again -> the two
     saved files are BYTE-IDENTICAL (load-then-resave stability, the
     literal "byte-identical" gate this section names).

"Every shipped example" = gui/services/examples.py's STRUCTURE_EXAMPLES
(the Structure/Mesh workbench's own "load example" list) plus every
workbench.core.templates template (the section 24 template picker's own
catalog) converted through the same structure_from_domain() adapter
templates.build already uses -- both are real, user-reachable sources of
a structure+mesh pair in this app, not synthetic fixtures invented for
this test. Each is paired with two process flows (empty, and a small
non-trivial substrate/implant/anneal flow) since a real project always
carries a process document alongside structure/mesh.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import pytest

from gui.services.process_model import ProcessFlow, ProcessStep
from gui.services.examples import STRUCTURE_EXAMPLES
from backend_service.server import _project_load, _project_save

try:
    from workbench.core.templates import get_template, list_templates
    from workbench.adapters.spec import structure_from_domain
    _accel_import_error = None
except ImportError as exc:  # pytcad._core not built on this machine/session
    _accel_import_error = exc


def _empty_flow():
    return ProcessFlow()


def _nontrivial_flow():
    return ProcessFlow(steps=[
        ProcessStep("s1", "Substrate", "substrate",
                    parameters={"material": "Si", "doping_cm3": -1e15, "length_cm": 5e-4}),
        ProcessStep("s2", "Implant", "implant",
                    parameters={"species": "P", "dose_cm2": 1e13, "energy_kev": 60}),
        ProcessStep("s3", "Anneal", "anneal",
                    parameters={"time_s": 600, "temperature_c": 950}),
    ])


def _structure_example_cases():
    cases = [(name, factory) for name, factory in STRUCTURE_EXAMPLES.items()]
    if _accel_import_error is None:
        for tid in list_templates():
            def factory(tid=tid):
                return structure_from_domain(get_template(tid).build(None))
            cases.append((f"template:{tid}", factory))
    return cases


_CASES = _structure_example_cases()
_FLOWS = {"empty": _empty_flow, "nontrivial": _nontrivial_flow}


def _ids():
    return [f"{name}/{flow}" for name, _ in _CASES for flow in _FLOWS]


def _params():
    return [(name, factory, flow_name) for name, factory in _CASES for flow_name in _FLOWS]


@pytest.mark.skipif(_accel_import_error is not None,
                    reason=f"pytcad._core not built: {_accel_import_error}")
@pytest.mark.parametrize("name,factory,flow_name", _params(), ids=_ids())
def test_cross_open_round_trip_is_byte_identical(tmp_path, name, factory, flow_name):
    structure, mesh = factory()
    process = _FLOWS[flow_name]()

    path_a = str(tmp_path / "a.json")
    save_params = {"path": path_a, "name": name, "structure": structure.to_dict(),
                   "mesh": mesh.to_dict(), "process": process.to_dict(),
                   "sweep": None, "models": None, "spec_version": 1, "target_version": 6}
    _project_save(save_params)
    bytes_a = open(path_a, "rb").read()

    # direction 1: save-then-load fidelity -- the reloaded model matches
    # what was saved (compared by dict, since StructureModel/MeshModel/
    # ProcessFlow have no __eq__; their to_dict() is the wire contract).
    loaded = _project_load({"path": path_a})
    assert loaded["structure"] == structure.to_dict()
    assert loaded["mesh"] == mesh.to_dict()
    assert loaded["process"] == process.to_dict()
    assert loaded["models"] is None
    assert loaded["sweep"] is None

    # direction 2: load-then-resave stability -- re-saving exactly what
    # was just loaded reproduces the SAME bytes, not just the same dict.
    path_b = str(tmp_path / "b.json")
    resave_params = {"path": path_b, "name": loaded["name"], "structure": loaded["structure"],
                      "mesh": loaded["mesh"], "process": loaded["process"],
                      "sweep": loaded["sweep"], "models": loaded["models"],
                      "spec_version": 1, "target_version": 6}
    _project_save(resave_params)
    bytes_b = open(path_b, "rb").read()
    assert bytes_a == bytes_b, f"{name}/{flow_name}: cross-open round trip is not byte-identical"

    # schema-5 downgrade direction, same two checks, for the same reason
    # section 20.3 decision 4 exists: an ordinary project (spec_version 1)
    # must downgrade to schema 5 cleanly too, and stay byte-stable there.
    path_c = str(tmp_path / "c.json")
    save_params_v5 = dict(save_params, path=path_c, target_version=5)
    _project_save(save_params_v5)
    loaded_v5 = _project_load({"path": path_c})
    path_d = str(tmp_path / "d.json")
    resave_v5 = {"path": path_d, "name": loaded_v5["name"], "structure": loaded_v5["structure"],
                 "mesh": loaded_v5["mesh"], "process": loaded_v5["process"],
                 "sweep": loaded_v5["sweep"], "models": loaded_v5["models"],
                 "spec_version": 1, "target_version": 5}
    _project_save(resave_v5)
    assert open(path_c, "rb").read() == open(path_d, "rb").read(), \
        f"{name}/{flow_name}: schema-5 cross-open round trip is not byte-identical"
    assert json.loads(open(path_c).read())["schema_version"] == 5

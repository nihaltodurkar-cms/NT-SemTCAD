"""Schema v5: Physics Lab model config in project files.

v5 adds one further optional key -- "models" (the ModelCatalog config
dict, or null). v2/v3/v4 files simply lack the key, which loads as
model_config=None -- the caller's contract for None is "leave whatever
Physics Lab config is already in effect untouched", i.e. byte-identical
to pre-v5 behavior for old files.

PySide6/QML removed from this repo: the "controller integration" section
below (real save/load through AppController) was removed with it -- the
project_store-level round trip these tests cover directly is unaffected.
"""
import json
import os, sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from gui.services.structure_model import (
    BoundarySpec, ContactModel, GateModel, MeshModel, RegionSpec, StructureModel)
from gui.services.process_model import ProcessFlow
from gui.services.project_store import SCHEMA_VERSION, load_project, save_project
from workbench.core.catalog import ModelCatalog


def _sample():
    structure = StructureModel(width_cm=4e-5, height_cm=2e-5, regions=[
        RegionSpec("ch", "Channel", 0.0, 4e-5, 0.0, 2e-5, -1e17)],
        contacts=[ContactModel("c1", "left", BoundarySpec("left"), 0.0)],
        gates=[GateModel("g1", "gate", BoundarySpec("top"), tox_cm=5e-7,
                         vfb_mode="manual", vfb_manual=-0.8)])
    mesh = MeshModel(nx=10, ny=6)
    return structure, mesh


# ----------------------------------------------------------------------
#  version bump and file shape
# ----------------------------------------------------------------------
def test_schema_version_is_at_least_5():
    # NATIVE-DESKTOP-PLAN.md section 20.3 decision 1/4 (P4 S9) bumped
    # this to 6, adding "spec_version" -- see test_persistence_v6.py for
    # that bump's own gates. This module's own assertions (below) stay
    # version-tolerant rather than hardcoding the exact current value a
    # second time.
    assert SCHEMA_VERSION >= 5


def test_v5_file_contains_models_key(tmp_path):
    structure, mesh = _sample()
    config = ModelCatalog.default_config()
    config["auger"] = False
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, ProcessFlow(), None, config)
    data = json.load(open(path))
    assert data["schema_version"] == SCHEMA_VERSION
    assert data["models"] == config


def test_v5_file_without_model_config_writes_null(tmp_path):
    """save_project's new parameter is optional -- a caller that never
    passes model_config still writes a valid v5 file."""
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh)
    assert json.load(open(path))["models"] is None


# ----------------------------------------------------------------------
#  round-trip: create -> save -> load -> compare, at the module level
# ----------------------------------------------------------------------
def test_v5_model_config_round_trips_exactly(tmp_path):
    structure, mesh = _sample()
    config = ModelCatalog.default_config()
    config["auger"] = False
    config["impact"] = True
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, ProcessFlow(), None, config)

    name, s, m, f, sweep, model_config = load_project(path)
    assert model_config == config


def test_v5_file_with_null_models_loads_none(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, ProcessFlow(), None, None)
    _, _, _, _, _, model_config = load_project(path)
    assert model_config is None


# ----------------------------------------------------------------------
#  backward compatibility: v2/v3/v4 files have no "models" key at all
# ----------------------------------------------------------------------
def test_v4_file_loads_with_model_config_none(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh)          # writes SCHEMA_VERSION (5)
    data = json.load(open(path))
    del data["models"]                                # simulate a real v4 file
    data["schema_version"] = 4
    with open(path, "w") as fh:
        json.dump(data, fh)
    _, _, _, _, _, model_config = load_project(path)
    assert model_config is None


def test_v3_file_loads_with_model_config_none(tmp_path):
    structure, mesh = _sample()
    data = {
        "schema_version": 3,
        "name": "legacy",
        "structure": structure.to_dict(),
        "mesh": mesh.to_dict(),
        "process": {"steps": []},
    }
    path = str(tmp_path / "v3.json")
    with open(path, "w") as fh:
        json.dump(data, fh)
    _, _, _, _, _, model_config = load_project(path)
    assert model_config is None

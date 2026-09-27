"""Schema v6 (NATIVE-DESKTOP-PLAN.md section 20.3 decisions 1 and 4,
P4 S9): one further optional key, "spec_version" (DeviceSpec.
CURRENT_SPEC_VERSION's own project-level echo), plus the "native-only
when schema-6-only features are used" downgrade policy: a project whose
spec_version is above 1 cannot be saved as schema 5 -- save_project
refuses outright (IncompatibleDowngradeError) rather than silently
dropping the feature.

Nothing in this build ever produces spec_version > 1 (no version-2 wire
feature exists yet -- device_spec.py's CURRENT_SPEC_VERSION is 1), so
every project this build actually saves downgrades cleanly; the refusal
path below is exercised with an explicit spec_version=2 to prove the
POLICY works ahead of anything that needs it.
"""
import json
import os

from gui.services.structure_model import (
    BoundarySpec, ContactModel, MeshModel, RegionSpec, StructureModel)
from gui.services.process_model import ProcessFlow
from gui.services.project_store import (
    SCHEMA_VERSION, IncompatibleDowngradeError, UnsupportedProjectVersionError,
    load_project, save_project)


def _sample():
    structure = StructureModel(width_cm=4e-5, height_cm=2e-5, regions=[
        RegionSpec("ch", "Channel", 0.0, 4e-5, 0.0, 2e-5, -1e17)],
        contacts=[ContactModel("c1", "left", BoundarySpec("left"), 0.0)])
    mesh = MeshModel(nx=10, ny=6)
    return structure, mesh


# ----------------------------------------------------------------------
#  version bump and file shape
# ----------------------------------------------------------------------
def test_schema_version_is_6():
    assert SCHEMA_VERSION == 6


def test_default_save_writes_schema_6_without_a_spec_version_key(tmp_path):
    """spec_version defaults to 1 -- absent from the file, matching the
    v5 "models" precedent (an implied default is never written out)."""
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh)
    data = json.load(open(path))
    assert data["schema_version"] == 6
    assert "spec_version" not in data


def test_explicit_spec_version_is_written_at_schema_6(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, spec_version=1)
    # spec_version=1 IS the implied default -- still omitted, same as above.
    data = json.load(open(path))
    assert "spec_version" not in data


# ----------------------------------------------------------------------
#  round trip, v6
# ----------------------------------------------------------------------
def test_v6_round_trips_exactly_like_v5(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, ProcessFlow())
    name, s, m, f, sweep, model_config = load_project(path)
    assert name == "P"
    assert s == structure
    assert m == mesh
    assert sweep is None
    assert model_config is None


# ----------------------------------------------------------------------
#  backward compatibility: v2-v5 files have no "spec_version" key at all
# ----------------------------------------------------------------------
def test_v5_file_loads_as_spec_version_1_implicitly(tmp_path):
    """A real pre-v6 file (no "spec_version" key) must still load
    cleanly -- decision 4's own "a schema-5 file opened natively still
    loads cleanly" requirement."""
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, target_version=5)
    data = json.load(open(path))
    assert data["schema_version"] == 5
    assert "spec_version" not in data
    name, s, m, f, sweep, model_config = load_project(path)  # must not raise
    assert s == structure


# ----------------------------------------------------------------------
#  decision 4: the downgrade refusal
# ----------------------------------------------------------------------
def test_downgrading_a_schema_6_only_project_to_schema_5_is_refused(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    import pytest
    with pytest.raises(IncompatibleDowngradeError, match="spec_version"):
        save_project(path, "P", structure, mesh, spec_version=2, target_version=5)
    assert not os.path.exists(path), \
        "a refused save must not leave a partial/incorrect file behind"


def test_spec_version_1_downgrades_cleanly(tmp_path):
    """The negative case above only matters if the POSITIVE case (an
    ordinary, spec_version=1 project) still downgrades without
    complaint -- decision 4's own "everything schema-5-representable
    still round-trips" half."""
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, spec_version=1, target_version=5)
    data = json.load(open(path))
    assert data["schema_version"] == 5


def test_saving_a_schema_6_only_project_at_schema_6_succeeds(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, spec_version=2, target_version=6)
    data = json.load(open(path))
    assert data["schema_version"] == 6
    assert data["spec_version"] == 2


def test_loading_a_spec_version_above_this_builds_support_is_a_loud_named_error(tmp_path):
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    save_project(path, "P", structure, mesh, spec_version=2, target_version=6)
    import pytest
    with pytest.raises(UnsupportedProjectVersionError, match="spec_version"):
        load_project(path)


def test_unknown_schema_version_is_still_refused():
    import pytest
    import tempfile
    data = {"schema_version": 7, "name": "P", "structure": None, "mesh": None,
           "process": {"steps": []}, "sweep": None, "models": None}
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "p.json")
        with open(path, "w") as fh:
            json.dump(data, fh)
        with pytest.raises(UnsupportedProjectVersionError, match="schema version"):
            load_project(path)


def test_target_version_rejects_anything_other_than_5_or_6(tmp_path):
    import pytest
    structure, mesh = _sample()
    path = str(tmp_path / "p.json")
    with pytest.raises(ValueError, match="target_version"):
        save_project(path, "P", structure, mesh, target_version=4)

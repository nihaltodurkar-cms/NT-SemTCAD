"""Project file save/load.  Results (.npz) live in a separate results/
directory and are never embedded here -- only StructureModel/MeshModel's
scalars and short lists are.

A v4 project may hold any combination of structure=None, mesh=None, and
a process flow that is present or empty -- process-only projects,
structure-only projects, and structure+process projects all round-trip
correctly. No placeholder/fake geometry is ever synthesized here.
v2/v3 files load migrated (v2: empty process flow; v2/v3: sweep=None).
v5 adds one further optional key: "models" (the Physics Lab's
ModelCatalog config dict, or null) -- v2/v3/v4 files simply lack the key,
which loads as None, exactly like a v4 file's absent "sweep" key before it.

v6 (NATIVE-DESKTOP-PLAN.md section 20.3 decision 1/4) adds one further
optional key: "spec_version" (DeviceSpec.CURRENT_SPEC_VERSION's own
project-level echo -- see gui/services/device_spec.py), absent implies
1, exactly the "models" precedent above. Decision 4: a project whose
spec_version is above 1 is using a schema-6-only, native-app feature and
CANNOT be saved as schema 5 -- save_project(..., target_version=5)
refuses loudly (IncompatibleDowngradeError) rather than silently
dropping it. Nothing sets spec_version above 1 yet (no version-2 wire
feature exists), so every project today downgrades cleanly; this is
forward-looking infrastructure for the decision, not a behavior change
for any project saved by this build.
"""
import json

from .structure_model import StructureModel, MeshModel
from .process_model import ProcessFlow
from .device_spec import SweepSpec, CURRENT_SPEC_VERSION

SCHEMA_VERSION = 6


class UnsupportedProjectVersionError(Exception):
    pass


class IncompatibleDowngradeError(Exception):
    """Raised by save_project when the project uses a schema-6-only
    feature (spec_version > 1) and the caller asked for schema 5."""
    pass


def save_project(path, name, structure, mesh_model, process_flow=None,
                 sweep=None, model_config=None, spec_version=1,
                 target_version=SCHEMA_VERSION):
    """v5 added one optional key: "models". v6 adds "spec_version" --
    like "models", only written when it differs from its implied
    default (1), so an ordinary project's v6 file is byte-for-byte the
    same shape a v5 file would have been. Results are still never
    embedded -- a saved project is configuration, never in-progress or
    stale output.

    `target_version=5` writes the older, schema-5-compatible file (no
    "spec_version" key at all -- matching how a pre-v5 file simply
    lacks "models"), refusing outright (IncompatibleDowngradeError) if
    `spec_version` is above 1: that content genuinely cannot be
    represented in schema 5, so this never silently drops it."""
    if target_version not in (5, SCHEMA_VERSION):
        raise ValueError(f"target_version must be 5 or {SCHEMA_VERSION}, got {target_version!r}")
    if target_version == 5 and spec_version > 1:
        raise IncompatibleDowngradeError(
            f"project uses spec_version {spec_version} (a schema-{SCHEMA_VERSION}-only "
            "feature) and cannot be saved as schema 5")
    data = {
        "schema_version": target_version,
        "name": name,
        "structure": structure.to_dict() if structure is not None else None,
        "mesh": mesh_model.to_dict() if mesh_model is not None else None,
        "process": (process_flow or ProcessFlow()).to_dict(),
        "sweep": sweep.to_dict() if sweep is not None else None,
        "models": dict(model_config) if model_config is not None else None,
    }
    if target_version >= 6 and spec_version != 1:
        data["spec_version"] = spec_version
    with open(path, "w") as fh:
        json.dump(data, fh, indent=2)


def load_project(path):
    with open(path) as fh:
        data = json.load(fh)
    version = data.get("schema_version")
    if version not in (2, 3, 4, 5, 6):
        raise UnsupportedProjectVersionError(
            f"project schema version {version!r} is not supported "
            f"(this build supports 2 and 3 (migrated), 4, 5, and {SCHEMA_VERSION})")
    structure = StructureModel.from_dict(data["structure"]) if data.get("structure") else None
    mesh_model = MeshModel.from_dict(data["mesh"]) if data.get("mesh") else None
    process_flow = ProcessFlow.from_dict(data.get("process") or {"steps": []})
    # v2/v3 files have no "sweep" key; v4+ files carry a dict or null.
    # Missing -> safe default None.  Present-but-invalid -> clear error
    # here, at load time, not later as a failed solver job.  Contact-name
    # validity can only be judged against the real spec at Run time
    # (AppController.run() does exactly that via SweepSpec.validate).
    raw_sweep = data.get("sweep")
    sweep = SweepSpec.from_dict(raw_sweep) if raw_sweep is not None else None
    # v2/v3/v4 files have no "models" key at all; v5+ files carry a dict
    # or null. Missing/null -> None, telling the caller to leave whatever
    # Physics Lab config is already in effect (i.e. the catalog defaults)
    # untouched -- exactly the pre-v5 behavior, so old projects keep
    # loading byte-identically. Structural validation (dict of known keys
    # to bools) is ModelCatalog's job, not this module's -- the caller
    # already applies it via PhysicsLabController.setModelConfig().
    raw_models = data.get("models")
    model_config = dict(raw_models) if isinstance(raw_models, dict) else None
    # v2-v5 files have no "spec_version" key at all -- missing means 1,
    # the same "absent implies the pre-field default" rule spec_version
    # itself uses on DeviceSpec (device_spec.py).
    raw_spec_version = data.get("spec_version")
    spec_version = raw_spec_version if isinstance(raw_spec_version, int) else 1
    if spec_version < 1 or spec_version > CURRENT_SPEC_VERSION:
        raise UnsupportedProjectVersionError(
            f"project spec_version {spec_version!r} is not supported "
            f"(this build supports up to {CURRENT_SPEC_VERSION})")
    # spec_version is validated above but NOT added as a 7th return
    # value: every existing caller (grepped -- run_config.py,
    # app_controller.py, and every persistence test) unpacks a 6-tuple,
    # and nothing anywhere consumes a project-level spec_version yet (no
    # version-2 wire feature exists to need it). Changing this shape
    # would break every one of those unmodified, pinned call sites for
    # no present benefit -- the validation-and-refuse behavior decision
    # 4 asks for is complete without it; a future caller that needs the
    # value back can be given it then, deliberately, not preemptively.
    return (data["name"], structure, mesh_model, process_flow, sweep, model_config)

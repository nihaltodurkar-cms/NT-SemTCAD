"""Study rows and jobs, Qt-free (NATIVE-DESKTOP-PLAN.md 17.16, P3-S7).

Moved out of StudyController's configureStudy/_spec_for_row so the QML
controller and the native app (through the backend's study.templates /
study.rows) build the same rows with the same statuses, and the same
equilibrium-only job for each one. `RunConfigError` is not raised here:
a row the template rejects is recorded as "build_error" on that row,
never an exception -- run_split_matrix's own per-row isolation
(pytcad/M30-WORKBENCH-PLAN.md's G-ISOLATION gate), restated through
this module's status vocabulary. Only a template id run_split_matrix
itself cannot resolve is a hard failure.
"""
from .run_config import RunConfigError


def study_rows(template_id, base_values, split_spec):
    """The rows of a study's split matrix, exactly as
    StudyController.configureStudy builds them: a row the template
    rejects is 'build_error' rather than aborting the whole study.

    Returns (axes, rows): `axes` is {name: [values...]}, split_spec's
    own insertion order (the LAST axis varies fastest, same as
    expand_splits); `rows` is a list of
    {"params", "status", "error", "device"} dicts, "device" the built
    DomainDevice or None for a build_error row.
    """
    from workbench.splits import run_split_matrix
    from workbench.workflow import DeckRun

    run = DeckRun(template_id=str(template_id))
    run._values = dict(base_values or {})
    run.splits = {k: [float(v) for v in vals]
                 for k, vals in dict(split_spec or {}).items()}
    try:
        split_rows = run_split_matrix(run)
    except Exception as exc:
        raise RunConfigError("Could not configure study", str(exc)) from None

    rows = []
    for row in split_rows:
        rows.append({
            "params": dict(row.params),
            "status": "build_error" if row.error else "pending",
            "error": row.error or "",
            "device": row.device,
        })
    return dict(run.splits), rows


def row_spec(device):
    """The DeviceSpec for one study row: equilibrium-only (decision 1,
    NATIVE-DESKTOP-PLAN.md 17.16) -- every QML study row solves this
    way today; `setBias` exists on StudyController but no QML code
    calls it, so a bias/sweep study is new behaviour, not a port."""
    from workbench.adapters.spec import spec_from_domain
    spec = spec_from_domain(device)
    spec.bias = None
    spec.sweep = None
    return spec


def template_info(template):
    """One DeviceTemplate as a plain dict: id, title, description, and
    each TemplateParam's name/label/unit/default/lo/hi/integer -- the
    same shape study.templates hands the native app and the base/
    splits tables read their bounds from."""
    return {
        "id": template.id,
        "title": template.title,
        "description": template.description,
        "params": [
            {"name": p.name, "label": p.label, "unit": p.unit,
             "default": p.default, "lo": p.lo, "hi": p.hi,
             "integer": p.integer}
            for p in template.params
        ],
    }

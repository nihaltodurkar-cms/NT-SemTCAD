"""NATIVE-DESKTOP-PLAN.md P3-S7: the local Study, backend side.

The native app builds a study's rows and jobs through the backend
(study.templates, study.rows, from gui/services/study_jobs.py) --
PySide6/QML removed this repo's QML StudyController, which used to call
the SAME gui.services.study_jobs functions directly (study_jobs.py
itself is Qt-free and unaffected). Gated here as a conformance check
between the backend RPC and a direct call to study_jobs.py:
  - study.rows equals a direct study_jobs.study_rows() call (params,
    statuses, errors, axes) for a 2x2 mos_capacitor study that
    includes a rejected value;
  - every row's job_text is byte-identical to study_jobs.row_spec()'s
    own job file for that row (equilibrium-only);
  - study.templates equals the templates' own parameter lists.
"""
import json
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from backend_service import server  # noqa: E402
from gui.services import study_jobs  # noqa: E402
from workbench.core.templates import get_template, list_templates  # noqa: E402


def _call(method, params):
    resp, _ = server.handle(json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}))
    return json.loads(json.dumps(resp, allow_nan=False))


MOS_BASE = {}
MOS_SPLITS = {"na_cm3": [-1e16, -5e16], "tox_cm": [1e-6, 2e-4]}  # 2e-4 > hi=1e-4: rejected


def test_study_rows_matches_a_direct_call():
    axes, rows = study_jobs.study_rows("mos_capacitor", MOS_BASE, MOS_SPLITS)
    expected_rows = [{"params": r["params"], "status": r["status"], "error": r["error"]}
                     for r in rows]

    result = _call("study.rows", {"template_id": "mos_capacitor", "base": MOS_BASE,
                                  "splits": MOS_SPLITS})["result"]
    assert result["axes"] == axes
    native_rows = [{"params": r["params"], "status": r["status"], "error": r["error"]}
                  for r in result["rows"]]
    assert native_rows == expected_rows
    assert len(result["rows"]) == 4
    assert sum(1 for r in result["rows"] if r["status"] == "build_error") == 2

    for i, row in enumerate(result["rows"]):
        if row["status"] == "build_error":
            assert row["job_text"] is None
        else:
            job = study_jobs.row_spec(rows[i]["device"])
            assert row["job_text"].encode("utf-8") == json.dumps(job.to_dict()).encode("utf-8")


def test_study_templates_equals_the_catalog():
    result = _call("study.templates", {})["result"]
    assert [t["id"] for t in result] == list_templates()
    for entry, tid in zip(result, list_templates()):
        template = get_template(tid)
        assert entry["title"] == template.title
        assert entry["description"] == template.description
        assert [p["name"] for p in entry["params"]] == [p.name for p in template.params]
        for p_out, p in zip(entry["params"], template.params):
            assert p_out == {"name": p.name, "label": p.label, "unit": p.unit,
                             "default": p.default, "lo": p.lo, "hi": p.hi,
                             "integer": p.integer}


def test_a_build_error_row_has_no_job_text():
    result = _call("study.rows", {"template_id": "mos_capacitor", "base": {},
                                  "splits": {"tox_cm": [5e-4]}})["result"]
    assert len(result["rows"]) == 1
    row = result["rows"][0]
    assert row["status"] == "build_error" and row["job_text"] is None
    assert row["error"]


def test_study_rows_with_no_splits_is_the_single_base_row():
    result = _call("study.rows", {"template_id": "resistor", "base": {}, "splits": {}})["result"]
    assert result["axes"] == {}
    assert len(result["rows"]) == 1
    assert result["rows"][0]["status"] == "pending"
    assert result["rows"][0]["job_text"]


def test_an_unknown_template_is_an_application_error():
    err = _call("study.rows", {"template_id": "not_a_template", "base": {}, "splits": {}})["error"]
    assert err["code"] == server.APPLICATION_ERROR


@pytest.mark.parametrize("params", [
    {"base": {}, "splits": {}},                      # no template_id
    {"template_id": 3, "base": {}, "splits": {}},
    {"template_id": "resistor", "base": [1, 2]},
    {"template_id": "resistor", "splits": "x"},
])
def test_malformed_study_rows_params_are_invalid_params(params):
    assert _call("study.rows", params)["error"]["code"] == server.INVALID_PARAMS

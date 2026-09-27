"""NATIVE-DESKTOP-PLAN.md P3-S6: family and comparison jobs.

The native app builds families and comparisons through the backend
(family.jobs, comparison.job, from gui/services/family_jobs.py) --
PySide6/QML removed this repo's QML FamilySweepController/
AppController.runModelComparison, which used to call the SAME
gui.services.family_jobs functions directly (family_jobs.py itself is
Qt-free and unaffected). Gated here as a conformance check between the
backend RPC and a direct call to family_jobs.py, the exact pattern
test_backend_service.py already uses for structure.validate/
process.validate:
  - every job file a family writes, byte for byte, and its labels;
  - the comparison's job file, byte for byte;
  - every family refusal: the same (title, detail);
  - the family values, including the one-curve and reverse cases.
"""
import json
import os
import sys
import tempfile

import numpy as np
import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from backend_service import server  # noqa: E402
from gui.services import examples, family_jobs  # noqa: E402
from gui.services.device_spec import ContactSpec, DeviceSpec, DopingSpec, MeshSpec, SweepSpec  # noqa: E402


def _call(method, params):
    resp, _ = server.handle(json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}))
    return json.loads(json.dumps(resp, allow_nan=False))


def _base_spec():
    """A small 1D diode with two named contacts and a bias already
    attached -- exactly the shape a real Run leaves `lastRunSpec()` in,
    which is what family_jobs.family_specs/comparison_spec are always
    called against."""
    x = np.linspace(0.0, 2e-4, 20)
    doping = np.where(x < 1e-4, -1e17, 1e17)
    return DeviceSpec(
        mesh=MeshSpec(dimensionality=1, axes={"x": x.tolist()}),
        doping=DopingSpec(kind="array", values=doping.tolist()),
        contacts=[ContactSpec(name="cathode", kind="ohmic", nodes={"i": [0]}, V=0.0),
                  ContactSpec(name="anode", kind="ohmic", nodes={"i": [19]}, V=0.0)],
        bias={"cathode": 0.0, "anode": 0.0},
        sweep=SweepSpec(contact="anode", start=0.0, stop=0.3, step=0.1))


def _family_params(base, stepped, values, swept):
    return {"spec": base.to_dict(), "stepped": stepped,
            "values": dict(zip(("start", "stop", "step"), values)),
            "swept": dict(zip(("contact", "start", "stop", "step"), swept))}


FAMILIES = {
    "three": ("cathode", (0.0, 0.2, 0.1), ("anode", 0.0, 0.3, 0.1)),
    "reverse": ("cathode", (0.2, 0.0, -0.1), ("anode", 0.0, 0.3, 0.1)),
    "one_value": ("cathode", (0.05, 0.05, 0.0), ("anode", 0.0, 0.2, 0.1)),
}


@pytest.mark.parametrize("case", sorted(FAMILIES))
def test_family_job_files_match_a_direct_call(case):
    stepped, values, swept = FAMILIES[case]
    base = _base_spec()
    vals = family_jobs.family_values(*values)
    expected = [
        {"label": family_jobs.family_label(stepped, v), "value": v,
         "job_text": json.dumps(job.to_dict())}
        for v, job in family_jobs.family_specs(base, stepped, vals, swept[0], *swept[1:])
    ]
    got = _call("family.jobs", _family_params(base, stepped, values, swept))
    assert "error" not in got, got
    jobs = got["result"]
    assert [j["label"] for j in jobs] == [e["label"] for e in expected]
    assert [j["value"] for j in jobs] == [e["value"] for e in expected]
    assert [json.loads(j["job_text"]) for j in jobs] == \
        [json.loads(e["job_text"]) for e in expected]


def test_the_comparison_job_file_matches_a_direct_call():
    base = _base_spec()
    expected = family_jobs.comparison_spec(base)
    got = _call("comparison.job", {"spec": base.to_dict()})
    assert "error" not in got, got
    job = got["result"]
    assert job["label"] == family_jobs.COMPARISON_LABEL
    assert json.loads(job["job_text"]) == expected.to_dict()
    assert all(v is False for v in json.loads(job["job_text"])["models"].values())


REFUSALS = {
    "wrong_direction": ("cathode", (0.0, 0.2, -0.1), ("anode", 0.0, 0.3, 0.1)),
    "stepped_not_a_contact": ("gate", (0.0, 0.2, 0.1), ("anode", 0.0, 0.3, 0.1)),
    "swept_not_a_contact": ("cathode", (0.0, 0.2, 0.1), ("ghost", 0.0, 0.3, 0.1)),
    "invalid_sweep": ("cathode", (0.0, 0.2, 0.1), ("anode", 0.0, 0.3, 0.0)),
}


@pytest.mark.parametrize("case", sorted(REFUSALS))
def test_every_family_refusal_matches_a_direct_call(case):
    stepped, values, swept = REFUSALS[case]
    base = _base_spec()
    try:
        vals = family_jobs.family_values(*values)
        list(family_jobs.family_specs(base, stepped, vals, swept[0], *swept[1:]))
        pytest.fail("expected a RunConfigError from the direct call")
    except family_jobs.RunConfigError as exc:
        title, detail = exc.title, exc.detail

    err = _call("family.jobs", _family_params(base, stepped, values, swept))["error"]
    assert err["data"] == {"type": "RunConfigError", "title": title, "detail": detail}


def test_nothing_to_sweep_and_nothing_to_compare_are_refused():
    with pytest.raises(family_jobs.RunConfigError) as exc:
        family_jobs.family_specs(None, "a", [0.0], "b", 0, 1, 0.1)
    assert exc.value.title == "Nothing to sweep"
    with pytest.raises(family_jobs.RunConfigError) as exc2:
        family_jobs.comparison_spec(None)
    assert exc2.value.title == "Nothing to compare"


def test_the_family_leaves_its_base_untouched():
    base = examples.EXAMPLES["diode_1d"]()
    base.sweep = SweepSpec(contact="anode", start=0.0, stop=0.3, step=0.1)
    before = base.to_dict()
    family_jobs.family_specs(base, "cathode", [0.0, 0.1], "anode", 0.0, 0.3, 0.1)
    family_jobs.comparison_spec(base)
    assert base.to_dict() == before


@pytest.mark.parametrize("params", [
    {"stepped": "cathode", "values": {"start": 0, "stop": 1, "step": 0.5},
     "swept": {"contact": "anode", "start": 0, "stop": 1, "step": 0.5}},     # no spec
    {"spec": {}, "stepped": 3},
    {"spec": {}, "stepped": "c", "values": [0, 1, 0.5]},
])
def test_malformed_family_params_are_invalid_params(params):
    if "spec" in params and params["spec"] == {}:
        params = {**params, "spec": examples.EXAMPLES["diode_1d"]().to_dict()}
    assert _call("family.jobs", params)["error"]["code"] == server.INVALID_PARAMS

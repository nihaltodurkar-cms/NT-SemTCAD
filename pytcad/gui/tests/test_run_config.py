"""NATIVE-DESKTOP-PLAN.md P3-S2: the backend job methods.

The native app builds no job itself (section 17.5 decision 1): the backend
service loads the device (spec.from_example, spec.load, project.spec),
offers the backends and engines (run.options) and applies the run
configuration (spec.configure_run) through gui/services/run_config.py.

Gated here, each against a DIRECT call to gui/services/run_config.py
(PySide6/QML removed from this repo: these used to also cross-check
against AppController, which called the exact same run_config functions
-- the conformance being proven, RPC-equals-direct-Python-call, is
unaffected by that controller's removal):
  - every run() refusal: the same (title, detail) through the RPC;
  - every accepted run: the spec a direct configure_run() call produces
    equals the RPC's;
  - the backend/engine options equal a direct call's;
  - a project runs with its saved sweep and models;
  - a flow-only or invalid project is refused, named;
  - the loaders equal their direct Python calls; bad params are
    INVALID_PARAMS; and the round-trip latency is measured.
"""
import json
import os
import statistics
import subprocess
import sys
import time

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from backend_service import server  # noqa: E402
from gui.services import examples, run_config  # noqa: E402
from gui.services.device_spec import ACSpec, DeviceSpec, SweepSpec, TransientSpec, WaveformSpec  # noqa: E402
from gui.services.process_model import ProcessFlow, ProcessStep  # noqa: E402
from gui.services.project_store import save_project  # noqa: E402
from gui.services.run_config import RunConfigError  # noqa: E402
from gui.services.structure_model import (  # noqa: E402
    BoundarySpec, ContactModel, MeshModel, RegionSpec, StructureModel)
from workbench.core.catalog import ModelCatalog  # noqa: E402


def _call(method, params=None):
    req = {"jsonrpc": "2.0", "id": 1, "method": method}
    if params is not None:
        req["params"] = params
    resp, _ = server.handle(json.dumps(req))
    # Through JSON both ways, as on the wire.
    return json.loads(json.dumps(resp, allow_nan=False))


def _json(obj):
    return json.loads(json.dumps(obj, allow_nan=False))


def _run_via_rpc(spec, **kwargs):
    return _call("spec.configure_run", {"spec": spec.to_dict(), "run": kwargs})


# -- refusals -------------------------------------------------------------------

REFUSALS = {
    "sweep_contact_gone": (lambda: examples.EXAMPLES["mosfet_2d"](),
                           dict(sweep=SweepSpec(contact="anode", start=0.0, stop=0.5, step=0.1))),
    "transient_contact_gone": (lambda: examples.EXAMPLES["diode_1d"](),
                               dict(transient=TransientSpec(
                                   contact="ghost", waveform=WaveformSpec(kind="step", v0=0.0, v1=0.6),
                                   t_end=1e-9, dt0=1e-11))),
    "ac_contact_gone": (lambda: examples.EXAMPLES["mosfet_2d"](),
                       dict(ac=ACSpec(contact="anode", f_start=1e3, f_stop=1e9, n_points=5))),
    "sweep_and_transient": (lambda: examples.EXAMPLES["diode_1d"](),
                            dict(sweep=SweepSpec(contact="anode", start=0.0, stop=0.5, step=0.1),
                                 transient=TransientSpec(
                                     contact="anode", waveform=WaveformSpec(kind="step", v0=0.0, v1=0.6),
                                     t_end=1e-9, dt0=1e-11))),
    "transient_and_ac": (lambda: examples.EXAMPLES["diode_1d"](),
                        dict(transient=TransientSpec(
                            contact="anode", waveform=WaveformSpec(kind="step", v0=0.0, v1=0.6),
                            t_end=1e-9, dt0=1e-11),
                            ac=ACSpec(contact="anode", f_start=1e3, f_stop=1e9, n_points=5))),
    "equilibrium_only_with_sweep": (lambda: examples.EXAMPLES["diode_1d"](),
                                    dict(sweep=SweepSpec(contact="anode", start=0.0, stop=0.5, step=0.1),
                                         equilibrium_only=True)),
}

# configure_run()'s own unknown-backend refusal (restored so a DIRECT call,
# not just the RPC layer's earlier known_backends check, is guarded -- see
# CLAUDE.md's code-review addendum). The RPC layer refuses an unknown
# backend one step earlier as a plain ValueError, never reaching
# configure_run, so this can't join REFUSALS' RPC-parity check above; it
# is still counted in test_the_refusals_cover_every_title_run_config_raises
# below so this refusal stays gated.
DIRECT_ONLY_REFUSALS = {
    "unknown_backend": (lambda: examples.EXAMPLES["diode_1d"](), dict(backend="spice")),
}


def test_configure_run_refuses_an_unknown_backend_directly():
    make_spec, kwargs = DIRECT_ONLY_REFUSALS["unknown_backend"]
    with pytest.raises(RunConfigError) as exc:
        run_config.configure_run(make_spec(), **kwargs)
    assert exc.value.title == "Cannot run with backend 'spice'"
    assert "unknown backend 'spice'" in exc.value.detail


@pytest.mark.parametrize("case", sorted(REFUSALS))
def test_every_run_refusal_is_the_same_through_the_rpc(case):
    make_spec, kwargs = REFUSALS[case]
    spec = make_spec()
    with pytest.raises(RunConfigError) as exc:
        run_config.configure_run(spec, **kwargs)
    title, detail = exc.value.title, exc.value.detail

    resp = _run_via_rpc(spec, **{k: (v.to_dict() if hasattr(v, "to_dict") else v)
                                 for k, v in kwargs.items()})
    err = resp["error"]
    assert err["code"] == server.APPLICATION_ERROR
    assert err["data"] == {"type": "RunConfigError", "title": title, "detail": detail}
    assert err["message"] == f"{title}: {detail}"


def test_the_refusals_cover_every_title_run_config_raises():
    """No refusal in configure_run goes ungated above (each title once)."""
    import inspect
    import re
    src = inspect.getsource(run_config.configure_run)
    raised = set(re.findall(r'RunConfigError\(f?"([^"]+)"', src))
    seen = set()
    for case in REFUSALS:
        make_spec, kwargs = REFUSALS[case]
        with pytest.raises(RunConfigError) as exc:
            run_config.configure_run(make_spec(), **kwargs)
        seen.add(exc.value.title)
    for case in DIRECT_ONLY_REFUSALS:
        make_spec, kwargs = DIRECT_ONLY_REFUSALS[case]
        with pytest.raises(RunConfigError) as exc:
            run_config.configure_run(make_spec(), **kwargs)
        seen.add(exc.value.title.replace("spice", "{backend}"))
    assert raised == seen


# -- accepted runs --------------------------------------------------------------

def _models_without(key):
    cfg = ModelCatalog.default_config()
    cfg[key] = not cfg[key]
    ModelCatalog.validate(cfg)
    return cfg


ACCEPTED = {
    "bias": (lambda: examples.EXAMPLES["diode_1d"](), dict()),
    "sweep": (lambda: examples.EXAMPLES["diode_1d"](),
             dict(sweep=SweepSpec(contact="anode", start=0.0, stop=0.5, step=0.1))),
    "transient": (lambda: examples.EXAMPLES["diode_1d"](),
                 dict(transient=TransientSpec(
                     contact="anode", waveform=WaveformSpec(kind="step", v0=0.0, v1=0.6),
                     t_end=1e-9, dt0=1e-11))),
    "ac": (lambda: examples.EXAMPLES["diode_1d"](),
          dict(ac=ACSpec(contact="anode", f_start=1e3, f_stop=1e9, n_points=5))),
    "equilibrium_only": (lambda: examples.EXAMPLES["mosfet_2d"](), dict(equilibrium_only=True)),
    "engine_direct": (lambda: examples.EXAMPLES["resistor_3d"](), dict(engine="direct")),
    "models_toggled": (lambda: examples.EXAMPLES["diode_1d"](),
                       dict(models=_models_without("auger"))),
}


def _rpc_kwargs(kwargs):
    return {k: (v.to_dict() if hasattr(v, "to_dict") else v) for k, v in kwargs.items()}


@pytest.mark.parametrize("case", sorted(ACCEPTED))
def test_an_accepted_run_matches_a_direct_call(case):
    make_spec, kwargs = ACCEPTED[case]
    spec = make_spec()
    started = run_config.configure_run(spec, **kwargs)
    resp = _run_via_rpc(spec, **_rpc_kwargs(kwargs))
    assert "error" not in resp, resp
    assert resp["result"] == _json(started.to_dict())


@pytest.mark.parametrize("case", sorted(ACCEPTED))
def test_the_native_job_file_matches_the_direct_specs_json(case):
    """Section 17.4's contract: the native runner writes spec.job_text's
    string verbatim (UTF-8), byte-identical to DeviceSpec.to_json() of the
    same configured spec."""
    make_spec, kwargs = ACCEPTED[case]
    spec = make_spec()
    started = run_config.configure_run(spec, **kwargs)
    configured = _run_via_rpc(spec, **_rpc_kwargs(kwargs))["result"]
    text = _call("spec.job_text", {"spec": configured})["result"]
    assert json.loads(text) == json.loads(json.dumps(started.to_dict()))


@pytest.mark.parametrize("args", [(-1e17, 5.0, -2.0, 2.0, 0.05), (3e16, 2.5, -1.0, 1.5, 0.0),
                                  (-1e18, 10.0, 0.0, 3.0, -0.25)])
def test_the_cv_job_text_matches_a_direct_call(args):
    """P3-S4: the C-V job the native app writes (cv.job_text) equals
    gui.services.cv_job.job_text() for the same inputs -- including its
    zero-step fallback and its absolute step."""
    from gui.services import cv_job
    params = cv_job.cv_params(*args)
    expected = cv_job.job_text(params)
    keys = ("nsub_cm3", "tox_nm", "vstart", "vstop", "vstep")
    text = _call("cv.job_text", dict(zip(keys, args)))["result"]
    assert json.loads(text) == json.loads(expected)


@pytest.mark.parametrize("change,needle", [
    ({"tox_nm": 0.0}, "tox_nm must be > 0"),
    ({"nsub_cm3": 0.0}, "nsub_cm3 must be nonzero"),
    ({"vstop": -3.0}, "must be greater than vstart"),
])
def test_a_cv_job_that_cannot_run_is_refused_named(change, needle):
    params = {"nsub_cm3": -1e17, "tox_nm": 5.0, "vstart": -2.0, "vstop": 2.0, "vstep": 0.05, **change}
    err = _call("cv.job_text", params)["error"]
    assert err["code"] == server.APPLICATION_ERROR and needle in err["message"], err


@pytest.mark.parametrize("params", [{"tox_nm": 5.0}, {"nsub_cm3": "1e17", "tox_nm": 5.0, "vstart": 0,
                                                      "vstop": 1, "vstep": 0.1}, [1, 2]])
def test_malformed_cv_params_are_invalid_params(params):
    assert _call("cv.job_text", params)["error"]["code"] == server.INVALID_PARAMS


def test_the_armed_run_is_actually_applied():
    """The equality above is not vacuous: each case changes the spec."""
    base = examples.EXAMPLES["diode_1d"]().to_dict()
    out = _call("spec.configure_run", {"spec": base, "run": {
        "transient": {"contact": "anode", "t_end": 1e-9, "dt0": 1e-11,
                      "waveform": {"kind": "step", "v0": 0.0, "v1": 0.6}},
        "models": {"auger": False}, "engine": "direct"}})["result"]
    assert out["transient"]["contact"] == "anode" and out["sweep"] is None
    assert out["models"]["auger"] is False and out["engine"] == "direct"
    eq = _call("spec.configure_run", {"spec": base, "run": {"equilibrium_only": True}})["result"]
    assert base["bias"] is not None and eq["bias"] is None


def test_models_null_keeps_the_specs_own():
    spec = examples.EXAMPLES["diode_1d"]()
    spec.models = _models_without("auger")
    out = _call("spec.configure_run", {"spec": spec.to_dict(), "run": {}})["result"]
    assert out["models"] == spec.models


def test_configure_run_leaves_its_input_untouched():
    spec = examples.EXAMPLES["diode_1d"]()
    before = spec.to_dict()
    out = run_config.configure_run(spec, equilibrium_only=True, models=_models_without("auger"),
                                   engine="direct")
    assert spec.to_dict() == before and out.bias is None


# -- options --------------------------------------------------------------------

def test_run_options_default_to_the_specs_models():
    spec = examples.EXAMPLES["diode_1d"]()
    out = _call("run.options", {"spec": spec.to_dict()})["result"]
    assert out["backends"] == _json(run_config.backend_options(spec, spec.models))
    assert out["engines"] == _json(run_config.engine_options(spec, False))


@pytest.mark.parametrize("example", ["diode_1d", "mosfet_2d", "resistor_3d"])
@pytest.mark.parametrize("transient", [False, True])
def test_run_options_match_a_direct_call(example, transient):
    spec = examples.EXAMPLES[example]()
    models = _models_without("auger")
    resp = _call("run.options", {"spec": spec.to_dict(), "models": models,
                                 "transient_armed": transient})
    assert resp["result"] == _json({"backends": run_config.backend_options(spec, models),
                                    "engines": run_config.engine_options(spec, transient)})


# -- projects -------------------------------------------------------------------

def _structure():
    structure = StructureModel(width_cm=4e-5, height_cm=2e-5, regions=[
        RegionSpec("p", "P side", 0.0, 2e-5, 0.0, 2e-5, -1e17),
        RegionSpec("n", "N side", 2e-5, 4e-5, 0.0, 2e-5, 1e17)],
        contacts=[ContactModel("c1", "anode", BoundarySpec("left"), 0.0),
                  ContactModel("c2", "cathode", BoundarySpec("right"), 0.0)])
    return structure, MeshModel(nx=12, ny=6)


def test_a_project_runs_with_its_sweep_and_models_matching_a_direct_call(tmp_path):
    structure, mesh = _structure()
    models = _models_without("auger")
    sweep = SweepSpec(contact="anode", start=0.0, stop=0.3, step=0.1)
    path = str(tmp_path / "proj.json")
    save_project(path, "Proj", structure, mesh, ProcessFlow(), sweep, models)

    name, spec, direct_sweep, direct_models = run_config.project_run_inputs(path)
    started = run_config.configure_run(spec, sweep=direct_sweep, models=direct_models)
    assert started.models["auger"] is False and started.sweep.contact == "anode"

    proj = _call("project.spec", {"path": path})["result"]
    assert proj["name"] == "Proj" and proj["models"] == models
    assert proj["sweep"] == _json(sweep.to_dict())
    out = _call("spec.configure_run", {"spec": proj["spec"], "run": {
        "sweep": proj["sweep"], "models": proj["models"]}})
    assert out["result"] == _json(started.to_dict())


def test_a_partial_models_config_is_merged_correctly(tmp_path):
    """A save from another build: a key missing, an unknown one present.
    The Physics Lab config merge (run_config.merged_models, via
    project_run_inputs) merges onto the defaults and drops the unknown
    key; the project's run must be the same through the RPC."""
    structure, mesh = _structure()
    path = str(tmp_path / "partial.json")
    save_project(path, "Partial", structure, mesh, ProcessFlow(), None,
                 {"auger": False, "a_future_model": True})
    name, spec, sweep, models = run_config.project_run_inputs(path)
    started = run_config.configure_run(spec, models=models)

    proj = _call("project.spec", {"path": path})["result"]
    assert "a_future_model" not in proj["models"]
    assert set(proj["models"]) == set(ModelCatalog.default_config())
    out = _call("spec.configure_run", {"spec": proj["spec"], "run": {"models": proj["models"]}})
    assert out["result"] == _json(started.to_dict())


def test_a_project_without_models_keeps_the_catalog_defaults(tmp_path):
    structure, mesh = _structure()
    path = str(tmp_path / "old.json")
    save_project(path, "Old", structure, mesh)          # models null, as pre-v5
    proj = _call("project.spec", {"path": path})["result"]
    assert proj["models"] is None and proj["sweep"] is None
    assert proj["spec"] == _json(structure.to_device_spec(mesh).to_dict())


def test_a_flow_only_project_is_refused_named(tmp_path):
    flow = ProcessFlow()
    flow.add_step(ProcessStep(id="s1", name="Substrate", operation="substrate",
                              parameters={"doping_cm3": 1e16, "type": "p"}))
    path = str(tmp_path / "flow.json")
    save_project(path, "Flow", None, None, flow)
    err = _call("project.spec", {"path": path})["error"]
    assert err["data"]["type"] == "RunConfigError"
    assert err["data"]["title"] == "Nothing to run"
    assert "process flow only" in err["data"]["detail"]


def test_an_invalid_project_is_refused_matching_a_direct_call(tmp_path):
    structure, mesh = _structure()
    mesh.nx = 1                                          # "Mesh Nx must be at least 2"
    path = str(tmp_path / "bad.json")
    save_project(path, "Bad", structure, mesh)
    with pytest.raises(RunConfigError) as exc:
        run_config.project_run_inputs(path)
    err = _call("project.spec", {"path": path})["error"]
    assert (err["data"]["title"], err["data"]["detail"]) == (exc.value.title, exc.value.detail)


# -- loaders and params ---------------------------------------------------------

def test_spec_load_and_from_example_equal_the_direct_calls(tmp_path):
    spec = examples.EXAMPLES["mosfet_2d"]()
    path = str(tmp_path / "job.json")
    spec.to_json(path)
    assert _call("spec.load", {"path": path})["result"] == _json(
        DeviceSpec.from_json(path).to_dict())
    assert _call("spec.from_example", {"name": "mosfet_2d"})["result"] == _json(spec.to_dict())
    assert _call("spec.load", {"path": str(tmp_path / "none.json")})["error"]["data"]["type"] \
        == "FileNotFoundError"


@pytest.mark.parametrize("method,params", [
    ("spec.configure_run", {"spec": [1]}),
    ("spec.configure_run", {"spec": None}),
    ("spec.configure_run", {"run": {}}),
    ("spec.configure_run", "SPEC"),
    ("run.options", {"spec": 3}),
    ("spec.load", {"path": 3}),
    ("project.spec", {}),
])
def test_bad_params_are_invalid_params(method, params):
    assert _call(method, params)["error"]["code"] == server.INVALID_PARAMS


@pytest.mark.parametrize("run,code,needle", [
    ({"bogus": 1}, server.INVALID_PARAMS, "unknown run keys"),
    ({"equilibrium_only": "yes"}, server.INVALID_PARAMS, "equilibrium_only"),
    ({"sweep": [0, 1]}, server.INVALID_PARAMS, "'sweep'"),
    ({"models": ["auger"]}, server.INVALID_PARAMS, "models"),
    ({"engine": "warp"}, server.APPLICATION_ERROR, "unknown engine 'warp'"),
    ({"backend": "spice"}, server.APPLICATION_ERROR, "unknown backend 'spice'"),
    ({"models": {"auger": "no"}}, server.APPLICATION_ERROR, ""),
])
def test_a_malformed_run_is_refused_named(run, code, needle):
    base = examples.EXAMPLES["diode_1d"]().to_dict()
    err = _call("spec.configure_run", {"spec": base, "run": run})["error"]
    assert err["code"] == code and needle in err["message"], err


def test_the_methods_are_listed():
    names = _call("system.methods")["result"]
    for m in ("spec.from_example", "spec.load", "project.spec", "run.options",
              "spec.configure_run", "spec.job_text"):
        assert m in names


# -- latency (section 17.2: each method's latency is measured) ------------------

@pytest.mark.timing
def test_round_trip_latency_through_the_real_service(tmp_path):
    """Median round trip of each job method through a real service
    process, after warm-up. The bound is loose (a Run click must not wait
    visibly); the measured values are recorded in the plan."""
    structure, mesh = _structure()
    proj = str(tmp_path / "p.json")
    save_project(proj, "P", structure, mesh, ProcessFlow(), None, ModelCatalog.default_config())
    proc = subprocess.Popen([sys.executable, "-m", "backend_service"], cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, encoding="utf-8")
    try:
        def call(method, params):
            proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 1, "method": method,
                                         "params": params}) + "\n")
            proc.stdin.flush()
            return json.loads(proc.stdout.readline())

        spec3d = call("spec.from_example", {"name": "resistor_3d"})["result"]
        cases = {"spec.from_example": {"name": "diode_1d"},
                 "project.spec": {"path": proj},
                 "run.options": {"spec": spec3d},
                 "spec.configure_run": {"spec": spec3d, "run": {"engine": "direct"}}}
        medians = {}
        for method, params in cases.items():
            assert "result" in call(method, params)          # warm-up (imports)
            times = []
            for _ in range(15):
                t0 = time.perf_counter()
                call(method, params)
                times.append((time.perf_counter() - t0) * 1e3)
            medians[method] = statistics.median(times)
        print("P3-S2 round-trip medians (ms):",
              {k: round(v, 2) for k, v in medians.items()})
        assert all(v < 250.0 for v in medians.values()), medians
    finally:
        proc.stdin.close()
        proc.wait(timeout=30)

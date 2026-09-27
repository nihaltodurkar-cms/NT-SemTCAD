"""NATIVE-DESKTOP-PLAN.md 18.1/18.2, GUI layer: RemoteJobRunner builds
the same hardened argv shapes as workbench.remote_executor.SSHTransport
(tests/test_remote_hardening.py) -- "--" before the target on every
stage, and a shell-quoted command string for "mkdir"/"run". Gated here
directly against `_argv_for_stage` (no process actually started), plus
StudyController.setRemoteHosts's per-host error reporting.
"""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import pytest
from PySide6.QtGui import QGuiApplication

from gui import app as gui_app
from gui.services.remote_job_runner import RemoteJobRunner
from workbench.remote_executor import RemoteHost, quote


@pytest.fixture(scope="module")
def gapp():
    yield QGuiApplication.instance() or QGuiApplication([])


def _runner(gapp, **host_kwargs):
    host = RemoteHost(host="lab-1", **host_kwargs)
    return RemoteJobRunner(host, work_dir=None)


@pytest.mark.parametrize("stage,expect_target_right_after", [
    ("mkdir", True), ("run", True), ("push", False), ("pull", False),
])
def test_every_stage_has_double_dash_before_the_target(gapp, stage, expect_target_right_after):
    r = _runner(gapp)
    r._job_path = "/local/job.json"
    r.result_path = "/local/result.npz"
    r._remote_job = "/tmp/pytcad-remote/job-x.json"
    r._remote_out = "/tmp/pytcad-remote/result-x.npz"
    argv = r._argv_for_stage(stage)
    assert "--" in argv
    dash = argv.index("--")
    if expect_target_right_after:
        assert argv[dash + 1] == "lab-1"
    else:
        # push/pull: the target is embedded in "lab-1:path", not standalone.
        assert any(a.startswith("lab-1:") for a in argv[dash + 1:])


def test_mkdir_and_run_commands_are_shell_quoted(gapp):
    r = _runner(gapp, remote_workdir="/tmp/a b", python="/usr/bin/python 3")
    r._remote_job = "/tmp/a b/job-x.json"
    r._remote_out = "/tmp/a b/result-x.npz"

    mkdir_argv = r._argv_for_stage("mkdir")
    assert mkdir_argv[-1] == "mkdir -p " + quote("/tmp/a b")

    run_argv = r._argv_for_stage("run")
    expect = (f"{quote('/usr/bin/python 3')} -m gui.services.solver_runner "
             f"{quote('/tmp/a b/job-x.json')} {quote('/tmp/a b/result-x.npz')}")
    assert run_argv[-1] == expect


# ----------------------------------------------------------------------
#  RemoteHost validation surfaces through StudyController, per host
# ----------------------------------------------------------------------
def test_set_remote_hosts_reports_a_hostile_host_and_keeps_the_good_ones(gapp):
    engine, controller = gui_app.create_engine(gapp)
    study = controller.studyManager
    errors = []
    controller.errorRaised.connect(lambda t, d: errors.append((t, d)))

    study.setRemoteHosts(["lab-1", "-oProxyCommand=touch pwned", "lab-2"])
    assert study.remoteHosts == ["lab-1", "lab-2"]
    assert len(errors) == 1
    assert errors[0][0] == "Invalid remote host"
    assert "-oProxyCommand" in errors[0][1]

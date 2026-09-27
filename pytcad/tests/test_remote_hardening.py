"""NATIVE-DESKTOP-PLAN.md 18.1/18.2: the two remote-execution
vulnerabilities found while planning P3-S8, fixed in `workbench.
remote_executor` (`RemoteHost`, `SSHTransport`) and
`gui.services.remote_job_runner` (`RemoteJobRunner`):

  1. SSH argument injection via the host string (a host starting with
     '-', e.g. "-oProxyCommand=...", is parsed by ssh/scp as a LOCAL
     option, not a hostname -- reproduced by hand before this fix).
  2. Remote shell injection via unquoted paths (`host.remote_workdir`/
     `host.python` dropped unquoted into a command string the remote
     shell parses).

Gated here at the library (`SSHTransport`) layer; `RemoteJobRunner`'s
own equivalent argv is gated in gui/tests/test_remote_job_runner_
hardening.py, against the same fake_ssh.py/fake_scp.py fixtures.
"""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
PYTCAD_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, PYTCAD_ROOT)

import pytest

from workbench.remote_executor import CommandResult, RemoteExecutor, RemoteHost, SSHTransport, quote

FIXTURES_DIR = os.path.join(PYTCAD_ROOT, "gui", "tests", "fixtures")
_FAKE_SSH = (sys.executable, os.path.join(FIXTURES_DIR, "fake_ssh.py"))
_FAKE_SCP = (sys.executable, os.path.join(FIXTURES_DIR, "fake_scp.py"))


# ----------------------------------------------------------------------
#  RemoteHost validation (finding 1)
# ----------------------------------------------------------------------
@pytest.mark.parametrize("bad_host", [
    "-oProxyCommand=touch pwned",
    "-oProxyCommand=touch%20pwned",
    "-x",
    "",
    "has space",
    "has\ttab",
    "has\nnewline",
])
def test_remotehost_rejects_a_hostile_or_malformed_host(bad_host):
    with pytest.raises(ValueError):
        RemoteHost(host=bad_host)


@pytest.mark.parametrize("bad_user", ["-oProxyCommand=touch pwned", "has space"])
def test_remotehost_rejects_a_hostile_or_malformed_user(bad_user):
    with pytest.raises(ValueError):
        RemoteHost(host="lab-1", user=bad_user)


@pytest.mark.parametrize("good_host", [
    "lab-1", "192.168.1.10", "worker.example.com", "fe80::1", "a-b_c.d",
])
def test_remotehost_accepts_ordinary_hosts(good_host):
    RemoteHost(host=good_host)  # must not raise


def test_remote_workdir_and_python_are_not_character_restricted():
    """Unlike host/user, these are quoted, not validated -- any
    character a real path can hold is legal (18.2)."""
    RemoteHost(host="lab-1", remote_workdir="/tmp/a b;touch pwned",
              python="/usr/bin/python 3")


# ----------------------------------------------------------------------
#  SSHTransport argv shape: "--" before the target (defense in depth)
# ----------------------------------------------------------------------
class _RecordingRun:
    """Captures the argv `subprocess.run` would have received, without
    actually starting `ssh`/`scp` (no real network)."""

    def __init__(self):
        self.calls = []

    def __call__(self, argv, **kwargs):
        self.calls.append(list(argv))
        class _P:
            returncode = 0
            stdout = ""
            stderr = ""
        return _P()


def test_ssh_argv_has_double_dash_before_the_target(monkeypatch):
    rec = _RecordingRun()
    monkeypatch.setattr("workbench.remote_executor.subprocess.run", rec)
    t = SSHTransport()
    host = RemoteHost(host="lab-1")
    t.run(["mkdir", "-p", "/tmp/x"], host)
    t.push("/local/job.json", "/tmp/x/job.json", host)
    t.pull("/tmp/x/result.npz", "/local/result.npz", host)
    assert len(rec.calls) == 3
    run_argv, push_argv, pull_argv = rec.calls
    dash = run_argv.index("--")
    assert run_argv[dash + 1] == "lab-1"  # ssh's target, right after "--"
    dash = push_argv.index("--")
    assert push_argv[dash + 1:] == ["/local/job.json", "lab-1:/tmp/x/job.json"]
    dash = pull_argv.index("--")
    assert pull_argv[dash + 1:] == ["lab-1:/tmp/x/result.npz", "/local/result.npz"]


def test_ssh_run_quotes_each_argv_token_into_one_command_string(monkeypatch):
    rec = _RecordingRun()
    monkeypatch.setattr("workbench.remote_executor.subprocess.run", rec)
    t = SSHTransport()
    host = RemoteHost(host="lab-1", remote_workdir="/tmp/a b")
    t.run(["mkdir", "-p", host.remote_workdir], host)
    argv = rec.calls[0]
    command = argv[argv.index("--") + 2]
    assert command == "mkdir -p " + quote("/tmp/a b")
    assert command == "mkdir -p '/tmp/a b'"


# ----------------------------------------------------------------------
#  End to end against the real fixtures: quoting actually round-trips,
#  not just "doesn't crash" -- a hostile remote_workdir never runs its
#  injected command.
# ----------------------------------------------------------------------
def test_a_workdir_with_a_space_and_a_shell_metacharacter_round_trips(tmp_path):
    import numpy as np
    from workbench.adapters.spec import spec_from_domain
    from workbench.core.templates import get_template

    workdir = tmp_path / "remote wd; touch pwned"
    workdir.mkdir()
    host = RemoteHost(host="lab-1", python=sys.executable,
                      remote_workdir=str(workdir))
    transport = SSHTransport(ssh_cmd=_FAKE_SSH, scp_cmd=_FAKE_SCP)
    executor = RemoteExecutor([host], transport=transport)

    device = get_template("resistor").build({"nx": 16, "ny": 6})
    spec = spec_from_domain(device)
    spec.bias = None
    spec.sweep = None
    job_path = str(tmp_path / "job.json")
    out_path = str(tmp_path / "result.npz")
    spec.to_json(job_path)

    outcomes = executor.run_jobs([(job_path, out_path)])
    assert len(outcomes) == 1 and outcomes[0].error is None, outcomes[0]
    assert os.path.isfile(out_path)
    with np.load(out_path) as npz:
        assert "field__potential" in npz.files
    # The injected "touch pwned" must never have run, on the remote
    # "shell" (fake_ssh's own subprocess) or anywhere else.
    assert not (tmp_path / "pwned").exists()
    assert not (workdir / "pwned").exists()

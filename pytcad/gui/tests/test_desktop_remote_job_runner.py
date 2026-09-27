"""NATIVE-DESKTOP-PLAN.md section 18 (P3-S8) gates for the native app's
C++ RemoteJobRunner (desktop/src/run/remote_job_runner.{hpp,cpp}), run
by the Qt Test executable tcad_desktop_remote_tests
(desktop/tests/test_remote_job_runner.cpp).

Against gui/tests/fixtures/fake_ssh.py/fake_scp.py/fake_ssh_always_fail.py
-- the same loopback stand-ins the Python RemoteJobRunner's own tests
use, no real network:
- a hostile host string (a leading '-', embedded whitespace) is
  refused before any process starts;
- every stage's argv has "--" before the target, and the mkdir/run
  commands are one shell-quoted string (18.1/18.2);
- the full mkdir->push->run->pull chain reaches a real, readable result;
- a remote_workdir with a space and a shell metacharacter round-trips
  correctly -- the injected command never runs;
- a bad host (fake_ssh_always_fail.py) fails named, never a hang;
- cancel mid-run kills the live process at once, no leftover job file;
- RemoteJobRunner is usable through the RunnerBase pointer/signals
  JobRunner also implements (18.3's polymorphism decision).

Skipped when the app is not built. Needs no display: the runner is Qt
Core only.
"""
import json
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

BUILD = os.path.join(ROOT, "build", "desktop")
MANIFEST = os.path.join(BUILD, "desktop_runtime.json")
FIXTURES_DIR = os.path.join(ROOT, "gui", "tests", "fixtures")

pytestmark = pytest.mark.skipif(
    not os.path.isfile(MANIFEST),
    reason="native desktop app not built (powershell -File desktop\\build.ps1)")


def _write_job(d):
    from gui.services.examples import EXAMPLES
    spec = EXAMPLES["diode_1d"]()
    spec.bias = None
    spec.sweep = None
    spec.to_json(os.path.join(d, "job.json"))


def test_remote_job_runner(tmp_path):
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    assert manifest["backend_python"], "build.ps1 found no tcad-dev interpreter"
    assert "remote_tests" in manifest["tools"], "rebuild: desktop_runtime.json predates P3-S8"
    data = tmp_path / "remote_data"
    data.mkdir()
    _write_job(str(data))
    env = {k: v for k, v in os.environ.items()
          if k not in ("PYTHONIOENCODING", "PYTHONUTF8", "PYTHONUNBUFFERED")}
    env["PATH"] = manifest["runtime_bin"] + os.pathsep + env.get("PATH", "")
    env.update({
        "TCAD_TEST_PYTHON": manifest["backend_python"],
        "TCAD_TEST_ROOT": manifest["backend_root"],
        "TCAD_TEST_DATA": str(data),
        "TCAD_TEST_FAKE_SSH": os.path.join(FIXTURES_DIR, "fake_ssh.py"),
        "TCAD_TEST_FAKE_SCP": os.path.join(FIXTURES_DIR, "fake_scp.py"),
        "TCAD_TEST_FAKE_SSH_FAIL": os.path.join(FIXTURES_DIR, "fake_ssh_always_fail.py"),
    })
    report = tmp_path / "remote.txt"
    out = subprocess.run([os.path.join(BUILD, manifest["tools"]["remote_tests"]), "-o", f"{report},txt"],
                        capture_output=True, text=True, env=env, timeout=600)
    text = report.read_text(encoding="utf-8") if report.exists() else out.stdout + out.stderr
    assert out.returncode == 0 and ", 0 failed," in text, text[-6000:]

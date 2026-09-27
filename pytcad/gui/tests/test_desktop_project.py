"""NATIVE-DESKTOP-PLAN.md P4 S9 gate (section 20.4): runs the C++ Qt Test
binary tcad_desktop_project_tests (desktop/tests/test_project.cpp) --
ProjectController driven against the REAL backend_service subprocess,
exercising project.load/project.save end to end (save -> load round
trip, a missing-file error, the schema-6-only downgrade refusal, and
newProject()'s reset).

Skipped (not failed) when the desktop app has not been built -- build
with `powershell -File desktop\\build.ps1` (plan section 7).
"""
import json
import os
import subprocess

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build", "desktop")
MANIFEST = os.path.join(BUILD, "desktop_runtime.json")

pytestmark = pytest.mark.skipif(
    not os.path.isfile(MANIFEST),
    reason="native desktop app not built (powershell -File desktop\\build.ps1)")


def test_project_controller_pass(tmp_path):
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    assert manifest["backend_python"], "build.ps1 found no tcad-dev interpreter"
    env = {k: v for k, v in os.environ.items()
           if k not in ("PYTHONIOENCODING", "PYTHONUTF8", "TCAD_BACKEND_DEBUG",
                        "QT_QPA_PLATFORM")}
    env["PATH"] = manifest["runtime_bin"] + os.pathsep + env.get("PATH", "")
    env.update({
        "TCAD_TEST_PYTHON": manifest["backend_python"],
        "TCAD_TEST_ROOT": manifest["backend_root"],
    })
    report = tmp_path / "project.txt"
    exe = os.path.join(BUILD, manifest["tools"]["project_tests"])
    out = subprocess.run([exe, "-o", f"{report},txt"], capture_output=True, text=True,
                         env=env, timeout=120)
    text = report.read_text(encoding="utf-8") if report.exists() else out.stdout + out.stderr
    assert out.returncode == 0 and ", 0 failed," in text, text

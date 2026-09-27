"""NATIVE-DESKTOP-PLAN.md section 21 gate: runs the C++ Qt Test binary
tcad_desktop_build_shell_tests (desktop/tests/test_build_shell.cpp) --
the real MainWindow's Build dock (S1-S8's editors, the S9 ProjectController,
and the S8 UndoStack, finally assembled) against the real backend service:
New/Open/Save project, mesh/region edits pushed as undoable commands, and
live structure validation.

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


def test_build_shell_pass(tmp_path):
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    assert manifest["backend_python"], "build.ps1 found no tcad-dev interpreter"
    env = {k: v for k, v in os.environ.items()
           if k not in ("PYTHONIOENCODING", "PYTHONUTF8", "TCAD_BACKEND_DEBUG",
                        "QT_QPA_PLATFORM")}
    env["PATH"] = manifest["runtime_bin"] + os.pathsep + env.get("PATH", "")
    report = tmp_path / "build_shell.txt"
    exe = os.path.join(BUILD, manifest["tools"]["build_shell_tests"])
    out = subprocess.run([exe, "-o", f"{report},txt"], capture_output=True, text=True,
                         env=env, timeout=180)
    text = report.read_text(encoding="utf-8") if report.exists() else out.stdout + out.stderr
    assert out.returncode == 0 and ", 0 failed," in text, text

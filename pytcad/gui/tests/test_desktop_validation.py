"""NATIVE-DESKTOP-PLAN.md P4 S8 gate (section 20.4): runs the C++ Qt Test
binary tcad_desktop_validation_tests (desktop/tests/test_validation.cpp)
-- ValidationPanel plus the structure/process error-message formatting
(document/validation.hpp) matching app_controller.py's
structureValidationErrors/processValidationErrors exactly.

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


def test_validation_panel_pass(tmp_path):
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    env = {k: v for k, v in os.environ.items() if k != "QT_QPA_PLATFORM"}
    env["PATH"] = manifest["runtime_bin"] + os.pathsep + env.get("PATH", "")
    report = tmp_path / "validation.txt"
    exe = os.path.join(BUILD, manifest["tools"]["validation_tests"])
    out = subprocess.run([exe, "-o", f"{report},txt"], capture_output=True, text=True,
                         env=env, timeout=120)
    text = report.read_text(encoding="utf-8")
    assert out.returncode == 0, text
    assert ", 0 failed," in text, text

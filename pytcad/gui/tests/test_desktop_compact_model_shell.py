"""NATIVE-DESKTOP-PLAN.md section 25 gate: runs the C++ Qt Test binary
tcad_desktop_compact_model_shell_tests (desktop/tests/test_compact_model_shell.cpp)
-- the real MainWindow's Compact Model dock against a real
`python -m gui.services.compact_runner` subprocess (M38 Phase 4), for
both the diode and n-MOSFET extraction kinds.

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


def test_compact_model_shell_pass(tmp_path):
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    assert manifest["backend_python"], "build.ps1 found no tcad-dev interpreter"
    env = {k: v for k, v in os.environ.items()
           if k not in ("PYTHONIOENCODING", "PYTHONUTF8", "TCAD_BACKEND_DEBUG",
                        "QT_QPA_PLATFORM")}
    env["PATH"] = manifest["runtime_bin"] + os.pathsep + env.get("PATH", "")
    report = tmp_path / "compact_model_shell.txt"
    exe = os.path.join(BUILD, manifest["tools"]["compact_model_shell_tests"])
    out = subprocess.run([exe, "-o", f"{report},txt"], capture_output=True, text=True,
                         env=env, timeout=600)
    text = report.read_text(encoding="utf-8") if report.exists() else out.stdout + out.stderr
    assert out.returncode == 0 and ", 0 failed," in text, text

"""NATIVE-DESKTOP-PLAN.md 27: desktop/tools/check_no_qt.py, the P6 hard gate that a TCAD tree contains zero Qt.

Runs on fake trees (cross-platform). NOT run here, and not claimed: the check against a real dist\\TCAD -- that needs the
Windows build. Until the Qt purge lands, a real stage FAILS this gate by design; it is not yet wired into stage.ps1.
"""
import json
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "desktop", "tools", "check_no_qt.py")


def _run(tree):
    p = subprocess.run([sys.executable, SCRIPT, str(tree)], capture_output=True, text=True)
    return p.returncode, p.stdout


def _clean(tmp_path):
    t = tmp_path / "TCAD"
    (t / "runtime" / "conda-meta").mkdir(parents=True)
    (t / "licenses").mkdir()
    (t / "tcad_desktop.exe").write_bytes(b"MZ kernel32.dll user32.dll vtkRenderingCore-9.7.dll")
    (t / "runtime" / "python.exe").write_bytes(b"MZ python")
    (t / "runtime" / "conda-meta" / "numpy-2.5.3-py314_0.json").write_text(json.dumps({"name": "numpy", "depends": ["python", "libblas"]}))
    (t / "licenses" / "manifest.json").write_text(json.dumps({"components": [{"name": "numpy"}]}))
    return t


def test_clean_tree_passes(tmp_path):
    code, out = _run(_clean(tmp_path))
    assert code == 0 and "NO Qt found" in out


@pytest.mark.parametrize("mutate,check", [
    (lambda t: (t / "Qt6Core.dll").write_bytes(b"MZ"), "file-name"),
    (lambda t: (t / "qtadvanceddocking-qt6.dll").write_bytes(b"MZ"), "file-name"),
    (lambda t: (t / "qt.conf").write_text("[Paths]"), "file-name"),
    (lambda t: ((t / "platforms").mkdir(), (t / "platforms" / "qwindows.dll").write_bytes(b"MZ")), "plugin-dir"),
    (lambda t: ((t / "imageformats").mkdir(), (t / "imageformats" / "anything.dll").write_bytes(b"MZ")), "plugin-dir"),
    (lambda t: (t / "renamed.dll").write_bytes(b"MZ ... Qt6Widgets.dll ..."), "binary-content"),
    (lambda t: (t / "vtkThing.dll").write_bytes(b"MZ vtkGUISupportQt-9.7.dll"), "binary-content"),
    (lambda t: (t / "runtime" / "conda-meta" / "qt6-main-6.11.2-h0_0.json").write_text(json.dumps({"name": "qt6-main"})), "conda-package"),
    (lambda t: (t / "runtime" / "conda-meta" / "vtk-9-h0_0.json").write_text(json.dumps({"name": "vtk-base", "depends": ["qt6-main >=6.11"]})), "conda-depends"),
    (lambda t: (t / "licenses" / "manifest.json").write_text(json.dumps({"components": [{"name": "qt6-advanced-docking-system"}]})), "licence-manifest"),
    (lambda t: (t / "runtime" / "shiboken6.pyd").write_bytes(b"MZ"), "file-name"),
], ids=["Qt6Core", "ADS", "qt.conf", "platforms", "imageformats", "renamed-import", "vtk-qt", "conda-qt", "conda-depends",
        "manifest", "shiboken"])
def test_each_kind_of_qt_evidence_fails(tmp_path, mutate, check):
    t = _clean(tmp_path)
    mutate(t)
    code, out = _run(t)
    assert code == 1 and f"[{check}]" in out, out


def test_missing_and_empty_trees_do_not_pass(tmp_path):
    assert _run(tmp_path / "nope")[0] == 2
    (tmp_path / "empty").mkdir()
    assert _run(tmp_path / "empty")[0] == 2


def test_json_report_lists_every_hit(tmp_path):
    t = _clean(tmp_path)
    (t / "Qt6Gui.dll").write_bytes(b"MZ")
    (t / "x.dll").write_bytes(b"MZ Qt6Network.dll")
    rep = tmp_path / "r.json"
    p = subprocess.run([sys.executable, SCRIPT, str(t), "--json", str(rep)], capture_output=True, text=True)
    assert p.returncode == 1
    hits = json.load(open(rep))["hits"]
    assert {h["path"] for h in hits} >= {"Qt6Gui.dll", "x.dll"}


def test_not_wired_into_the_stage_yet_and_says_so():
    stage = open(os.path.join(ROOT, "desktop", "tools", "stage.ps1"), encoding="utf-8").read()
    assert "check_no_qt.py" not in stage        # wiring it in is part of the Qt purge itself (plan 27), not before

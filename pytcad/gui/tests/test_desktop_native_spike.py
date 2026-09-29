"""NATIVE-DESKTOP-PLAN.md 27.4: static gates on the Qt-purge spike (Win32 host for the VTK field view).

What is checked HERE (Linux, no compiler, no VTK): that the new code is Qt-free, that the CMake structure keeps
every Qt reference inside `if(TCAD_LEGACY_QT)`, that the FieldView -> FieldScene split lost no API, and that the
Windows gate script has its steps. NOT checked here, and not claimed: that any of it COMPILES, or that the field
view displays -- that is desktop/tools/verify_native_spike.ps1, run by the user on Windows.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DESK = os.path.join(ROOT, "desktop")
SRC = os.path.join(DESK, "src")


def _read(*p):
    with open(os.path.join(*p), "r", encoding="utf-8") as fh:
        return fh.read()


def _files(sub, exts=(".cpp", ".hpp", ".h")):
    out = []
    for base, _d, fs in os.walk(os.path.join(SRC, sub)):
        out += [os.path.join(base, f) for f in fs if f.endswith(exts)]
    return out


INCLUDE_RE = re.compile(r'#\s*include\s*[<"](Q[A-Za-z]|QVTK)')
QT_TOKEN = re.compile(r'\bQ[A-Z][A-Za-z]+\b|\bQ_OBJECT\b|\bemit\b|Qt6::|qtadvanceddocking|GUISupportQt')


def _strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def test_native_and_scene_sources_have_no_qt():
    scene = [os.path.join(SRC, "views", "field", n) for n in ("field_scene.hpp", "field_scene.cpp", "field_scene_3d.cpp")]
    for f in _files("native") + scene:
        code = _strip_comments(_read(f))
        # string literals may NAME Qt (the spike's runtime self-check looks for Qt module names); code may not
        m = INCLUDE_RE.search(code) or QT_TOKEN.search(re.sub(r'"(\\.|[^"\\])*"', '""', code))
        assert not m, f"{os.path.relpath(f, DESK)}: Qt reference {m.group()!r}"


def test_scene_left_no_fieldview_names_behind():
    for n in ("field_scene.hpp", "field_scene.cpp", "field_scene_3d.cpp"):
        code = _strip_comments(_read(SRC, "views", "field", n))
        assert "FieldView" not in code, n


def _cmake_blocks():
    """(line, text, inside_legacy) for every CMake line, tracking if(TCAD_LEGACY_QT) ... endif() nesting."""
    depth, legacy_depth, out = 0, None, []
    for i, line in enumerate(_read(DESK, "CMakeLists.txt").splitlines(), 1):
        code = line.split("#")[0]
        if re.match(r"\s*if\s*\(", code, re.I):
            depth += 1
            if re.search(r"if\s*\(\s*TCAD_LEGACY_QT\s*\)", code, re.I) and legacy_depth is None:
                legacy_depth = depth
        out.append((i, code, legacy_depth is not None))
        if re.match(r"\s*endif\s*\(", code, re.I):
            if legacy_depth == depth:
                legacy_depth = None
            depth -= 1
    return out


def test_every_qt_reference_in_cmake_is_inside_the_legacy_block():
    outside = [(i, c.strip()) for i, c, legacy in _cmake_blocks()
               if not legacy and re.search(r"Qt6|qtadvanceddocking|ads::|GUISupportQt|AUTOMOC", c)]
    assert not outside, outside


def test_native_targets_link_no_qt_and_the_scene_library_is_shared():
    text = _read(DESK, "CMakeLists.txt")
    spike = re.search(r"add_executable\(tcad_native_spike.*?vtk_module_autoinit\(TARGETS tcad_native_spike[^\n]*\)", text, re.S).group()
    assert "Qt" not in spike and "ads::" not in spike and "GUISupportQt" not in spike
    assert "cxx_std_23" in spike
    assert "field_scene.cpp" in re.search(r"add_library\(tcad_field_scene.*?\)", text, re.S).group()
    assert "tcad_field_scene" in re.search(r"add_library\(tcad_desktop_ui.*?vtk_module_autoinit\(TARGETS tcad_desktop_ui", text, re.S).group()
    # the VTK component list of the Qt-free build must not name a Qt module
    first = re.search(r"find_package\(VTK 9 REQUIRED COMPONENTS(.*?)\)", text, re.S).group(1)
    assert "Qt" not in first and "RenderingUI" in first and "IOImage" in first


def test_fieldview_split_kept_every_public_name():
    scene = _read(SRC, "views", "field", "field_scene.hpp")
    view = _read(SRC, "views", "field", "field_view.hpp")
    for name in ("setResult", "setField", "setLogScale", "setContours", "setMeshLines", "resetView", "setViewPreset",
                 "setIsosurface", "setVolume", "setSnapshot", "setCutLine", "setExploded", "renderNow", "applyTheme"):
        assert re.search(rf"\b{name}\s*\(", scene), name
    # the four QString-typed entry points keep their old names on the Qt wrapper, over std::string ones on the scene
    for qt_name, scene_name in (("readoutAt", "readoutAtStr"), ("readoutForPoint", "readoutForPointStr"),
                                ("readoutForNode", "readoutForNodeStr"), ("explodedAvailable", "explodedAvailableStr")):
        assert re.search(rf"\b{qt_name}\s*\(", view) and re.search(rf"\b{scene_name}\s*\(", scene)
    assert "class FieldView : public QVTKOpenGLNativeWidget, public FieldScene" in view
    assert "sceneWindow" in scene and "renderWindow()" not in _strip_comments(scene)   # QVTK's name must stay unambiguous
    for sig in ("readoutChanged(const QString& text)", "displayChanged()"):
        assert sig in view


def test_every_original_emit_became_a_notification():
    for n in ("field_scene.cpp", "field_scene_3d.cpp"):
        code = _read(SRC, "views", "field", n)
        assert "emit " not in _strip_comments(code)
    assert _read(SRC, "views", "field", "field_scene_3d.cpp").count("notifyDisplayChanged();") >= 10


def test_native_host_uses_vtk_win32_not_qvtk():
    host = _read(SRC, "native", "vtk_win32_host.cpp")
    assert "vtkWin32OpenGLRenderWindow" in host and "vtkWin32RenderWindowInteractor" in host
    assert "SetParentId" in host and "Initialize()" in host


def test_manifest_declares_per_monitor_v2_dpi():
    m = _read(SRC, "native", "tcad_native.manifest")
    assert "PerMonitorV2" in m and "UTF-8" in m


def test_gate_script_has_all_four_steps_and_scrubs_path():
    s = _read(DESK, "tools", "verify_native_spike.ps1")
    assert "-NoLegacyQt" in s and "check_no_qt.py" in s and "--screenshot" in s and "dumpbin" in s
    assert "windeployqt" not in s.replace("NO windeployqt", "")          # the stage must never call it
    assert '$env:PATH = "$env:SystemRoot\\System32;$env:SystemRoot"' in s   # run with the Windows dirs only
    assert "qt_modules_loaded" in s


def test_build_script_has_the_qt_free_mode():
    b = _read(DESK, "build.ps1")
    assert "[switch] $NoLegacyQt" in b and "TCAD_LEGACY_QT=$legacy" in b and "desktop-native" in b

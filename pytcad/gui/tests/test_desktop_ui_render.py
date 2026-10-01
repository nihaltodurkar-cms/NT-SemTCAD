"""NATIVE-DESKTOP-PLAN.md 27.7, N2a + N2b: the native UI framework's render core (D3D12 + D3D11On12 + Direct2D/
DirectWrite into a DXGI flip-model swap chain, WIC readback) and its widget tree, painter and layouts.

The gate itself is the C++ binary `tcad_ui_render_tests` (WARP goldens at 100/150/200% compared exactly, device-loss
recovery, 1000 resize/DPI frames without leaks, a clean D3D12/DXGI debug layer in a child process, a GPU smoke
check). This file runs it when it has been built (`desktop\\build.ps1 -NoLegacyQt`) and pins, statically, what must
not regress: no Qt anywhere in the framework, and the two D3D12 facts the first Windows run taught.
"""
import json
import os
import re
import subprocess

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DESK = os.path.join(ROOT, "desktop")
UI_SRC = os.path.join(DESK, "src", "ui")
UI_TESTS = os.path.join(DESK, "tests", "ui")
EXE = os.path.join(ROOT, "build", "desktop-native", "tcad_ui_render_tests.exe")
CORE_EXE = os.path.join(ROOT, "build", "desktop-native", "tcad_ui_core_tests.exe")
QT = re.compile(r"#\s*include\s*<Q|\bQ[A-Z][A-Za-z]+\b|\bQt[A-Z0-9]|Q_OBJECT|qtadvanceddocking|QVTK")


def _read(*p):
    with open(os.path.join(*p), "r", encoding="utf-8") as fh:
        return fh.read()


def _sources(*roots):
    out = []
    for root in roots:
        for base, _d, files in os.walk(root):
            out += [os.path.join(base, f) for f in files if f.endswith((".cpp", ".hpp", ".h"))]
    return out


def _strip_comments(code):
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    return re.sub(r"//[^\n]*", "", code)


def test_the_ui_framework_and_its_tests_have_no_qt():
    files = _sources(UI_SRC, UI_TESTS)
    assert len(files) >= 8, files
    for f in files:
        m = QT.search(_strip_comments(_read(f)))
        assert not m, f"{os.path.relpath(f, DESK)}: Qt reference {m.group()!r}"


def test_cmake_n2a_block_is_outside_the_legacy_block_and_links_no_qt():
    text = _read(DESK, "CMakeLists.txt")
    legacy_start = text.index("if(TCAD_LEGACY_QT)\n# -- the application")
    block = re.search(r"# -- N2a: the UI framework's render core.*?target_link_libraries\(tcad_ui_demo[^\n]*\)", text, re.S)
    assert block and block.start() < legacy_start
    code = "\n".join(l.split("#")[0] for l in block.group().splitlines())
    assert "Qt" not in code and "ads::" not in code and "vtk" not in code.lower()
    for lib in ("d3d12", "d3d11", "dxgi", "d2d1", "dwrite", "windowscodecs"):
        assert re.search(rf"\b{lib}\b", code), lib
    assert "add_test(NAME tcad_ui_render_tests" in code


def test_back_buffers_are_wrapped_present_in_and_present_out():
    # D2D is the only thing drawing into a swap-chain buffer, which arrives in PRESENT; declaring RENDER_TARGET as the
    # in-state (as Microsoft's D3D12-first 11on12 sample does) put every D2D clear on a resource in the wrong state --
    # found by the debug-layer test on the first Windows run.
    code = _strip_comments(_read(UI_SRC, "render", "window_surface.cpp"))
    call = re.search(r"CreateWrappedResource\((.*?)\);", code, re.S).group(1)
    assert re.findall(r"D3D12_RESOURCE_STATE_\w+", call) == ["D3D12_RESOURCE_STATE_PRESENT", "D3D12_RESOURCE_STATE_PRESENT"]


def test_one_device_per_adapter_and_clients_release_before_recovery():
    # D3D12CreateDevice returns the existing device for an adapter while anything references it -- even a removed
    # one -- so recovery must make every surface let go first, and a second RenderDevice for the same adapter must
    # be the same object.
    code = _strip_comments(_read(UI_SRC, "render", "render_device.cpp"))
    assert "slot.lock()" in code
    rec = code[code.index("RenderDevice::recover()"):]
    assert rec.index("releaseDeviceObjects()") < rec.index("releaseDevice();") < rec.index("buildDevice()")
    assert "releaseDeviceObjects() override" in _read(UI_SRC, "render", "window_surface.hpp")


def test_goldens_and_their_environment_are_recorded():
    man = json.loads(_read(UI_TESTS, "goldens", "manifest.json"))
    # what the pixels depend on: the rasterizer, D2D, DirectWrite, the UI/monospace/fallback fonts (N2d) and the
    # user's ClearType Tuner values that D2D's default text rendering parameters come from
    assert set(man["environment"]) == {"d3d10warp.dll", "d2d1.dll", "DWrite.dll", "Segoe UI", "Consolas", "Nirmala UI",
                                       "Microsoft YaHei", "Yu Gothic UI", "Segoe UI Emoji", "text rendering params"}
    names = ("n2a_scene", "n2b_panel", "n2d_text", "n2e_edit", "n2f_high_contrast", "n3a_widgets", "n3a_widgets_hc", "n3b_numbers", "n3b_numbers_hc",
             "n3c_combo", "n3c_combo_hc", "n3c_popup", "n3c_popup_hc")
    assert set(man["images"]) == {f"{n}@{p}.png" for n in names for p in (100, 150, 200)}
    canvas = {"n2a_scene": (320, 200), "n2b_panel": (360, 400), "n2d_text": (440, 330), "n2e_edit": (300, 120),
              "n2f_high_contrast": (300, 160), "n3a_widgets": (400, 330), "n3a_widgets_hc": (400, 330),
              "n3b_numbers": (400, 330), "n3b_numbers_hc": (400, 330),
              "n3c_combo": (400, 200), "n3c_combo_hc": (400, 200)}
    # a popup is as big as its content at the scale it opens at, so its size is pinned per scale, not scaled
    popup = {100: (323, 68), 150: (322, 102), 200: (323, 136)}
    for name, dims in man["images"].items():
        assert os.path.getsize(os.path.join(UI_TESTS, "goldens", name)) > 1000
        base, pct = name[:-4].split("@")
        scale = int(pct) / 100
        if base.startswith("n3c_popup"):
            assert (dims["width"], dims["height"]) == popup[int(pct)]
            continue
        w, h = canvas[base]
        assert (dims["width"], dims["height"]) == (round(w * scale), round(h * scale))


@pytest.mark.skipif(not os.path.exists(EXE), reason="tcad_ui_render_tests not built (desktop\\build.ps1 -NoLegacyQt)")
def test_render_core_gate_passes():
    p = subprocess.run([EXE], capture_output=True, text=True, timeout=600)
    out = p.stdout + p.stderr
    if "STALE goldens" in out:
        pytest.skip("goldens were captured on another WARP/D2D/DirectWrite/font build: " +
                    next(l for l in out.splitlines() if "STALE" in l).strip())
    assert p.returncode == 0, out
    assert re.search(r"^74 test\(s\), 0 failed$", out, re.M), out
    for name in ("n2a_scene", "n2b_panel", "n2d_text", "n2e_edit", "n2f_high_contrast", "n3a_widgets", "n3a_widgets_hc", "n3b_numbers", "n3b_numbers_hc",
             "n3c_combo", "n3c_combo_hc", "n3c_popup", "n3c_popup_hc"):
        for pct in (100, 150, 200):
            assert f"{name}@{pct}.png: 0 differing pixels" in out
    assert "child process with --debug-layer exited 0" in out


def test_the_portable_core_has_no_win32():
    for f in _sources(os.path.join(UI_SRC, "core"), os.path.join(UI_SRC, "widgets")):
        code = _strip_comments(_read(f))
        assert not re.search(r"#\s*include\s*<(windows|d2d1|d3d|dwrite|dxgi|wincodec)", code), f
        assert "HWND" not in code and "ComPtr" not in code, f


def test_layouts_build_only_the_measured_subset():
    # 27.7 N2b decision: the subset the Qt panels use, measured -- no alignment flags, no row/column stretch, no
    # extra size policies. A new one must come with a port that needs it (and its tests).
    hdr = _strip_comments(_read(UI_SRC, "core", "widget.hpp"))
    assert re.search(r"enum class SizePolicy \{ Fixed, Minimum, Preferred, Expanding \}", hdr)
    lay = _strip_comments(_read(UI_SRC, "core", "layout.hpp"))
    for absent in ("setAlignment", "setRowStretch", "setColumnStretch", "MinimumExpanding", "Ignored"):
        assert absent not in lay, absent
    # N3a (27.8.1): height-for-width was measured (9 wrapped labels, none in a grid) -- Box, Form and Stack honour it,
    # Grid does not
    for layout in ("BoxLayout", "FormLayout", "StackLayout"):
        body = lay[lay.index(f"class {layout} final"):]
        body = body[: body.index("};")]
        assert "heightForWidthPx" in body, layout
    grid = lay[lay.index("class GridLayout final"):]
    assert "heightForWidthPx" not in grid[: grid.index("};")]


@pytest.mark.skipif(not os.path.exists(CORE_EXE), reason="tcad_ui_core_tests not built (desktop\build.ps1 -NoLegacyQt)")
def test_portable_core_tests_pass():
    p = subprocess.run([CORE_EXE], capture_output=True, text=True, timeout=300)
    assert p.returncode == 0 and re.search(r"^165 test\(s\), 0 failed$", p.stdout, re.M), p.stdout + p.stderr


def test_the_test_driver_never_moves_the_real_cursor_or_types_into_other_windows():
    # N2c: events go through the window procedure as messages; SendInput/SetCursorPos/keybd_event would drive the real
    # machine (the QTest::mouseMove lesson), and a held key must not change a test (the N1 F12 lesson)
    code = _strip_comments(_read(UI_TESTS, "ui_driver.hpp"))
    for banned in ("SendInput", "SetCursorPos", "keybd_event", "mouse_event("):
        assert banned not in code, banned
    assert "SendMessageW" in code and "SetKeyboardState(want)" in code and "SetKeyboardState(keys)" in code
    # mouse messages too: a held Shift turned a synthetic click into a Shift+click (N2e; 12 of 30 runs)
    for fn in ("void move(", "void press(", "void release(", "void wheel("):
        body = code[code.index(fn):]
        body = body[:body.index("    }")]
        assert "withMods(" in body, fn


def test_input_routing_is_portable_and_the_window_forwards_every_input_message():
    router = _strip_comments(_read(UI_SRC, "core", "input_router.cpp")) + _strip_comments(_read(UI_SRC, "core", "input_router.hpp"))
    assert not re.search(r"#\s*include\s*<windows", router) and "HWND" not in router
    win = _strip_comments(_read(DESK, "src", "platform", "window.cpp"))
    assert "case WM_SETCURSOR:" in win and "case WM_CAPTURECHANGED:" in win
    glue = _strip_comments(_read(UI_SRC, "win32", "ui_window.cpp"))
    for handler in ("on_mouse", "on_key", "on_char", "on_focus", "on_capture_lost", "on_set_cursor"):
        assert f"h.{handler} = " in glue, handler


def test_text_wraps_at_whole_words_and_hit_tests_from_the_top_line():
    # N2d, both found by the first Windows run: DirectWrite's WRAP splits a word too long for the box (Qt's WordWrap
    # lets it overflow); and hit-testing with the style's vertical centring in an unbounded box height put the text
    # half a million DIPs down
    code = _strip_comments(_read(UI_SRC, "win32", "dwrite_text.cpp"))
    assert "DWRITE_WORD_WRAPPING_WHOLE_WORD" in code and "DWRITE_WORD_WRAPPING_WRAP " not in code
    for fn in ("TextHit DWriteTextEngine::hitTest(", "RectF DWriteTextEngine::caretRect("):
        body = code[code.index(fn):]
        body = body[:body.index("\n}\n")]
        assert "s.valign = VAlign::Top;" in body, fn
    painter = _strip_comments(_read(UI_SRC, "win32", "d2d_painter.cpp"))
    assert "DrawTextLayout" in painter and "ENABLE_COLOR_FONT" in painter and "->DrawText(" not in painter


def test_text_scope_is_the_measured_subset():
    hdr = _strip_comments(_read(UI_SRC, "core", "painter.hpp"))
    assert re.search(r"enum class FontFamily \{ Ui, Monospace \}", hdr)
    for absent in ("elide", "Elide", "richText", "html"):
        assert absent not in hdr, absent


def test_the_edit_stack_is_the_measured_subset_and_tsf_is_full():
    model = _strip_comments(_read(UI_SRC, "core", "edit_model.hpp"))
    for absent in ("EchoMode", "InputMask", "Completer", "maxLength", "setMaxLength"):
        assert absent not in model, absent
    store = _strip_comments(_read(UI_SRC, "win32", "tsf_text_store.hpp"))
    assert "public ITextStoreACP2" in store and "public ITfContextOwnerCompositionSink" in store  # decision 27.7-2
    impl = _strip_comments(_read(UI_SRC, "win32", "tsf_text_store.cpp"))
    assert "TS_E_SYNCHRONOUS" in impl and "TS_S_ASYNC" in impl and "TS_E_NOLOCK" in impl and "queued_" in impl
    edit = _strip_comments(_read(UI_SRC, "win32", "line_edit.cpp"))
    assert "blank_doc_" in edit  # an unfocused edit never keeps the TSF focus


def test_every_widget_is_a_ui_automation_element_with_the_patterns_the_plan_names():
    prov = _strip_comments(_read(UI_SRC, "win32", "uia_provider.hpp"))
    for iface in ("IRawElementProviderSimple", "IRawElementProviderFragment", "IRawElementProviderFragmentRoot",
                  "IInvokeProvider", "IToggleProvider", "IValueProvider", "IRangeValueProvider", "IExpandCollapseProvider", "ISelectionItemProvider", "ITextProvider"):
        assert f"public {iface}" in prov, iface
    impl = _strip_comments(_read(UI_SRC, "win32", "uia_provider.cpp"))
    assert "UiaDisconnectProvider" in impl and "UIA_E_ELEMENTNOTAVAILABLE" in impl  # a removed widget never dangles
    assert "isVisibleSelf()" in impl  # the UIA tree is the VISIBLE widget tree
    win = _strip_comments(_read(UI_SRC, "win32", "ui_window.cpp"))
    assert "UiaReturnRawElementProvider(window_->hwnd(), 0, 0, nullptr)" in win and "SPI_GETHIGHCONTRAST" in win
    assert "case WM_GETOBJECT:" in _strip_comments(_read(DESK, "src", "platform", "window.cpp"))


def test_the_n2_gate_script_has_its_steps():
    s = _read(DESK, "tools", "verify_native_n2.ps1")
    for needle in ("-NoLegacyQt", "tcad_ui_core_tests", "tcad_ui_render_tests", "STALE", "--debug-layer", "$Soak",
                   "check_no_qt.py", "--gallery", "verify_native_n1.ps1", "-Command"):
        assert needle in s, needle
    assert r'$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"' in s  # the gallery runs with the Windows dirs only
    # the MSVC runtime comes from vc_redist (26.9.7), never copied into the stage
    assert "msvcp140|vcruntime140" in s

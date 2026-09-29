"""NATIVE-DESKTOP-PLAN.md 27.5 (N1): gates on the Qt-free native platform layer.

REAL here (Linux, g++ or clang++): the portable core -- JSON-RPC session, backend/PATH configuration, settings, DPI/layout,
shortcuts -- is COMPILED (C++23) and its unit tests run; the same RpcSession is then driven through the REAL Python backend
(backend_service) over a POSIX pipe transport. Static gates check that nothing Qt crept into src/platform, src/native or the
CMake structure and that the N0 spike baseline is untouched.
NOT run here, and not claimed: any Win32 code (app/process/window/workspace/dialogs/backend transport, tcad_native.exe) --
it cannot be compiled or run off Windows. desktop/tools/verify_native_n1.ps1 is that gate, run by the user.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DESK = os.path.join(ROOT, "desktop")
SRC = os.path.join(DESK, "src")
CORE_SOURCES = ["dpi", "env_path", "input", "rpc_session", "settings"]
CXX = shutil.which("g++") or shutil.which("clang++")
needs_cxx = pytest.mark.skipif(CXX is None, reason="no C++ compiler")
posix = pytest.mark.skipif(os.name == "nt", reason="POSIX pipe transport")


def _read(*p):
    with open(os.path.join(*p), "r", encoding="utf-8") as fh:
        return fh.read()


def _sha(*p):
    with open(os.path.join(*p), "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def _compile(out, main_cpp, extra=(), src_dir=None, timeout=300):
    src_dir = src_dir or os.path.join(SRC, "platform")
    srcs = [os.path.join(src_dir, f"{n}.cpp") for n in CORE_SOURCES if os.path.exists(os.path.join(src_dir, f"{n}.cpp"))]
    inc = os.path.dirname(src_dir) if os.path.basename(src_dir) == "platform" else SRC
    cmd = [CXX, "-std=c++23", "-Wall", "-Wextra", f"-I{inc}", f"-I{os.path.join(DESK, 'tests', 'platform')}", main_cpp, *srcs, *extra, "-o", out]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    assert p.returncode == 0, p.stdout + p.stderr
    return p.stderr


@pytest.fixture(scope="module")
def core_tests(tmp_path_factory):
    out = str(tmp_path_factory.mktemp("bin") / "core_tests")
    warnings = _compile(out, os.path.join(DESK, "tests", "platform", "test_platform_core.cpp"))
    assert "warning" not in warnings, warnings          # -Wall -Wextra clean
    return out


@needs_cxx
def test_core_unit_tests_compile_clean_and_pass(core_tests):
    p = subprocess.run([core_tests], capture_output=True, text=True, timeout=300)
    assert p.returncode == 0, p.stdout + p.stderr
    assert re.search(r"^(\d+) test\(s\), 0 failed", p.stdout, re.M)
    assert int(re.search(r"^(\d+) test", p.stdout, re.M).group(1)) >= 30


def _server_available():
    p = subprocess.run([sys.executable, "-c", "import backend_service.server"], cwd=ROOT, capture_output=True, text=True)
    return p.returncode == 0


@needs_cxx
@posix
@pytest.mark.skipif(not _server_available(), reason="backend_service is not importable here")
def test_session_against_the_real_backend(tmp_path):
    out = str(tmp_path / "real")
    _compile(out, os.path.join(DESK, "tests", "platform", "test_platform_backend_posix.cpp"))
    p = subprocess.run([out, sys.executable, ROOT], capture_output=True, text=True, timeout=600)
    assert p.returncode == 0, p.stdout + p.stderr
    assert "4 test(s), 0 failed" in p.stdout


@needs_cxx
@pytest.mark.slow
@pytest.mark.parametrize("old,new,expect", [
    # one request in flight: a second request is written while the first is unanswered
    ("if (inflight_ || queue_.empty() || !process_active_ || killing_ || !transport_.running()) return;",
     "if (queue_.empty() || !process_active_ || killing_ || !transport_.running()) return;", "rpc_one_request_in_flight_fifo"),
    # ASCII escaping of requests dropped
    ("transport_.write(req.dump(-1, ' ', true) + \"\\n\");", "transport_.write(req.dump() + \"\\n\");", "rpc_requests_are_ascii_escaped"),
    # the crash-loop breaker never trips
    ("if (static_cast<int>(crash_times_ms_.size()) >= kCrashLimit) {", "if (false) {", "rpc_three_unexpected_exits"),
    # a timeout does not kill the stuck service
    ("    killProcess();  // the service is sequential and stuck: replace it", "    // (mutated: no kill)", "rpc_timeout_fails_the_call"),
])
def test_mutated_session_is_caught(tmp_path, old, new, expect):
    src = tmp_path / "platform"
    shutil.copytree(os.path.join(SRC, "platform"), src)
    f = src / "rpc_session.cpp"
    text = f.read_text(encoding="utf-8")
    assert old in text, "the mutation target moved: update this test"
    f.write_text(text.replace(old, new, 1), encoding="utf-8")
    out = str(tmp_path / "mut")
    _compile(out, os.path.join(DESK, "tests", "platform", "test_platform_core.cpp"), src_dir=str(src))
    p = subprocess.run([out], capture_output=True, text=True, timeout=300)
    assert p.returncode != 0, "the unit tests did not notice the mutation"
    runs = [l[4:] for l in p.stdout.splitlines() if l.startswith("RUN ")]
    caught = f"FAIL {expect}" in p.stdout or (p.returncode < 0 and runs and runs[-1].startswith(expect))   # failed, or aborted inside it
    assert caught, p.stdout


# ---- static gates ------------------------------------------------------------------------------------------------

QT = re.compile(r'#\s*include\s*[<"]Q[A-Za-z]|\bQ[A-Z][A-Za-z]+\b|\bQ_OBJECT\b|\bemit\b|Qt6::|qtadvanceddocking|GUISupportQt|QVTK')


def _strip(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    return re.sub(r'"(\\.|[^"\\])*"', '""', s)


def _files(sub):
    out = []
    for base, _d, fs in os.walk(os.path.join(SRC, sub)):
        out += [os.path.join(base, f) for f in fs if f.endswith((".cpp", ".hpp", ".h"))]
    return out


def test_platform_and_native_app_sources_have_no_qt():
    for f in _files("platform") + [os.path.join(SRC, "native", n) for n in ("app_main.cpp", "app_support.cpp", "app_support.hpp")]:
        m = QT.search(_strip(_read(f)))
        assert not m, f"{os.path.relpath(f, DESK)}: Qt reference {m.group()!r}"


def test_portable_core_has_no_win32_or_vtk():
    for n in CORE_SOURCES:
        for ext in (".cpp", ".hpp"):
            p = os.path.join(SRC, "platform", n + ext)
            if os.path.exists(p):
                code = _strip(_read(p))
                assert not re.search(r"#\s*include\s*<(windows|winsock2|shlobj|shobjidl|psapi)\.h>|<vtk", code), p
                assert "HWND" not in code and "WINAPI" not in code, p


def test_cmake_native_targets_are_outside_the_legacy_block_and_link_no_qt():
    text = _read(DESK, "CMakeLists.txt")
    legacy = re.search(r"^if\(TCAD_LEGACY_QT\)\n# -- the application.*?^endif\(\)  # TCAD_LEGACY_QT", text, re.S | re.M).group()
    for target in ("tcad_platform_core", "tcad_platform_win32", "tcad_native", "tcad_platform_core_tests", "tcad_native_spike"):
        assert f"add_library({target}" in text.replace(legacy, "") or f"add_executable({target}" in text.replace(legacy, ""), target
    block = re.search(r"# -- N1: the native platform layer.*?vtk_module_autoinit\(TARGETS tcad_native MODULES[^\n]*\)", text, re.S).group()
    code = "\n".join(l.split("#")[0] for l in block.splitlines())     # CMake comments may say "no Qt"
    assert "Qt" not in code and "ads::" not in code and "GUISupportQt" not in code
    assert "cxx_std_23" in code
    assert "tcad_field_scene" in block and "tcad_platform_win32" in block


def test_n0_spike_baseline_is_preserved():
    # 64be993: the N0/P6 native-rendering baseline. verify_native_spike.ps1 is a permanent regression, and the spike's own
    # sources must not drift; only vtk_win32_host.* may change for N1 integration (additively: on_key_mods).
    assert _sha(SRC, "native", "spike_main.cpp") == "37bc408711ef1dcde6891f3f279cfdcdf125f4e2454124e19ff573ef81e93ce9"
    assert _sha(SRC, "native", "win32_window.cpp") == "eb68b24e9346791557db5144327be998a404c52dadc03c843344ac88548cafd8"
    assert _sha(SRC, "native", "win32_window.hpp") == "4daf1c660e87f19e75c2532bd3628aa96c365a5fd1901caed629608f24d85ba6"
    assert _sha(SRC, "native", "tcad_native.manifest") == "db63167a94ee909971e9ce6d1fe2d3886d08ed2c566d65188198bd8373582870"
    assert _sha(DESK, "tools", "verify_native_spike.ps1") == "e99555920bcd546418afd17fbd92e51cfa6be29a7b9a229890d59bc988acb0eb"
    host = _read(SRC, "native", "vtk_win32_host.hpp")
    assert "std::function<void(const std::string&)> on_key;" in host        # the spike's callback is intact
    text = _read(DESK, "CMakeLists.txt")
    assert "add_executable(tcad_native_spike" in text
    assert "TCAD_LEGACY_QT" in text and "option(TCAD_LEGACY_QT" in text and "ON)" in text.split("option(TCAD_LEGACY_QT")[1].split("\n")[0]


def test_field_scene_files_unchanged_by_n1_except_nothing():
    # N1 must not change the VTK FieldScene (it is hosted, not modified)
    for n in ("field_scene.cpp", "field_scene.hpp", "field_scene_3d.cpp"):
        assert "handleMouseMove" in _read(SRC, "views", "field", "field_scene.hpp")
    import subprocess as sp
    r = sp.run(["git", "diff", "--quiet", "64be993", "--", "pytcad/desktop/src/views"], cwd=os.path.dirname(ROOT), capture_output=True)
    if r.returncode not in (0, 1):
        pytest.skip("git history for 64be993 not available")
    assert r.returncode == 0, "src/views changed since the N0 baseline"


def test_gate_script_has_its_steps():
    s = _read(DESK, "tools", "verify_native_n1.ps1")
    for needle in ("-NoLegacyQt", "tcad_platform_core_tests", "dumpbin", "check_no_qt.py", "--selftest", "verify_native_spike.ps1", "-SkipBuild"):
        assert needle in s, needle
    assert "windeployqt" not in s.replace("NO windeployqt", "")
    assert '$env:PATH = "$env:SystemRoot\\System32;$env:SystemRoot"' in s


# ---- Win32 sources: a SYNTAX check against mingw-w64's headers (not MSVC, not a link, not a run) -------------------------

MINGW = shutil.which("x86_64-w64-mingw32-g++")
needs_mingw = pytest.mark.skipif(MINGW is None, reason="mingw-w64 is not installed")


def _nlohmann_include(tmp_path):
    for cand in ("/usr/include/nlohmann", "/usr/local/include/nlohmann"):
        if os.path.isdir(cand):
            d = tmp_path / "nl"
            d.mkdir(exist_ok=True)
            if not (d / "nlohmann").exists():
                os.symlink(cand, d / "nlohmann")
            return str(d)
    pytest.skip("nlohmann/json.hpp is not installed")


def _mingw(tmp_path, rel, vtk=None):
    flags = ["-std=c++23", "-fsyntax-only", "-Wall", "-Wextra", "-DNOMINMAX", "-DUNICODE", "-D_UNICODE", "-D_WIN32_WINNT=0x0A00",
             "-DWINVER=0x0A00", f"-I{SRC}", "-isystem", _nlohmann_include(tmp_path)]
    if vtk:
        flags += ["-isystem", vtk]
    p = subprocess.run([MINGW, *flags, os.path.join(SRC, rel)], capture_output=True, text=True, timeout=300)
    assert p.returncode == 0, p.stderr[-3000:]
    assert "warning:" not in p.stderr, p.stderr[-3000:]


@needs_mingw
@pytest.mark.parametrize("name", ["win32_util", "app", "process", "window", "workspace", "file_dialog", "backend_client"])
def test_win32_platform_sources_pass_a_mingw_syntax_check(tmp_path, name):
    _mingw(tmp_path, f"platform/{name}.cpp")


@needs_mingw
@pytest.mark.skipif(not os.environ.get("TCAD_VTK_INCLUDE"), reason="set TCAD_VTK_INCLUDE to a Windows VTK include dir (e.g. conda-forge vtk-base's Library/include/vtk-9.x)")
@pytest.mark.parametrize("rel", ["native/vtk_win32_host.cpp", "native/app_support.cpp", "native/app_main.cpp", "views/field/field_scene.cpp",
                                 "views/field/field_scene_3d.cpp", "native/spike_main.cpp"])
def test_native_app_and_scene_pass_a_mingw_syntax_check_against_vtk_headers(tmp_path, rel):
    _mingw(tmp_path, rel, vtk=os.environ["TCAD_VTK_INCLUDE"])

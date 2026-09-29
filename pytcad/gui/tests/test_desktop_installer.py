"""NATIVE-DESKTOP-PLAN.md section 26.3, P5-S3: the Windows installer sources
(desktop/installer/tcad.iss, desktop/tools/make_installer.ps1,
desktop/tools/verify_install.ps1).

What runs here (any OS): a static lint of the Inno Setup script against the recorded
decisions and against the app's real paths, and -- where PowerShell 7 (`pwsh`) is
installed -- the scripts' own decision functions, actually executed: version parsing,
"is this stage installable", the Windows Sandbox config, and the file-list diff.

What does NOT run here, and is not claimed by any test in this file: compiling the
installer (ISCC.exe), installing it, running the installed app, uninstalling, and the
before/after file-list and registry diff on a real machine. Those are the Windows gates
listed in NATIVE-DESKTOP-PLAN.md section 26.8.
"""
import json
import os
import re
import shutil
import subprocess
import uuid
import xml.etree.ElementTree as ET

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ISS = os.path.join(ROOT, "desktop", "installer", "tcad.iss")
MAKE = os.path.join(ROOT, "desktop", "tools", "make_installer.ps1")
VERIFY = os.path.join(ROOT, "desktop", "tools", "verify_install.ps1")
CMAKE = os.path.join(ROOT, "desktop", "CMakeLists.txt")

PWSH = shutil.which("pwsh")
needs_pwsh = pytest.mark.skipif(PWSH is None, reason="PowerShell 7 (pwsh) is not installed")


def _read(p):
    with open(p, "r", encoding="utf-8") as fh:
        return fh.read()


def _iss_lines():
    """The script's non-comment, non-blank lines."""
    return [ln.rstrip() for ln in _read(ISS).splitlines() if ln.strip() and not ln.lstrip().startswith(";")]


def _section(name):
    out, on = [], False
    for ln in _iss_lines():
        if ln.startswith("["):
            on = ln.strip() == f"[{name}]"
        elif on:
            out.append(ln)
    return out


def _setup():
    d = {}
    for ln in _section("Setup"):
        k, _, v = ln.partition("=")
        d[k.strip()] = v.strip()
    return d


# -- the Inno Setup script -------------------------------------------------------

def test_app_id_is_a_valid_guid_shared_with_the_verify_script():
    m = re.search(r"^AppId=\{\{([0-9A-Fa-f-]{36})\}$", _read(ISS), re.M)
    assert m, "AppId must be written {{GUID} (a doubled opening brace is Inno's escape)"
    assert str(uuid.UUID(m.group(1))).upper() == m.group(1).upper()
    # verify_install.ps1 finds the Add/Remove Programs entry by this exact GUID
    assert m.group(1).upper() in _read(VERIFY).upper()


def test_install_is_per_user_with_no_admin_under_local_appdata_programs():
    s = _setup()
    assert s["PrivilegesRequired"] == "lowest"
    assert "PrivilegesRequiredOverridesAllowed" not in s          # no admin dialog
    assert s["DefaultDirName"] == r"{autopf}\TCAD"                # {autopf} = %LOCALAPPDATA%\Programs when not admin
    assert s["ArchitecturesAllowed"] == "x64compatible"           # the compiled extension and MKL are x64 only
    assert s["ArchitecturesInstallIn64BitMode"] == "x64compatible"


def test_required_defines_are_guarded_and_every_used_define_exists():
    text = _read(ISS)
    for d in ("AppVersion", "StageDir"):                          # no default: a missing one must stop the build
        assert re.search(rf"#ifndef {d}\s*\n\s*#error", text), d
    assert re.search(r'#ifndef NameSuffix\s*\n\s*#define NameSuffix "-unsigned"', text)   # decision 26.4-2
    defined = set(re.findall(r"^\s*#define (\w+)", text, re.M)) | {"AppVersion", "StageDir"}
    used = set(re.findall(r"\{#(\w+)\}", "\n".join(_iss_lines())))
    assert used <= defined, used - defined
    assert _setup()["OutputBaseFilename"] == "TCAD-{#AppVersion}{#NameSuffix}-setup"


def test_installer_names_the_real_executable_and_ships_the_whole_stage():
    assert re.search(r'#define AppExe "tcad_desktop\.exe"', _read(ISS))
    assert re.search(r"add_executable\(tcad_desktop\b", _read(CMAKE))
    files = _section("Files")
    assert files == ['Source: "{#StageDir}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs']


def test_no_file_association_is_taken_only_open_with_under_applications():
    """The project file type is plain .json and results are .npz: a default association
    would take over every JSON/NumPy file. Only the per-user "Open with" candidate list."""
    main_window = _read(os.path.join(ROOT, "desktop", "src", "shell", "main_window.cpp"))
    # the premise of the design: no dedicated project extension exists yet. If this
    # fails, the installer's association decision (NATIVE-DESKTOP-PLAN.md 26.8) is stale.
    assert "PyTCAD projects (*.json)" in main_window
    reg = _section("Registry")
    assert reg, "the openwith task has registry entries"
    for ln in reg:
        assert ln.startswith("Root: HKA;"), ln                     # HKCU for a per-user install, never HKLM/HKCR
        m = re.search(r'Subkey: "([^"]+)"', ln)
        assert m.group(1).startswith(r"Software\Classes\Applications\{#AppExe}"), ln
        assert "Tasks: openwith" in ln, ln                          # optional, not forced
    assert any(r'"""{app}\{#AppExe}"" ""%1"""' in ln for ln in reg)   # the app takes the file as its argv[1]
    assert any('ValueName: ".json"' in ln for ln in reg) and any('ValueName: ".npz"' in ln for ln in reg)
    assert sum("uninsdeletekey" in ln for ln in reg) == 1           # the top key: the subtree goes at uninstall
    tasks = "\n".join(_section("Tasks"))
    assert re.search(r'Name: "openwith"', tasks) and not re.search(r'Name: "openwith".*unchecked', tasks)


def test_uninstall_removes_the_pyc_caches_and_never_touches_user_data():
    ud = _section("UninstallDelete")
    assert 'Type: filesandordirs; Name: "{app}\\runtime"' in ud
    assert 'Type: filesandordirs; Name: "{app}\\backend"' in ud
    # never `filesandordirs` on all of {app}: a user may have picked a folder holding other things
    assert not [ln for ln in ud if 'Name: "{app}"' in ln and "filesandordirs" in ln]
    for ln in _iss_lines():
        if ln.startswith("["):
            continue
        assert not re.search(r"\{(userappdata|localappdata|userdocs|commonappdata|userdesktop)\}", ln), ln
    # and the user-data locations the comment names are the app's real ones
    app_settings = _read(os.path.join(ROOT, "desktop", "src", "shell", "app_settings.cpp"))
    assert 'IniFormat, QSettings::UserScope, "PyTCAD", "PyTCAD Desktop"' in app_settings
    assert '"PyTCAD/runs"' in _read(os.path.join(ROOT, "desktop", "src", "shell", "main_window.cpp"))


def test_launch_after_install_is_skipped_for_silent_installs():
    run = _section("Run")
    assert len(run) == 1 and "postinstall" in run[0] and "skipifsilent" in run[0]   # the Sandbox gate installs silently


def test_every_script_file_is_lf_and_ascii_safe_where_windows_powershell_reads_it():
    """Windows PowerShell 5.1 reads a BOM-less script as the ANSI code page: a non-ASCII
    character in a .ps1 would be misread. The sources stay ASCII."""
    for p in (MAKE, VERIFY, ISS):
        raw = open(p, "rb").read()
        assert raw.isascii(), f"{os.path.basename(p)} has non-ASCII bytes"


# -- the PowerShell functions, executed --------------------------------------------

def _ps(script, script_args, expr):
    """Dot-source `script` (its main body is skipped when dot-sourced), evaluate `expr`,
    return its JSON."""
    cmd = f". '{script}' {script_args}; ConvertTo-Json -Compress -Depth 6 -InputObject ({expr})"
    p = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-Command", cmd],
                       capture_output=True, text=True, encoding="utf-8", timeout=120)
    assert p.returncode == 0, p.stdout + p.stderr
    return json.loads(p.stdout)


def _arr(items):
    """A PowerShell array literal of single-quoted strings (no Python repr: backslashes)."""
    return "@(" + ",".join("'" + x.replace("'", "''") + "'" for x in items) + ")"


def _fake_stage(tmp_path, manifest=None):
    st = tmp_path / "stage"
    for rel in ("tcad_desktop.exe", "runtime/python.exe", "backend/backend_service/__main__.py",
                "backend/gui/services/solver_runner.py", "backend/pytcad/_core.cp314-win_amd64.pyd"):
        f = st / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(b"")
    (st / "desktop_runtime.json").write_text(json.dumps(
        manifest or {"backend_python": "runtime\\python.exe", "backend_root": "backend", "runtime_bin": ""}))
    return st


@needs_pwsh
def test_version_is_read_from_cmake():
    m = re.search(r"project\(\s*tcad_desktop\s+VERSION\s+(\d+\.\d+\.\d+)", _read(CMAKE))
    assert _ps(MAKE, "", f"Get-DesktopVersion '{CMAKE}'") == m.group(1)


@needs_pwsh
def test_a_complete_relative_stage_is_installable(tmp_path):
    st = _fake_stage(tmp_path)
    assert _ps(MAKE, "", f"@(Get-StageProblems '{st}')") == []


@needs_pwsh
@pytest.mark.parametrize("what, mutate, expect", [
    ("no exe", lambda st: (st / "tcad_desktop.exe").unlink(), "missing tcad_desktop.exe"),
    ("no runtime", lambda st: (st / "runtime" / "python.exe").unlink(), "missing runtime"),
    ("no extension", lambda st: [f.unlink() for f in (st / "backend" / "pytcad").glob("_core*")], "_core"),
    ("no backend", lambda st: (st / "backend" / "backend_service" / "__main__.py").unlink(), "missing backend"),
    ("gmsh shipped", lambda st: (st / "runtime" / "Lib" / "site-packages" / "gmsh").mkdir(parents=True), "gmsh"),
    ("tetgen shipped", lambda st: (st / "runtime" / "Lib" / "site-packages" / "tetgen").mkdir(parents=True), "tetgen"),
    ("scikit-image shipped", lambda st: (st / "runtime" / "Lib" / "site-packages" / "skimage").mkdir(parents=True), "skimage"),
    ("garbage manifest", lambda st: (st / "desktop_runtime.json").write_text("{not json"), "not valid JSON"),
    ("dev manifest", lambda st: (st / "desktop_runtime.json").write_text(json.dumps(
        {"backend_python": os.path.abspath(os.sep + os.path.join("opt", "tcad-dev", "python.exe")),
         "backend_root": "backend"})), "absolute"),
])
def test_a_stage_that_must_not_be_installed_is_refused(tmp_path, what, mutate, expect):
    st = _fake_stage(tmp_path)
    (st / "runtime" / "Lib" / "site-packages").mkdir(parents=True, exist_ok=True)
    mutate(st)
    problems = _ps(MAKE, "", f"@(Get-StageProblems '{st}')")
    assert any(expect in p for p in problems), (what, problems)


@needs_pwsh
def test_sandbox_config_is_valid_xml_with_the_right_mappings(tmp_path):
    xml = _ps(MAKE, "", "New-SandboxConfig 'C:\\out' 'TCAD-0.1.0-unsigned-setup.exe' 'C:\\repo\\tools' 'C:\\out\\log'")
    root = ET.fromstring(xml)
    assert root.tag == "Configuration" and root.findtext("Networking") == "Disable"
    folders = {f.findtext("SandboxFolder"): (f.findtext("HostFolder"), f.findtext("ReadOnly"))
               for f in root.iter("MappedFolder")}
    assert folders == {"C:\\installer": ("C:\\out", "true"), "C:\\tools": ("C:\\repo\\tools", "true"),
                       "C:\\log": ("C:\\out\\log", "false")}
    cmd = root.findtext("LogonCommand/Command")
    assert "verify_install.ps1" in cmd and "C:\\installer\\TCAD-0.1.0-unsigned-setup.exe" in cmd


@needs_pwsh
def test_file_list_diff_reports_what_a_leftover_looks_like(tmp_path):
    root = tmp_path / "Programs"
    (root / "Other").mkdir(parents=True)
    (root / "Other" / "keep.txt").write_text("x")
    args = "-Installer x"
    before = _ps(VERIFY, args, f"@(Get-FileSet '{root}')")
    assert sorted(before) == ["Other", os.path.join("Other", "keep.txt")]
    assert _ps(VERIFY, args, f"@(Get-FileSet '{tmp_path / 'absent'}')") == []
    # an "install" that is fully removed again: identical
    (root / "TCAD" / "runtime").mkdir(parents=True)
    (root / "TCAD" / "runtime" / "python.exe").write_text("x")
    during = _ps(VERIFY, args, f"@(Get-FileSet '{root}')")
    shutil.rmtree(root / "TCAD")
    after = _ps(VERIFY, args, f"@(Get-FileSet '{root}')")
    diff = _ps(VERIFY, args, f"Compare-Sets {_arr(before)} {_arr(after)}")
    assert diff == {"added": [], "removed": []} and len(during) == 5
    # a leftover .pyc directory (what UninstallDelete exists to prevent) is reported as added
    (root / "TCAD" / "backend" / "__pycache__").mkdir(parents=True)
    left = _ps(VERIFY, args, f"@(Get-FileSet '{root}')")
    diff = _ps(VERIFY, args, f"Compare-Sets {_arr(before)} {_arr(left)}")
    assert [os.path.basename(x) for x in diff["added"]] == ["TCAD", "backend", "__pycache__"] and diff["removed"] == []
    # and a vanished file is reported as removed
    diff = _ps(VERIFY, args, "Compare-Sets @('a','b') @('a')")
    assert diff == {"added": [], "removed": ["b"]}


@needs_pwsh
@pytest.mark.parametrize("script", [MAKE, VERIFY])
def test_scripts_parse_without_errors(script):
    cmd = (f"$e=$null;$t=$null;[void][System.Management.Automation.Language.Parser]::ParseFile('{script}',[ref]$t,[ref]$e);"
           "$e.Count")
    p = subprocess.run([PWSH, "-NoProfile", "-Command", cmd], capture_output=True, text=True, timeout=120)
    assert p.stdout.strip() == "0", p.stdout + p.stderr

"""NATIVE-DESKTOP-PLAN.md 26.9.3: desktop/tools/s4_evidence.ps1, the READ-ONLY collector of the facts the S4
licence decisions need from a Windows machine (which ucrt / MSVC runtime files are staged, what the OS loads,
what the installed Visual Studio's Redist folder holds).

Run here (Linux, PowerShell 7) against a fake stage: the file/metadata logic. NOT run here, and not claimed:
Windows-only parts (starting the staged programs and sampling their loaded modules, vswhere, the Visual Studio
Redist folder, Authenticode signatures). Those are reported by the script itself as "skipped: not Windows".
"""
import hashlib
import json
import os
import re
import shutil
import subprocess

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "desktop", "tools", "s4_evidence.ps1")
PWSH = shutil.which("pwsh")
needs_pwsh = pytest.mark.skipif(PWSH is None, reason="PowerShell 7 (pwsh) is not installed")


def _read(p):
    with open(p, "r", encoding="utf-8") as fh:
        return fh.read()


def _ps(expr, cwd=None, as_list=False):
    """Evaluate `expr` after dot-sourcing the script; as_list keeps a one-element result a JSON list."""
    inner = f"@($({expr}))" if as_list else f"$({expr})"
    cmd = f". '{SCRIPT}'; ConvertTo-Json -Compress -Depth 8 -InputObject {inner}"
    p = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-Command", cmd], capture_output=True,
                       text=True, encoding="utf-8", timeout=120, cwd=cwd)
    assert p.returncode == 0, p.stdout + p.stderr
    return json.loads(p.stdout)


def _meta(prefix, name, version, licence, files):
    (prefix / "conda-meta").mkdir(parents=True, exist_ok=True)
    (prefix / "conda-meta" / f"{name}-{version}-h0_0.json").write_text(json.dumps({
        "name": name, "version": version, "build": "h0_0", "channel": "conda-forge", "license": licence,
        "sha256": "ab" * 32, "md5": "cd" * 16, "url": f"https://example/{name}", "files": files}))


def _fake(tmp_path):
    stage, gui = tmp_path / "stage", tmp_path / "gui"
    (stage / "runtime" / "Library" / "bin").mkdir(parents=True)
    gui.mkdir()
    ucrt = ["ucrtbase.dll", "api-ms-win-crt-runtime-l1-1-0.dll", "api-ms-win-crt-math-l1-1-0.dll"]
    allf = ucrt + ["Library/bin/" + u for u in ucrt]
    _meta(stage / "runtime", "ucrt", "10.0.26100.0", "LicenseRef-MicrosoftWindowsSDK10", allf)
    _meta(gui, "ucrt", "10.0.26100.0", "LicenseRef-MicrosoftWindowsSDK10", allf)
    _meta(stage / "runtime", "vc14_runtime", "14.51.36247", "LicenseRef-MicrosoftVisualCpp2015-2022Runtime",
          ["vcruntime140.dll", "msvcp140.dll", "Library/bin/vcruntime140.dll"])
    _meta(stage / "runtime", "vcomp14", "14.51.36247", "LicenseRef-MicrosoftVisualCpp2015-2022Runtime", ["vcomp140.dll"])
    for rel in ("ucrtbase.dll", "api-ms-win-crt-runtime-l1-1-0.dll", "Library/bin/ucrtbase.dll", "vcruntime140.dll",
                "msvcp140.dll", "vcomp140.dll", "Library/bin/vcruntime140.dll"):
        (stage / "runtime" / rel).write_bytes(b"fake " + rel.encode())
    (stage / "api-ms-win-crt-stdio-l1-1-0.dll").write_bytes(b"x")          # an app-dir copy
    (stage / "msvcp140.dll").write_bytes(b"app dir copy")                   # a second copy of a runtime DLL
    # a DLL copied in by hand: in a subfolder, listed by NO conda package (only a recursive scan finds it)
    (stage / "plugins").mkdir()
    (stage / "plugins" / "msvcp140_1.dll").write_bytes(b"stray copy")
    (stage / "licenses").mkdir()
    return stage, gui


def _tree(p):
    out = {}
    for base, _d, files in os.walk(str(p)):
        for f in files:
            fp = os.path.join(base, f)
            out[fp] = hashlib.sha256(open(fp, "rb").read()).hexdigest()
    return out


# -- static properties of the script -------------------------------------------------------------

def test_the_script_is_read_only_and_never_touches_conda_or_the_policy():
    s = _read(SCRIPT)
    code = "\n".join(ln for ln in s.splitlines() if not ln.lstrip().startswith("#"))
    for verb in ("Remove-Item", "Move-Item", "Rename-Item", "Copy-Item", "Clear-Content", "Add-Content"):
        assert verb not in code, verb
    assert code.count("New-Item") == 1 and "New-Item -ItemType Directory -Force $Out" in code   # only its own output dir
    assert not re.search(r"\bconda\s+(install|remove|update|create|clean|env\s+(create|remove))", code)
    assert "license_policy" not in code and "gen_licenses" not in code                            # neither the policy nor the gate
    assert code.count("Start-Process") == 1 and code.count("Stop-Process") == 1                  # sample loaded modules, then stop
    assert all(w in code for w in ("--settings", "ephemeral.ini"))                               # never the user's own settings


def test_the_msvc_check_never_relies_on_the_package_licence_text():
    code = "\n".join(ln for ln in _read(SCRIPT).splitlines() if not ln.lstrip().startswith("#"))
    assert "LICENSE.TXT" not in code.upper().replace("LICENSE.TXT", "LICENSE.TXT") or "LICENSE.TXT" not in code
    assert "info\\licenses" in code                                     # section C hashes texts, only to identify them
    assert "vswhere" in code and "Redist" in code and "redist*.txt" in code   # the basis is the VS installation


def test_the_script_is_ascii():
    assert open(SCRIPT, "rb").read().isascii()


@needs_pwsh
def test_the_script_parses():
    cmd = (f"$e=$null;$t=$null;[void][System.Management.Automation.Language.Parser]::ParseFile('{SCRIPT}',[ref]$t,[ref]$e);$e.Count")
    p = subprocess.run([PWSH, "-NoProfile", "-Command", cmd], capture_output=True, text=True, timeout=120)
    assert p.stdout.strip() == "0", p.stdout + p.stderr


# -- the pure helpers, executed ------------------------------------------------------------------

@needs_pwsh
def test_get_condameta_reads_exact_package_facts(tmp_path):
    _meta(tmp_path, "tk", "9.0.4", "TCL", [])
    _meta(tmp_path, "libtk", "9.0.4", "TCL", ["Library/bin/tk90t.dll"])
    got = _ps(f"Get-CondaMeta '{tmp_path}' @('tk','python')", as_list=True)
    assert [(g["name"], g["version"], g["build"], g["license"], g["sha256"]) for g in got] == [("tk", "9.0.4", "h0_0", "TCL", "ab" * 32)]
    assert _ps(f"Get-CondaMeta '{tmp_path / 'nope'}' @('tk')", as_list=True) == []


@needs_pwsh
def test_get_redist_names_reads_every_dll_name_once_lowercased():
    lines = ["Distributable: VCRUNTIME140.dll, msvcp140.DLL", "  * msvcp140.dll", "vcomp140.dll (OpenMP)", "not a dll.txt"]
    ps = "@(Get-RedistNames @(" + ",".join("'" + x + "'" for x in lines) + "))"
    assert _ps(ps) == ["msvcp140.dll", "vcomp140.dll", "vcruntime140.dll"]


@needs_pwsh
def test_compare_redist_distinguishes_name_only_from_the_same_file(tmp_path):
    a, b, c = tmp_path / "a", tmp_path / "b", tmp_path / "c"
    for d in (a, b, c):
        d.mkdir()
    (a / "vcruntime140.dll").write_bytes(b"same")
    (b / "vcruntime140.dll").write_bytes(b"same")          # identical file in "VS"
    (a / "msvcp140.dll").write_bytes(b"one")
    (b / "msvcp140.dll").write_bytes(b"different")         # same name, different bytes
    (a / "vcomp140.dll").write_bytes(b"x")                 # not in "VS" at all
    expr = (f"$st=@('{a}/vcruntime140.dll','{a}/msvcp140.dll','{a}/vcomp140.dll') | ForEach-Object {{ Get-FileFacts $_ }};"
            f"$vs=@('{b}/vcruntime140.dll','{b}/msvcp140.dll') | ForEach-Object {{ Get-FileFacts $_ }};"
            "@(Compare-Redist $st $vs @('vcruntime140.dll'))")
    rows = {r["file"]: r for r in _ps(expr)}
    assert (rows["vcruntime140.dll"]["inVsRedistFolder"], rows["vcruntime140.dll"]["sameHashAsVs"], rows["vcruntime140.dll"]["inRedistTxt"]) == (True, True, True)
    assert (rows["msvcp140.dll"]["inVsRedistFolder"], rows["msvcp140.dll"]["sameHashAsVs"], rows["msvcp140.dll"]["inRedistTxt"]) == (True, False, False)
    assert (rows["vcomp140.dll"]["inVsRedistFolder"], rows["vcomp140.dll"]["sameHashAsVs"]) == (False, False)


@needs_pwsh
def test_get_filefacts_hashes_a_file_and_reports_a_missing_one(tmp_path):
    f = tmp_path / "x.dll"
    f.write_bytes(b"hello")
    got = _ps(f"Get-FileFacts '{f}'")
    assert got["exists"] is True and got["bytes"] == 5 and got["sha256"] == hashlib.sha256(b"hello").hexdigest()
    assert _ps(f"Get-FileFacts '{tmp_path / 'absent.dll'}'")["exists"] is False


# -- the whole script on a fake stage ---------------------------------------------------------------

@needs_pwsh
def test_the_script_reports_the_staged_ucrt_and_msvc_files_and_changes_nothing(tmp_path):
    stage, gui = _fake(tmp_path)
    out = tmp_path / "out"
    before = {"stage": _tree(stage), "gui": _tree(gui)}
    p = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", SCRIPT, "-Stage", str(stage), "-GuiEnv", str(gui),
                        "-Out", str(out)], capture_output=True, text=True, encoding="utf-8", timeout=300)
    assert p.returncode == 0, p.stdout + p.stderr
    assert {"stage": _tree(stage), "gui": _tree(gui)} == before                       # read-only
    assert sorted(os.listdir(str(out))) == ["s4-evidence.json", "s4-evidence.txt"]
    d = json.loads(_read(str(out / "s4-evidence.json")))
    A, B = d["A_ucrt"], d["B_msvc"]
    assert "listed by conda-meta: 6; present in the stage: 3; absent: 3" in A["runtimeSummary"]
    assert [os.path.basename(x["path"]) for x in A["appDirDlls"]] == ["api-ms-win-crt-stdio-l1-1-0.dll"]
    assert {(x["prefix"] == str(stage / "runtime")) for x in A["packages"]} == {True, False}       # in BOTH environments
    assert A["loaded"][0]["note"] == "skipped: not Windows"                                           # says so, does not pretend
    staged = sorted(os.path.relpath(x["path"], str(stage)).replace("\\", "/") for x in B["stagedDlls"])
    assert staged == ["msvcp140.dll", "plugins/msvcp140_1.dll", "runtime/Library/bin/vcruntime140.dll",
                      "runtime/msvcp140.dll", "runtime/vcomp140.dll", "runtime/vcruntime140.dll"]      # every location of every copy, strays included
    assert "not Windows" in B["note"] and all(r["inVsRedistFolder"] is False for r in B["comparison"])
    # rows carry the Visual Studio copy's own facts (null when there is none) and each staged file's hash
    assert all({"vsPath", "vsVersion", "vsSha256", "stagedSha256"} <= set(r) and r["vsVersion"] is None for r in B["comparison"])
    # a missing redist*.txt is reported as "n/a, not false" and says REDIST authorisation is NOT established
    assert "n/a, NOT false" in B["redistTxtNote"] and "redist_table.py" in B["redistTxtNote"]
    assert all(r["inRedistTxt"] is None for r in B["comparison"])
    assert "inRedistTxt=n/a" in _read(str(out / "s4-evidence.txt"))
    assert "authoritative REDIST list" in B["authority"]
    assert [(c["name"], c["where"]) for c in d["C_packages"]].count(("ucrt", "runtime")) == 1
    txt = _read(str(out / "s4-evidence.txt"))
    assert "== A. ucrt" in txt and "== B. MSVC runtime" in txt and "== C. packages" in txt


# -- loaded-module evidence for the staged python.exe (why "no matching modules read" was not evidence) -----------

def _lm():
    import importlib.util
    spec = importlib.util.spec_from_file_location("loaded_modules", os.path.join(ROOT, "desktop", "tools", "loaded_modules.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def test_loaded_modules_summarize_flags_stage_and_ucrt():
    lm = _lm()
    stage = "C:\\dist\\TCAD\\runtime"
    rep = lm.summarize([stage + "\\python.exe", "C:\\Windows\\System32\\ucrtbase.dll", stage + "\\vcruntime140.dll",
                        "C:\\Windows\\System32\\kernel32.dll", "c:/dist/tcad/runtime/api-ms-win-crt-math-l1-1-0.dll"], stage)
    assert rep["moduleCount"] == 5
    got = {m["module"]: m["fromStage"] for m in rep["matches"]}
    assert got == {"ucrtbase.dll": False, "vcruntime140.dll": True, "api-ms-win-crt-math-l1-1-0.dll": True}


def test_loaded_modules_refuses_rather_than_reports_nothing_off_windows():
    if os.name == "nt":
        pytest.skip("Windows")
    import sys
    p = subprocess.run([sys.executable, os.path.join(ROOT, "desktop", "tools", "loaded_modules.py")], capture_output=True, text=True)
    assert p.returncode == 2 and p.stdout == "" and "Windows only" in p.stderr


def test_evidence_runs_staged_python_on_a_script_file_not_an_unquoted_dash_c():
    code = _read(SCRIPT)
    assert "loaded_modules.py" in code and "time.sleep(12)" not in code          # the old failing invocation is gone
    assert "-ArgumentList (ConvertTo-ArgString" in code                          # nothing passes a raw array to Start-Process


@needs_pwsh
def test_argstring_quotes_spaces_and_empty():
    s = _ps('ConvertTo-ArgString @("--settings", "C:\\a b\\x.ini", "", "plain")')
    assert s == '--settings "C:\\a b\\x.ini" "" plain'

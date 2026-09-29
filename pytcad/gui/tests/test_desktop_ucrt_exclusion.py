"""NATIVE-DESKTOP-PLAN.md 26.9.3: stage.ps1 -ExcludeUcrt (stage-time exclusion of the conda `ucrt` package).

Executed under PowerShell 7 against a fake staged runtime built from the REAL ucrt 10.0.26100.0 file set
(46 distinct names in two places = 92 files). NOT covered, not claimed: an actual Windows staging with the switch,
and the S2/S3 revalidation that removing app-local UCRT files requires (a runtime change).
"""
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ROOT, "desktop", "tools")
STAGE_PS1 = os.path.join(TOOLS, "stage.ps1")
PWSH = shutil.which("pwsh")
needs_pwsh = pytest.mark.skipif(PWSH is None, reason="PowerShell 7 (pwsh) is not installed")

# the distinct names in conda-forge ucrt-10.0.26100.0-h57928b3_0 (info/paths.json): each at the env root AND Library/bin
UCRT_NAMES = (
    ["api-ms-win-core-" + n for n in (
        "console-l1-1-0", "console-l1-2-0", "datetime-l1-1-0", "debug-l1-1-0", "errorhandling-l1-1-0", "fibers-l1-1-0",
        "fibers-l1-1-1", "file-l1-1-0", "file-l1-2-0", "file-l2-1-0", "handle-l1-1-0", "heap-l1-1-0", "interlocked-l1-1-0",
        "kernel32-legacy-l1-1-1", "libraryloader-l1-1-0", "localization-l1-2-0", "memory-l1-1-0", "namedpipe-l1-1-0",
        "processenvironment-l1-1-0", "processthreads-l1-1-0", "processthreads-l1-1-1", "profile-l1-1-0",
        "rtlsupport-l1-1-0", "string-l1-1-0", "synch-l1-1-0", "synch-l1-2-0", "sysinfo-l1-1-0", "sysinfo-l1-2-0",
        "timezone-l1-1-0", "util-l1-1-0")]
    + ["api-ms-win-crt-" + n for n in (
        "conio-l1-1-0", "convert-l1-1-0", "environment-l1-1-0", "filesystem-l1-1-0", "heap-l1-1-0", "locale-l1-1-0",
        "math-l1-1-0", "multibyte-l1-1-0", "private-l1-1-0", "process-l1-1-0", "runtime-l1-1-0", "stdio-l1-1-0",
        "string-l1-1-0", "time-l1-1-0", "utility-l1-1-0")]
)
UCRT_NAMES = [n + ".dll" for n in UCRT_NAMES] + ["ucrtbase.dll"]
UCRT_FILES = UCRT_NAMES + ["Library/bin/" + n for n in UCRT_NAMES]


def _read(p):
    with open(p, "r", encoding="utf-8") as fh:
        return fh.read()


def _meta(prefix, name, version, files, licence="MIT"):
    (prefix / "conda-meta").mkdir(parents=True, exist_ok=True)
    (prefix / "conda-meta" / f"{name}-{version}-h0_0.json").write_text(json.dumps(
        {"name": name, "version": version, "build": "h0_0", "license": licence, "files": files}))


def _touch(prefix, files):
    for rel in files:
        p = prefix / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"x")


def _fake_runtime(tmp_path):
    rt = tmp_path / "runtime"
    _meta(rt, "ucrt", "10.0.26100.0", UCRT_FILES, "LicenseRef-MicrosoftWindowsSDK10")
    _meta(rt, "python", "3.14.7", ["python.exe", "python314.dll"], "PSF-2.0")
    _meta(rt, "vc14_runtime", "14.51.36247", ["vcruntime140.dll", "Library/bin/vcruntime140.dll"],
          "LicenseRef-MicrosoftVisualCpp2015-2022Runtime")
    _touch(rt, UCRT_FILES + ["python.exe", "python314.dll", "vcruntime140.dll", "Library/bin/vcruntime140.dll", "Lib/keep.py"])
    return rt


def _ps(rt, expr, extra=""):
    script = (f"$ast=[System.Management.Automation.Language.Parser]::ParseFile('{STAGE_PS1}',[ref]$null,[ref]$null);"
              "foreach($f in $ast.FindAll({$args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst]},$true)){"
              "if($f.Name -in 'Remove-CondaPackage','Get-CondaPackageFileNames'){Invoke-Expression $f.Extent.Text}};"
              f"{extra}; ConvertTo-Json -Compress -Depth 6 -InputObject $({expr})")
    p = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True, text=True,
                       encoding="utf-8", timeout=120)
    return p


def test_the_fake_matches_the_real_package_shape():
    assert len(UCRT_NAMES) == 46 and len(UCRT_FILES) == 92 and len(set(UCRT_FILES)) == 92
    assert sum(n.startswith("api-ms-win-crt-") for n in UCRT_NAMES) == 15 and sum(n.startswith("api-ms-win-core-") for n in UCRT_NAMES) == 30


@needs_pwsh
def test_removes_exactly_the_92_listed_files_and_the_conda_meta_record(tmp_path):
    rt = _fake_runtime(tmp_path)
    p = _ps(rt, f"Remove-CondaPackage '{rt}' 'ucrt'")
    assert p.returncode == 0, p.stdout + p.stderr
    r = json.loads(p.stdout)
    assert (r["name"], r["version"], r["build"]) == ("ucrt", "10.0.26100.0", "h0_0")
    assert sorted(r["removed"]) == sorted(UCRT_FILES) and len(r["removed"]) == 92 and r["missing"] == [] and r["keptShared"] == []
    assert r["metaRemoved"] == ["ucrt-10.0.26100.0-h0_0.json"]
    for rel in UCRT_FILES:
        assert not (rt / rel).exists(), rel
    # everything else is untouched: other packages' files AND their conda-meta records
    for rel in ("python.exe", "python314.dll", "vcruntime140.dll", "Library/bin/vcruntime140.dll", "Lib/keep.py"):
        assert (rt / rel).is_file(), rel
    assert sorted(os.listdir(str(rt / "conda-meta"))) == ["python-3.14.7-h0_0.json", "vc14_runtime-14.51.36247-h0_0.json"]


@needs_pwsh
def test_a_second_run_is_a_noop_and_a_missing_package_returns_nothing(tmp_path):
    rt = _fake_runtime(tmp_path)
    assert _ps(rt, f"Remove-CondaPackage '{rt}' 'ucrt'").returncode == 0
    p = _ps(rt, f"Remove-CondaPackage '{rt}' 'ucrt'")
    assert p.returncode == 0 and p.stdout.strip() == "null"
    p = _ps(rt, f"Remove-CondaPackage '{rt}' 'no_such_package'")
    assert p.returncode == 0 and p.stdout.strip() == "null"


@needs_pwsh
def test_files_absent_from_disk_are_reported_not_fatal(tmp_path):
    rt = _fake_runtime(tmp_path)
    (rt / "ucrtbase.dll").unlink()
    (rt / "Library" / "bin" / "api-ms-win-crt-time-l1-1-0.dll").unlink()
    r = json.loads(_ps(rt, f"Remove-CondaPackage '{rt}' 'ucrt'").stdout)
    assert sorted(r["missing"]) == ["Library/bin/api-ms-win-crt-time-l1-1-0.dll", "ucrtbase.dll"] and len(r["removed"]) == 90


@needs_pwsh
def test_a_file_another_package_also_owns_is_kept(tmp_path):
    rt = _fake_runtime(tmp_path)
    _meta(rt, "other", "1.0", ["ucrtbase.dll"], "MIT")                      # claims one of ucrt's files
    r = json.loads(_ps(rt, f"Remove-CondaPackage '{rt}' 'ucrt'").stdout)
    assert (rt / "ucrtbase.dll").is_file() and len(r["removed"]) == 91
    assert r["keptShared"] == ["ucrtbase.dll (also owned by other)"]


@needs_pwsh
@pytest.mark.parametrize("bad", ["../escape.dll", "..\\escape.dll", "sub/../../escape.dll", "C:/Windows/System32/x.dll", "/etc/x.dll"])
def test_a_path_outside_the_prefix_is_refused_before_anything_is_deleted(tmp_path, bad):
    rt = _fake_runtime(tmp_path)
    _meta(rt, "ucrt", "10.0.26100.0", UCRT_FILES + [bad], "LicenseRef-MicrosoftWindowsSDK10")
    p = _ps(rt, f"Remove-CondaPackage '{rt}' 'ucrt'")
    assert p.returncode != 0 and "refusing to remove" in (p.stdout + p.stderr)
    assert all((rt / rel).is_file() for rel in UCRT_FILES), "nothing may be deleted when any path is refused"
    assert (rt / "conda-meta" / "ucrt-10.0.26100.0-h0_0.json").is_file()


@needs_pwsh
def test_the_app_side_name_list_is_the_distinct_lowercase_dll_names(tmp_path):
    gui = tmp_path / "gui"
    _meta(gui, "ucrt", "10.0.26100.0", UCRT_FILES, "LicenseRef-MicrosoftWindowsSDK10")
    p = _ps(gui, f"Get-CondaPackageFileNames '{gui}' 'ucrt'")
    got = json.loads(p.stdout)
    assert got == sorted(n.lower() for n in UCRT_NAMES) and len(got) == 46


def test_after_exclusion_the_licence_inventory_no_longer_contains_ucrt(tmp_path):
    """The point of deleting the conda-meta record: gen_licenses inventories what ships."""
    spec = importlib.util.spec_from_file_location("gen_licenses", os.path.join(TOOLS, "gen_licenses.py"))
    gl = importlib.util.module_from_spec(spec)
    sys.modules["gen_licenses"] = gl
    spec.loader.exec_module(gl)
    stage = tmp_path / "stage"
    rt = stage / "runtime"
    _meta(rt, "ucrt", "10.0.26100.0", UCRT_FILES, "LicenseRef-MicrosoftWindowsSDK10")
    _meta(rt, "python", "3.14.7", ["python.exe"], "PSF-2.0")
    _touch(rt, UCRT_FILES + ["python.exe"])
    pol = gl.load_policy(gl.DEFAULT_POLICY)
    rows, problems, _ = gl.build_inventory(str(stage), None, pol, False)
    assert {r["name"] for r in rows} == {"ucrt", "python"}
    assert any("LicenseRef-MicrosoftWindowsSDK10" in p for p in problems)          # today's blocker
    for rel in UCRT_FILES:
        os.remove(str(rt / rel))
    os.remove(str(rt / "conda-meta" / "ucrt-10.0.26100.0-h0_0.json"))
    rows, problems, _ = gl.build_inventory(str(stage), None, pol, False)
    assert {r["name"] for r in rows} == {"python"} and not any("ucrt" in p or "MicrosoftWindowsSDK10" in p for p in problems)


def test_stage_script_wiring_is_explicit_ordered_and_never_touches_the_conda_env():
    s = _read(STAGE_PS1)
    code = "\n".join(ln for ln in s.splitlines() if not ln.lstrip().startswith("#"))
    assert "[switch] $ExcludeUcrt" in code and "$ExcludeUcrt" in code.split("param(")[1].split(")")[0]      # off by default: an explicit decision
    assert not re.search(r"^\s*(&\s*)?conda(\.exe)?\s+(remove|uninstall|install|update)", code, re.M)         # never `conda remove ucrt` (it would take python with it)
    assert "conda remove" not in code
    closure = code.index("$excludedDlls.ContainsKey($key)")
    assert code.index("Get-CondaPackageFileNames $gui") < closure                                                # the skip list exists before the closure walk
    assert closure < code.index("Remove-CondaPackage $rt")                                                       # app-side skip, then the runtime removal
    assert code.index("Remove-CondaPackage $rt") < code.index("gen_licenses.py") < code.index("excluded-packages.json")   # inventory sees the result; the record is written last
    assert "-ExcludeUcrt: still staged" in code                                                                  # and it proves nothing is left


def test_the_patch_is_ascii():
    assert open(STAGE_PS1, "rb").read().isascii()

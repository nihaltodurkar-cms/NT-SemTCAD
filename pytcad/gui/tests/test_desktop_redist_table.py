"""NATIVE-DESKTOP-PLAN.md 26.9.3: desktop/tools/redist_table.py, the MSVC-runtime table
DLL | staged version | VS version | same hash | REDIST-listed | source URL/section.

The REDIST list is supplied by the user from Microsoft's page (it is not reachable from the cloud session); these
tests use SYNTHETIC lists only to test the mechanics. No test here asserts anything about what Microsoft's real list
contains, and none may.
"""
import importlib.util
import json
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_spec = importlib.util.spec_from_file_location("redist_table", os.path.join(ROOT, "desktop", "tools", "redist_table.py"))
rt = importlib.util.module_from_spec(_spec)
sys.modules["redist_table"] = rt
_spec.loader.exec_module(rt)

STAGE = "C:/x/dist/TCAD"


def _row(name, at, version="14.51.36247.0", same_hash=True, same_version=True, vs_version=None):
    return {"file": name, "stagedAt": f"{STAGE}/{at}", "version": version, "signature": "Valid", "sameHashAsVs": same_hash,
            "sameVersionAsVs": same_version, "inVsRedistFolder": True, "vsVersion": vs_version}


def _evidence(tmp_path, rows):
    p = tmp_path / "ev.json"
    p.write_text(json.dumps({"generated_for": STAGE, "B_msvc": {"comparison": rows, "vs": {"displayName": "Visual Studio Community 2026",
                                                                                        "installationVersion": "18.0.0"}}}))
    return str(p)


ROWS = [_row("vcruntime140.dll", "runtime/vcruntime140.dll"), _row("vcruntime140.dll", "vcruntime140.dll"),
        _row("msvcp140_1.dll", "runtime/Library/bin/msvcp140_1.dll"), _row("vcomp140.dll", "runtime/vcomp140.dll")]
LIST = "Distributable Code:\n  vcruntime140.dll\n  msvcp140*.dll\n  vcomp140.dll\n  concrt140.dll\n  vccorlib140.dll\n"


def test_parse_list_reads_names_and_wildcards_lowercased_and_distinct():
    got = rt.parse_list("VCRUNTIME140.DLL, msvcp140*.dll\nvcruntime140.dll  x?.dll   readme.txt")
    assert got == ["msvcp140*.dll", "vcruntime140.dll", "x?.dll"]


def test_wildcards_and_exact_names_match_case_insensitively():
    pats = rt.parse_list(LIST)
    assert rt.is_listed("msvcp140_1.dll", pats) and rt.is_listed("VCRUNTIME140.dll", pats)
    assert not rt.is_listed("vcruntime140_1.dll", pats)          # not on THIS list: a prefix is not a wildcard


def test_without_a_list_the_column_says_not_established_and_exits_zero(tmp_path, capsys):
    assert rt.main(["--evidence", _evidence(tmp_path, ROWS)]) == 0
    out = capsys.readouterr().out
    assert "NOT ESTABLISHED" in out and "| DLL | staged version | VS version | same hash | REDIST-listed | source URL/section |" in out
    assert "True | NOT ESTABLISHED (no list supplied) | - |" in out


def test_with_a_list_every_staged_copy_gets_a_row_and_the_source_is_recorded(tmp_path, capsys):
    lst = tmp_path / "list.txt"
    lst.write_text(LIST)
    code = rt.main(["--evidence", _evidence(tmp_path, ROWS), "--redist-list", str(lst),
                    "--source-url", "https://example.invalid/vs2026-redist", "--source-section", "Distributable Code / REDIST list",
                    "--out", str(tmp_path / "t.md")])
    out = capsys.readouterr().out
    assert code == 0
    body = [ln for ln in out.splitlines() if ln.startswith("| ") and not ln.startswith("| DLL |")]
    assert len(body) == 4                                                                # one row per STAGED file, header excluded
    assert "| runtime/vcruntime140.dll | 14.51.36247.0 | 14.51.36247.0 | True | True | https://example.invalid/vs2026-redist , section: Distributable Code / REDIST list |" in out
    assert "| vcruntime140.dll |" in out                                                    # the app-dir copy is its own row
    assert "sha256" in out and "Visual Studio Community 2026" in out
    rows = json.loads((tmp_path / "t.md.json").read_text())["rows"]
    assert sorted(r["dll"] for r in rows) == ["runtime/Library/bin/msvcp140_1.dll", "runtime/vcomp140.dll", "runtime/vcruntime140.dll", "vcruntime140.dll"]


def test_a_dll_not_on_the_list_or_not_identical_to_vs_fails_the_run(tmp_path, capsys):
    lst = tmp_path / "list.txt"
    lst.write_text(LIST)
    rows = ROWS + [_row("vcruntime140_1.dll", "runtime/vcruntime140_1.dll")]              # not on this list
    args = ["--redist-list", str(lst), "--source-url", "u", "--source-section", "s"]
    assert rt.main(["--evidence", _evidence(tmp_path, rows)] + args) == 1
    assert "| False |" in capsys.readouterr().out
    rows = ROWS + [_row("vcomp140.dll", "vcomp140.dll", same_hash=False)]                 # listed, but differs from Visual Studio's file
    assert rt.main(["--evidence", _evidence(tmp_path, rows)] + args) == 1


def test_the_source_must_be_recorded_and_a_tiny_list_is_refused(tmp_path, capsys):
    lst = tmp_path / "list.txt"
    lst.write_text(LIST)
    assert rt.main(["--evidence", _evidence(tmp_path, ROWS), "--redist-list", str(lst)]) == 2
    assert "needs --source-url and --source-section" in capsys.readouterr().out
    tiny = tmp_path / "tiny.txt"
    tiny.write_text("vcruntime140.dll only")
    assert rt.main(["--evidence", _evidence(tmp_path, ROWS), "--redist-list", str(tiny), "--source-url", "u", "--source-section", "s"]) == 2
    assert "does not look like a REDIST list" in capsys.readouterr().out


def test_vs_version_uses_the_recorded_value_else_derives_it_only_from_a_true_same_version():
    rows = rt.rows_from_evidence({"generated_for": STAGE, "B_msvc": {"comparison": [
        _row("a.dll", "a.dll", vs_version="14.51.99999.0", same_version=False),
        _row("b.dll", "b.dll", vs_version=None, same_version=True),
        _row("c.dll", "c.dll", vs_version=None, same_version=False)]}})
    got = {r["name"]: r["vs_version"] for r in rows}
    assert got == {"a.dll": "14.51.99999.0", "b.dll": "14.51.36247.0", "c.dll": "unknown (re-run s4_evidence.ps1)"}


def test_the_tool_is_ascii_and_states_that_nothing_is_inferred():
    p = os.path.join(ROOT, "desktop", "tools", "redist_table.py")
    assert open(p, "rb").read().isascii()
    assert "Nothing is inferred" in open(p, encoding="utf-8").read()

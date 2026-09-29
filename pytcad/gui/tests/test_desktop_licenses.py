"""NATIVE-DESKTOP-PLAN.md section 26.9, P5-S4: the third-party licence bundle and its gate
(desktop/tools/gen_licenses.py, license_policy.json, notices/LGPL-relinking.txt).

The generator reads conda's `conda-meta/*.json` (a conda-pack'd runtime keeps them) and the
`tcad-gui` env's, so it is tested here against FAKE prefixes built with conda's documented keys
(name, version, build, channel, license, files, extracted_package_dir, url) and a fake package
cache holding `info/licenses/`. Every gate has a case where it must fail.

NOT covered by any test in this file, and NOT claimed: a run against the real Windows runtime and
the real tcad-gui env. What licence strings those packages actually carry, whether their extracted
package directories still exist, and which of them the classifier cannot classify, is only known by
running `stage.ps1` on Windows (NATIVE-DESKTOP-PLAN.md 26.9).
"""
import importlib.util
import json
import os
import re
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ROOT, "desktop", "tools")
GEN = os.path.join(TOOLS, "gen_licenses.py")

_spec = importlib.util.spec_from_file_location("gen_licenses", GEN)
gl = importlib.util.module_from_spec(_spec)
sys.modules["gen_licenses"] = gl
_spec.loader.exec_module(gl)


def _read(p):
    with open(p, "r", encoding="utf-8") as fh:
        return fh.read()


# -- a fake stage ------------------------------------------------------------------------

class Fake:
    """A stage dir + a fake tcad-gui env + a fake conda package cache."""

    def __init__(self, tmp):
        self.tmp = tmp
        self.stage = tmp / "stage"
        self.rt = self.stage / "runtime"
        self.gui = tmp / "gui-env"
        self.pkgs = tmp / "pkgs"
        for d in (self.stage, self.rt, self.gui, self.pkgs):
            d.mkdir(parents=True, exist_ok=True)

    def pkg(self, prefix, name, version, license, files=(), text="Licence text of %s.\n", cache=True,
            texts=True, keep_dir=True, about=None, files_on_disk=None):
        ext = self.pkgs / f"{name}-{version}-0"
        if cache and keep_dir:
            (ext / "info" / "licenses").mkdir(parents=True, exist_ok=True)
            if texts:
                (ext / "info" / "licenses" / "LICENSE.txt").write_text(text % name if "%s" in text else text)
            if about is not None:
                (ext / "info" / "about.json").write_text(json.dumps({"license": about}))
        meta = {"name": name, "version": version, "build": "0", "channel": "https://conda.anaconda.org/conda-forge",
                "url": f"https://conda.anaconda.org/conda-forge/win-64/{name}-{version}-0.conda",
                "files": list(files), "extracted_package_dir": str(ext)}
        if license is not None:
            meta["license"] = license
        (prefix / "conda-meta").mkdir(parents=True, exist_ok=True)
        (prefix / "conda-meta" / f"{name}-{version}-0.json").write_text(json.dumps(meta))
        for f in (files if files_on_disk is None else files_on_disk):
            p = prefix / f
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"x")
        return meta

    def dll(self, rel):
        p = self.stage / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"x")

    def base(self):
        """A licence-clean base stage: a small runtime and the app's Qt/ADS/VTK-style DLLs."""
        (self.stage / "tcad_desktop.exe").write_bytes(b"x")
        (self.rt / "python.exe").write_bytes(b"x")
        self.pkg(self.rt, "python", "3.14.7", "PSF-2.0", files=["python.exe"])
        self.pkg(self.rt, "numpy", "2.5.3", "BSD-3-Clause", files=["Lib/site-packages/numpy/_core.pyd"])
        self.pkg(self.rt, "mkl", "2025.3.0", "LicenseRef-Intel-Proprietary", files=["Library/bin/mkl_rt.2.dll"])
        self.pkg(self.rt, "libzlib", "1.3.1", "Zlib", files=["Library/bin/zlib.dll"])
        self.pkg(self.gui, "qt6-main", "6.11.0", "LGPL-3.0-only OR GPL-3.0-only",
                 files=["Library/bin/Qt6Core.dll", "Library/bin/Qt6Widgets.dll", "Library/lib/qt6/plugins/platforms/qwindows.dll"],
                 files_on_disk=[])
        self.pkg(self.gui, "qt6-advanced-docking-system", "5.1.1", "LGPL-2.1-only",
                 files=["Library/bin/qtadvanceddocking-qt6.dll"], files_on_disk=[])
        self.pkg(self.gui, "vtk-base", "9.5.0", "BSD-3-Clause", files=["Library/bin/vtkCommonCore-9.5.dll"], files_on_disk=[])
        self.pkg(self.gui, "nlohmann_json", "3.12.0", "MIT", files=["Library/include/nlohmann/json.hpp"], files_on_disk=[])
        for d in ("Qt6Core.dll", "Qt6Widgets.dll", "platforms/qwindows.dll", "qtadvanceddocking-qt6.dll",
                  "vtkCommonCore-9.5.dll"):
            self.dll(d)
        return self

    def run(self, *extra, policy=None, capsys=None):
        args = ["--stage", str(self.stage), "--gui-env", str(self.gui)] + list(extra)
        if policy:
            args += ["--policy", str(policy)]
        return gl.main(args)


@pytest.fixture
def fake(tmp_path):
    return Fake(tmp_path).base()


def _out(capsys):
    o = capsys.readouterr()
    return o.out + o.err


# -- classification --------------------------------------------------------------------

@pytest.mark.parametrize("expr, expected", [
    ("MIT", "permissive"), ("BSD-3-Clause", "permissive"), ("BSD 3-clause", "permissive"),
    ("Apache-2.0", "permissive"), ("PSF-2.0", "permissive"), ("Zlib", "permissive"),
    ("Boost Software License, Version 1.0", "permissive"),          # a comma is not a separator
    ("LGPL-3.0-only", "weak-copyleft"), ("LGPL-2.1-or-later", "weak-copyleft"), ("MPL-2.0", "weak-copyleft"),
    ("GPL-3.0-only", "strong-copyleft"), ("GPL-2.0-or-later", "strong-copyleft"), ("AGPL-3.0-only", "strong-copyleft"),
    ("GPL-3.0-only WITH GCC-exception-3.1", "runtime-exception"),
    ("Apache-2.0 WITH LLVM-exception", "permissive"),
    ("Apache-2.0 AND MIT", "permissive"),
    ("MIT AND GPL-3.0-only", "strong-copyleft"),                    # AND: the most restrictive term
    ("Apache-2.0 OR GPL-2.0-or-later", "permissive"),               # OR: the licensee picks the least restrictive
    ("LGPL-3.0-only OR GPL-3.0-only", "weak-copyleft"),             # Qt's own expression
    ("Intel Simplified Software License", "unknown"), ("custom", "unknown"), ("", "unknown"),
    ("LicenseRef-Proprietary", "unknown"),
])
def test_classify(expr, expected):
    assert gl.classify(expr)[0] == expected


def test_lgpl_is_never_read_as_gpl_and_agpl_is_never_read_as_weaker():
    for token in ("LGPL-2.1-only", "LGPL-3.0-or-later", "lgpl 3"):
        assert gl.classify(token)[0] == "weak-copyleft"
    assert gl.classify("AGPL-3.0-or-later")[0] == "strong-copyleft"


# -- the policy -----------------------------------------------------------------------

def test_shipped_policy_is_valid_and_only_relaxes_what_the_plan_decided():
    pol = gl.load_policy(gl.DEFAULT_POLICY)
    # decision 26.4-4: copyleft is only ever allowed for the two add-on packages
    assert set(pol["addon_copyleft"]) == {"gmsh", "tetgen"}
    # 26.1: MKL is redistributable under the Intel licence with its notice -- the one reviewed entry
    assert set(pol["reviewed"]) == {"mkl"} and pol["reviewed"]["mkl"]["class"] == "proprietary-redistributable"
    assert [e["package"] for e in pol["embedded"]] == ["nlohmann_json"]
    assert not any(v["class"] == "strong-copyleft" for v in pol["reviewed"].values())


def test_a_policy_entry_without_a_reason_is_refused(tmp_path):
    bad = tmp_path / "p.json"
    bad.write_text(json.dumps({"reviewed": {"foo": {"class": "permissive", "reason": "", "basis": "x"}}}))
    with pytest.raises(ValueError, match="reason"):
        gl.load_policy(str(bad))
    bad.write_text(json.dumps({"reviewed": {"foo": {"class": "unknown", "reason": "r", "basis": "b"}}}))
    with pytest.raises(ValueError, match="class"):
        gl.load_policy(str(bad))


def test_qt_modules_that_are_gpl_or_commercial_only_are_the_ones_section_7_3_names():
    assert set(gl.FORBIDDEN_DLLS) == {r"^qt6charts", r"^qt6graphs", r"^qt6datavisualization"}
    plan = _read(os.path.join(ROOT, "NATIVE-DESKTOP-PLAN.md"))
    assert "Qt Charts / Qt Graphs / Qt Data Visualization" in plan          # the source of that list


# -- the gate: a clean stage passes and writes a bundle -----------------------------------

def test_a_clean_stage_produces_a_complete_bundle(fake, capsys):
    assert fake.run() == 0, _out(capsys)
    lic = fake.stage / "licenses"
    notices = _read(str(lic / "THIRD_PARTY_NOTICES.txt"))
    man = json.loads(_read(str(lic / "manifest.json")))
    names = {c["name"] for c in man["components"]}
    assert names == {"python", "numpy", "mkl", "libzlib", "qt6-main", "qt6-advanced-docking-system", "vtk-base",
                     "nlohmann_json"}
    for c in man["components"]:
        assert c["texts"], c["name"]                                    # every component carries its own licence text
        for t in c["texts"]:
            assert (lic / t["file"]).is_file()
    assert man["counts"]["weak-copyleft"] == 2 and man["counts"]["proprietary-redistributable"] == 1
    # LGPL relinking statement, naming exactly the LGPL components that are staged
    assert "LGPL components and your right to replace them" in notices
    assert "qt6-main 6.11.0" in notices and "qt6-advanced-docking-system 5.1.1" in notices
    assert "not been reviewed by counsel" in notices
    lgpl_block = notices.split("Components under terms")[0]
    assert "numpy" not in lgpl_block and "vtk-base" not in lgpl_block
    # MKL's reviewed status and reason are visible to a reader
    assert re.search(r"mkl 2025\.3\.0: .*proprietary-redistributable.*Intel Simplified Software License", notices)
    # DLLs are traced to their owning packages, including a plugin in a subfolder
    assert man["dlls"]["platforms/qwindows.dll"] == ["qt6-main"]
    assert man["dlls"]["qtadvanceddocking-qt6.dll"] == ["qt6-advanced-docking-system"]
    assert "embedded in tcad_desktop.exe" in notices                    # nlohmann_json, header-only
    assert gl.verify_bundle(str(fake.stage)) == []


def test_output_is_deterministic(fake, capsys):
    assert fake.run() == 0
    first = {f: _read(str(fake.stage / "licenses" / f)) for f in ("THIRD_PARTY_NOTICES.txt", "manifest.json")}
    assert fake.run() == 0
    second = {f: _read(str(fake.stage / "licenses" / f)) for f in first}
    assert first == second


# -- the gate: each way a stage must be refused ---------------------------------------------

def _fails(fake, capsys, *extra, expect):
    code = fake.run(*extra)
    out = _out(capsys)
    assert code == 1, out
    assert expect in out, out
    assert not (fake.stage / "licenses").exists(), "a failing run must not leave a bundle"
    return out


def test_refuses_a_package_with_no_licence_string(fake, capsys):
    fake.pkg(fake.rt, "mystery", "1.0", None, files=["Lib/mystery.py"])
    _fails(fake, capsys, expect="mystery 1.0: no licence string")


def test_the_licence_string_falls_back_to_info_about_json(fake, capsys):
    fake.pkg(fake.rt, "quiet", "1.0", None, files=["Lib/quiet.py"], about="MIT")
    assert fake.run() == 0, _out(capsys)


def test_refuses_a_licence_the_classifier_cannot_classify_until_a_person_reviews_it(fake, capsys, tmp_path):
    fake.pkg(fake.rt, "odd", "2.0", "SomeCustomTerms-1.0", files=["Lib/odd.py"])
    _fails(fake, capsys, expect="odd 2.0: licence 'SomeCustomTerms-1.0' cannot be classified")
    pol = json.loads(_read(gl.DEFAULT_POLICY))
    pol["reviewed"]["odd"] = {"class": "permissive", "reason": "read the text: BSD-like", "basis": "review 2026-09-30"}
    p = tmp_path / "policy.json"
    p.write_text(json.dumps(pol))
    assert fake.run(policy=p) == 0, _out(capsys)


def test_refuses_strong_copyleft_in_the_base_runtime(fake, capsys):
    fake.pkg(fake.rt, "gmsh", "4.15.0", "GPL-2.0-or-later", files=["Lib/gmsh.py"])
    out = _fails(fake, capsys, expect="strong copyleft licence 'GPL-2.0-or-later' in the base runtime")
    assert "decision 26.4-4" in out


def test_a_gpl_with_a_runtime_exception_is_allowed_with_a_note(fake, capsys):
    fake.pkg(fake.rt, "libgomp", "13.2.0", "GPL-3.0-only WITH GCC-exception-3.1", files=["Library/bin/libgomp.dll"])
    assert fake.run() == 0, _out(capsys)
    notices = _read(str(fake.stage / "licenses" / "THIRD_PARTY_NOTICES.txt"))
    assert re.search(r"libgomp 13\.2\.0: .*runtime-exception", notices)


def test_addon_runtime_allows_only_the_packages_the_policy_names(fake, capsys):
    fake.pkg(fake.rt, "gmsh", "4.15.0", "GPL-2.0-or-later", files=["Lib/gmsh.py"])
    fake.pkg(fake.rt, "tetgen", "0.8", "AGPL-3.0-only", files=["Lib/tetgen.py"])
    assert fake.run("--addon") == 0, _out(capsys)
    notices = _read(str(fake.stage / "licenses" / "THIRD_PARTY_NOTICES.txt"))
    assert "decision 26.4-4" in notices and "strong-copyleft" in notices
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    assert man["addon"] is True
    # ... and a GPL package the policy does NOT name is still refused, even in the add-on
    fake.pkg(fake.rt, "somegpl", "1.0", "GPL-3.0-only", files=["Lib/somegpl.py"])
    _fails(fake, capsys, "--addon", expect="somegpl 1.0: strong copyleft")


def test_refuses_a_package_whose_licence_text_is_missing(fake, capsys):
    fake.pkg(fake.rt, "notext", "1.0", "MIT", files=["Lib/notext.py"], texts=False)
    _fails(fake, capsys, expect="notext 1.0: no licence text on disk")


def test_refuses_a_package_whose_extracted_directory_is_gone(fake, capsys):
    fake.pkg(fake.rt, "cleaned", "1.0", "MIT", files=["Lib/cleaned.py"], keep_dir=False)
    _fails(fake, capsys, expect="extracted package directory no longer exists")


def test_refuses_a_staged_dll_that_no_package_owns(fake, capsys):
    fake.dll("mystery_helper.dll")
    _fails(fake, capsys, expect="mystery_helper.dll: no licence entry")


def test_an_unowned_dll_needs_a_policy_entry_with_a_licence_text(fake, capsys, tmp_path):
    fake.dll("hand_built.dll")
    text = tmp_path / "HAND_LICENSE.txt"
    text.write_text("MIT text")
    pol = json.loads(_read(gl.DEFAULT_POLICY))
    pol["unowned_dlls"]["hand_built.dll"] = {"package": "hand-built", "version": "1.0", "license": "MIT",
                                             "reason": "built from source by us", "text_file": str(text)}
    p = tmp_path / "policy.json"
    p.write_text(json.dumps(pol))
    assert fake.run(policy=p) == 0, _out(capsys)
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    assert man["dlls"]["hand_built.dll"] == ["hand-built"]
    # a policy entry pointing at a text file that does not exist is not accepted
    pol["unowned_dlls"]["hand_built.dll"]["text_file"] = str(tmp_path / "nope.txt")
    p.write_text(json.dumps(pol))
    assert fake.run(policy=p) == 1
    assert "does not exist" in _out(capsys)


@pytest.mark.parametrize("dll", ["Qt6Charts.dll", "Qt6Graphs.dll", "Qt6DataVisualization.dll", "qt6charts.dll"])
def test_refuses_the_gpl_only_qt_modules_even_though_qt6_main_is_lgpl(fake, capsys, dll):
    fake.dll(dll)
    fake.pkg(fake.gui, "qt6-charts-ish", "6.11.0", "LGPL-3.0-only OR GPL-3.0-only", files=[f"Library/bin/{dll}"],
             files_on_disk=[])
    _fails(fake, capsys, expect=f"{dll}: DLL of a GPL/commercial-only Qt module")


def test_refuses_a_runtime_binary_no_conda_package_owns(fake, capsys):
    (fake.rt / "Lib" / "site-packages").mkdir(parents=True, exist_ok=True)
    (fake.rt / "Lib" / "site-packages" / "stray.pyd").write_bytes(b"x")
    _fails(fake, capsys, expect="runtime binary with no owning conda package: runtime/lib/site-packages/stray.pyd")


def test_refuses_a_pip_installed_distribution_in_the_runtime(fake, capsys):
    di = fake.rt / "Lib" / "site-packages" / "leftpad-1.0.dist-info"
    di.mkdir(parents=True)
    (di / "INSTALLER").write_text("pip\n")
    _fails(fake, capsys, expect="pip-installed distribution not covered")
    (di / "INSTALLER").write_text("conda\n")
    assert fake.run() == 0


def test_every_problem_is_reported_at_once(fake, capsys):
    fake.pkg(fake.rt, "gmsh", "4.15.0", "GPL-2.0-or-later", files=["Lib/gmsh.py"])
    fake.pkg(fake.rt, "mystery", "1.0", None, files=["Lib/mystery.py"], texts=False)
    fake.dll("orphan.dll")
    fake.dll("Qt6Charts.dll")
    code = fake.run()
    out = _out(capsys)
    assert code == 1
    for expect in ("gmsh 4.15.0: strong copyleft", "mystery 1.0: no licence string", "mystery 1.0: no licence text",
                   "orphan.dll: no licence entry", "Qt6Charts.dll: DLL of a GPL/commercial-only Qt module"):
        assert expect in out, (expect, out)
    assert re.search(r"\d+ licence problem\(s\)", out)


def test_check_only_writes_nothing(fake, capsys):
    assert fake.run("--check-only") == 0
    assert not (fake.stage / "licenses").exists()
    assert "no problems" in _out(capsys)


# -- verifying an installed bundle (what verify_install.ps1 runs) --------------------------------

def test_verify_bundle_detects_tampering_and_staleness(fake, capsys):
    assert fake.run() == 0
    app = str(fake.stage)
    assert gl.verify_bundle(app) == []

    lic = fake.stage / "licenses"
    man = json.loads(_read(str(lic / "manifest.json")))
    victim = lic / next(c for c in man["components"] if c["name"] == "numpy")["texts"][0]["file"]
    original = victim.read_text()
    victim.write_text("edited")
    assert any("modified" in p for p in gl.verify_bundle(app))
    victim.unlink()
    assert any("is missing" in p for p in gl.verify_bundle(app))
    victim.write_text(original)
    assert gl.verify_bundle(app) == []

    fake.dll("late_addition.dll")                                   # a DLL added after the bundle was made
    assert any("late_addition.dll: DLL not covered" in p for p in gl.verify_bundle(app))
    (fake.stage / "late_addition.dll").unlink()

    fake.pkg(fake.rt, "newcomer", "1.0", "MIT", files=["Lib/newcomer.py"])   # a package added after
    assert any("newcomer" in p and "stale" in p for p in gl.verify_bundle(app))

    (lic / "THIRD_PARTY_NOTICES.txt").unlink()
    assert any("THIRD_PARTY_NOTICES.txt is missing" in p for p in gl.verify_bundle(app))


def test_verify_bundle_cli_exit_status(fake, capsys):
    assert fake.run() == 0
    capsys.readouterr()
    assert gl.main(["--verify-bundle", str(fake.stage)]) == 0
    assert "licence bundle OK" in _out(capsys)
    os.remove(str(fake.stage / "licenses" / "manifest.json"))
    assert gl.main(["--verify-bundle", str(fake.stage)]) == 1


def test_a_base_bundle_that_lists_strong_copyleft_fails_verification(fake):
    assert fake.run() == 0
    mp = fake.stage / "licenses" / "manifest.json"
    man = json.loads(_read(str(mp)))
    next(c for c in man["components"] if c["name"] == "numpy")["class"] = "strong-copyleft"
    mp.write_text(json.dumps(man))
    assert any("strong copyleft in a base bundle" in p for p in gl.verify_bundle(str(fake.stage)))


# -- the scripts that use it --------------------------------------------------------------------

def test_stage_make_installer_and_verify_scripts_use_the_bundle():
    stage = _read(os.path.join(TOOLS, "stage.ps1"))
    assert "gen_licenses.py" in stage and "--gui-env $gui" in stage and "[switch] $NoLicenses" in stage
    call = stage.index('(Join-Path $PSScriptRoot "gen_licenses.py")')          # the invocation, not the header comment
    assert call > stage.index('Copy-Tree (Join-Path $root "gui\\services")')          # after the backend is staged
    assert call < stage.index("$stagedRuntime = $true")                            # inside the runtime-staging block
    make = _read(os.path.join(TOOLS, "make_installer.ps1"))
    assert r"licenses\THIRD_PARTY_NOTICES.txt" in make and r"licenses\manifest.json" in make
    assert "--verify-bundle" in make
    verify = _read(os.path.join(TOOLS, "verify_install.ps1"))
    assert "--verify-bundle" in verify and 'Record "licenses"' in verify
    for p in ("stage.ps1", "make_installer.ps1", "verify_install.ps1", "gen_licenses.py", "license_policy.json",
              os.path.join("notices", "LGPL-relinking.txt")):
        assert open(os.path.join(TOOLS, p), "rb").read().isascii(), p


def test_relinking_template_has_its_placeholder_and_the_counsel_caveat():
    t = _read(gl.RELINK_TEMPLATE)
    assert "@LGPL_COMPONENTS@" in t and "not been reviewed by counsel" in t
    assert "statically linked" in t                                  # the claim S1's DLL closure supports

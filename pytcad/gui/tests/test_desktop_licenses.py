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
import hashlib
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
    # 26.1: MKL is redistributable under the Intel licence with its notice; 26.9.7: tk (TCL) and tzdata
    # (public domain) accepted as permissive, each pinned to the one build a person read
    assert set(pol["reviewed"]) == {"mkl", "tk", "tzdata"} and pol["reviewed"]["mkl"]["class"] == "proprietary-redistributable"
    assert pol["reviewed"]["tk"]["pin"] == {"version": "8.6.13", "build": "h967ab96_4"}
    assert pol["reviewed"]["tzdata"]["pin"] == {"version": "2026c", "build": "h151e31d_0"}
    assert [e["package"] for e in pol["embedded"]] == ["nlohmann_json"]
    assert not any(v["class"] == "strong-copyleft" for v in pol["reviewed"].values())
    # 26.9.7: the Microsoft runtimes are NOT accepted by review: the MSVC one is excluded from the stage and
    # installed by Microsoft's vc_redist, the UCRT one comes with Windows
    assert not {"vc14_runtime", "vcomp14", "ucrt"} & set(pol["reviewed"])
    assert set(pol["supplemental_texts"]) == {"libsqlite", "libwinpthread", "pyamg", "libfreetype6", "tk"}


def test_every_shipped_supplemental_text_matches_its_pin():
    pol = gl.load_policy(gl.DEFAULT_POLICY)
    for name, e in pol["supplemental_texts"].items():
        for t in e["texts"]:
            p = os.path.join(TOOLS, *t["file"].split("/"))
            assert os.path.isfile(p), (name, t["file"])
            assert gl.text_sha256(p) == t["sha256"], (name, t["file"])
    # FTL section 2: the FreeType credit, with the year of the version shipped
    assert "The FreeType Project" in pol["supplemental_texts"]["libfreetype6"]["notice"]
    # git must hand these bytes over untouched on every checkout
    assert "* -text" in _read(os.path.join(TOOLS, "notices", "upstream", ".gitattributes"))


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
    _fails(fake, capsys, expect="notext 1.0: no licence text")


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
    assert "gen_licenses.py" in stage and '"--gui-env", $gui' in stage and "[switch] $NoLicenses" in stage
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


# -- restoring licence texts the package cache no longer has ---------------------------------------
#
# The first real Windows run reported 43 problems in 54 components, most of them "no licence text":
# conda-meta's extracted_package_dir (in the pkgs cache) no longer held info/licenses. The texts live
# in the package ARCHIVE, so they can be restored mechanically (--restore-texts: the local tarball;
# --download: re-fetch, verified against the checksum conda recorded). Real archives are built here.

import hashlib  # noqa: E402
import io  # noqa: E402
import tarfile  # noqa: E402
import zipfile  # noqa: E402

try:                                                       # the same choice gen_licenses makes
    from compression import zstd as _zstd  # noqa: F401
    HAVE_ZSTD = True
except ImportError:
    try:
        import zstandard as _zstandard
        HAVE_ZSTD = True
    except ImportError:
        HAVE_ZSTD = False
needs_zstd = pytest.mark.skipif(not HAVE_ZSTD, reason="reading .conda packages needs Python 3.14+ or `zstandard`")


def _tar_bytes(members):
    """members: {name: bytes}; a name may contain '..' (built with TarInfo, not the filesystem)."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as tf:
        for name, data in members.items():
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            tf.addfile(ti, io.BytesIO(data))
    return buf.getvalue()


def _zst(data):
    try:
        from compression import zstd
        return zstd.compress(data)
    except ImportError:
        return _zstandard.ZstdCompressor().compress(data)


def make_archive(path, kind, name, version, licence_text=b"the package's licence text\n", about_license="MIT",
                 with_licenses=True, extra=None):
    """A real conda package at `path` (kind: 'tar.bz2' or 'conda') with info/licenses, info/about.json and a
    payload file that must NOT be extracted."""
    info = {"info/about.json": json.dumps({"license": about_license}).encode(),
            "info/index.json": json.dumps({"name": name, "version": version}).encode()}
    if with_licenses:
        info["info/licenses/LICENSE.txt"] = licence_text
    info.update(extra or {})
    payload = {"lib/payload.txt": b"payload"}
    if kind == "tar.bz2":
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:bz2") as tf:
            for n, d in {**payload, **info}.items():
                ti = tarfile.TarInfo(n)
                ti.size = len(d)
                tf.addfile(ti, io.BytesIO(d))
        path.write_bytes(buf.getvalue())
    else:
        with zipfile.ZipFile(path, "w") as z:
            z.writestr("metadata.json", json.dumps({"conda_pkg_format_version": 2}))
            z.writestr(f"pkg-{name}-{version}-0.tar.zst", _zst(_tar_bytes(payload)))
            z.writestr(f"info-{name}-{version}-0.tar.zst", _zst(_tar_bytes(info)))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _set_meta(prefix, name, version, **kv):
    f = prefix / "conda-meta" / f"{name}-{version}-0.json"
    m = json.loads(f.read_text())
    m.update(kv)
    f.write_text(json.dumps(m))


def _cleaned_package(fake, name="cleaned", version="1.0", license="MIT", kind="tar.bz2", tarball=True,
                     sha=True, **archive_kw):
    """A runtime package whose extracted cache dir is gone (`conda clean`), optionally with its tarball."""
    fake.pkg(fake.rt, name, version, license, files=[f"Lib/{name}.py"], keep_dir=False)
    arch = fake.tmp / "tarballs" / f"{name}-{version}-0.{kind}"
    arch.parent.mkdir(exist_ok=True)
    digest = make_archive(arch, kind, name, version, **archive_kw)
    kv = {"url": arch.as_uri()}
    if sha:
        kv["sha256"] = digest
    if tarball:
        kv["package_tarball_full_path"] = str(arch)
    _set_meta(fake.rt, name, version, **kv)
    return arch


def _cache(fake):
    return fake.tmp / "license-cache"


def test_without_restore_the_diagnosis_says_why_and_names_the_fix(fake, capsys):
    _cleaned_package(fake)
    out = _fails(fake, capsys, expect="cleaned 1.0: no licence text (its extracted package directory no longer exists")
    assert "LICENCE TEXTS MISSING -- by cause:" in out and "extracted directory gone: 1" in out
    assert "--restore-texts" in out and "--download" in out


@pytest.mark.parametrize("kind", ["tar.bz2", pytest.param("conda", marks=needs_zstd)])
def test_restore_texts_from_the_local_tarball(fake, capsys, kind):
    _cleaned_package(fake, kind=kind, licence_text=b"MIT LICENCE OF cleaned\n")
    assert fake.run("--restore-texts", "--text-cache", str(_cache(fake))) == 0, _out(capsys)
    lic = fake.stage / "licenses"
    man = json.loads(_read(str(lic / "manifest.json")))
    row = next(c for c in man["components"] if c["name"] == "cleaned")
    assert row["texts_from"] == "restored from the local package tarball"
    assert (lic / row["texts"][0]["file"]).read_text() == "MIT LICENCE OF cleaned\n"
    assert "text-src: restored from the local package tarball" in _read(str(lic / "THIRD_PARTY_NOTICES.txt"))
    # ONLY the licence files were taken out of the archive, not the payload
    cached = [os.path.relpath(os.path.join(b, f), str(_cache(fake)))
              for b, _d, fs in os.walk(str(_cache(fake))) for f in fs]
    assert sorted(c.replace("\\", "/") for c in cached) == sorted(
        ["cleaned-1.0-0/info/about.json", "cleaned-1.0-0/info/licenses/LICENSE.txt"])


def test_restore_uses_no_network_and_needs_no_download_flag(fake, capsys):
    _cleaned_package(fake)                                                   # tarball present
    _set_meta(fake.rt, "cleaned", "1.0", url="http://127.0.0.1:9/never-fetched.tar.bz2")
    assert fake.run("--restore-texts", "--text-cache", str(_cache(fake))) == 0, _out(capsys)


def test_a_missing_tarball_needs_download_and_says_so(fake, capsys):
    _cleaned_package(fake, tarball=False)
    out = _fails(fake, capsys, "--restore-texts", "--text-cache", str(_cache(fake)),
                 expect="the extracted directory and the tarball are both gone; re-run with --download")
    assert "the tarballs are gone too: re-run with --download" in out


@pytest.mark.parametrize("kind", ["tar.bz2", pytest.param("conda", marks=needs_zstd)])
def test_download_restores_the_texts_verified_against_the_recorded_checksum(fake, capsys, kind):
    _cleaned_package(fake, kind=kind, tarball=False, licence_text=b"fetched licence\n")
    assert fake.run("--download", "--text-cache", str(_cache(fake))) == 0, _out(capsys)
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    row = next(c for c in man["components"] if c["name"] == "cleaned")
    assert row["texts_from"].startswith("downloaded from file:///") and "checksum verified" in row["texts_from"]
    assert not list((_cache(fake) / "_download").glob("*"))                 # only the texts are kept


def test_a_download_that_does_not_match_the_recorded_checksum_is_refused(fake, capsys):
    _cleaned_package(fake, tarball=False)
    _set_meta(fake.rt, "cleaned", "1.0", sha256="0" * 64)
    out = _fails(fake, capsys, "--download", "--text-cache", str(_cache(fake)), expect="sha256 does not match conda-meta")
    assert "download of file:///" in out and "refused" in out
    assert not (_cache(fake) / "cleaned-1.0-0").exists() and not list((_cache(fake) / "_download").glob("*"))


def test_a_download_with_no_recorded_checksum_is_refused(fake, capsys):
    _cleaned_package(fake, tarball=False, sha=False)
    _fails(fake, capsys, "--download", "--text-cache", str(_cache(fake)), expect="cannot be verified")


def test_the_md5_is_used_when_no_sha256_is_recorded(fake, capsys):
    arch = _cleaned_package(fake, tarball=False, sha=False)
    _set_meta(fake.rt, "cleaned", "1.0", md5=hashlib.md5(arch.read_bytes()).hexdigest())
    assert fake.run("--download", "--text-cache", str(_cache(fake))) == 0, _out(capsys)


def test_a_failed_download_is_a_named_reason_not_a_crash(fake, capsys):
    _cleaned_package(fake, tarball=False)
    _set_meta(fake.rt, "cleaned", "1.0", url=(fake.tmp / "no_such.tar.bz2").as_uri())
    out = _fails(fake, capsys, "--download", "--text-cache", str(_cache(fake)), expect="failed:")
    assert "download failed or refused: 1" in out


def test_an_archive_with_no_licence_text_is_a_human_decision_not_a_mechanical_fix(fake, capsys):
    _cleaned_package(fake, with_licenses=False)
    out = _fails(fake, capsys, "--restore-texts", "--text-cache", str(_cache(fake)),
                 expect="the package archive itself contains no info/licenses")
    assert "archive itself has no licence text (upstream): 1" in out and "human decision" in out


def test_a_hostile_archive_cannot_write_outside_the_cache(fake, capsys):
    arch = _cleaned_package(fake, extra={"info/licenses/../../evil.txt": b"pwned"})
    out = _fails(fake, capsys, "--restore-texts", "--text-cache", str(_cache(fake)), expect="unsafe path in package archive")
    assert not (fake.tmp / "evil.txt").exists() and not (_cache(fake) / "evil.txt").exists()
    assert not list(fake.tmp.rglob("evil.txt"))
    assert arch.exists()


def test_restored_texts_are_kept_so_a_second_run_needs_neither_tarball_nor_network(fake, capsys):
    arch = _cleaned_package(fake, tarball=False)
    assert fake.run("--download", "--text-cache", str(_cache(fake))) == 0, _out(capsys)
    arch.unlink()                                                          # the "network" is gone now
    shutil = __import__("shutil")
    shutil.rmtree(str(fake.stage / "licenses"))
    assert fake.run("--restore-texts", "--text-cache", str(_cache(fake))) == 0, _out(capsys)
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    assert next(c for c in man["components"] if c["name"] == "cleaned")["texts_from"] == "restored earlier (licence-text cache)"


def test_the_licence_string_falls_back_to_the_restored_about_json(fake, capsys):
    _cleaned_package(fake, license=None, about_license="Apache-2.0")
    assert fake.run("--restore-texts", "--text-cache", str(_cache(fake))) == 0, _out(capsys)
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    assert next(c for c in man["components"] if c["name"] == "cleaned")["license"] == "Apache-2.0"


def test_extract_info_takes_only_licence_files_and_refuses_unsafe_names(tmp_path):
    arch = tmp_path / "p.tar.bz2"
    make_archive(arch, "tar.bz2", "p", "1")
    dest = tmp_path / "out"
    assert gl.extract_info(str(arch), str(dest)) == 2
    assert sorted(os.listdir(str(dest / "info"))) == ["about.json", "licenses"]
    assert not (dest / "lib").exists()
    for bad in ("/abs/x", "info/licenses/../../x", "C:/x", ""):
        assert not gl._safe_member(bad), bad
    assert gl._safe_member("info/licenses/LICENSE.txt")
    with pytest.raises(ValueError, match="unrecognised"):
        gl.extract_info(str(tmp_path / "p.zip"), str(dest))


# -- the human decisions, laid out ----------------------------------------------------------------

def test_unclassified_licences_are_grouped_with_the_exact_packages(fake, capsys):
    fake.pkg(fake.rt, "tzdata", "2025b", "LicenseRef-Public-Domain", files=["Lib/tzdata.py"])
    fake.pkg(fake.rt, "libsqlite", "3.50", "LicenseRef-Public-Domain", files=["Library/bin/sqlite3.dll"])
    fake.pkg(fake.rt, "ucrt", "10.0.22621", "LicenseRef-MicrosoftWindowsSDK10", files=["Library/bin/ucrtbase.dll"])
    fake.pkg(fake.rt, "tk", "8.6.13", "TCL", files=["Library/bin/tk86t.dll"])
    out = _fails(fake, capsys, expect="HUMAN DECISIONS NEEDED")
    assert "'LicenseRef-Public-Domain': libsqlite 3.50, tzdata 2025b" in out
    assert "'LicenseRef-MicrosoftWindowsSDK10': ucrt 10.0.22621" in out
    assert "'TCL': tk 8.6.13" in out
    assert "reviewed" in out and "class, reason, basis" in out


def test_stage_script_passes_the_restore_options_and_removes_the_sdk_dxc_dlls():
    stage = _read(os.path.join(TOOLS, "stage.ps1"))
    assert "[switch] $DownloadLicenseTexts" in stage and "--restore-texts" in stage and "--download" in stage
    # windeployqt6 deploys these two from the Windows SDK (like the D3D compiler it is already told to skip);
    # nothing owns them and the app has no D3D12 code, so they are not shipped
    assert 'dxcompiler.dll' in stage and 'dxil.dll' in stage and "Remove-Item" in stage


# -- 26.9.7: pins, supplemental texts, metapackages -------------------------------------------------

def _policy(tmp_path, **sections):
    pol = json.loads(_read(gl.DEFAULT_POLICY))
    for k, v in sections.items():                                   # added to the shipped entries, not replacing them
        pol.setdefault(k, {}).update(v)
    p = tmp_path / "policy.json"
    p.write_text(json.dumps(pol))
    return p


def _vendor(monkeypatch, tmp_path, rel, data):
    """A fake TOOLS dir holding one vendored text; returns its pinned (LF) sha256."""
    tools = tmp_path / "tools"
    f = tools / "notices" / "upstream" / rel
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_bytes(data)
    monkeypatch.setattr(gl, "TOOLS", str(tools))
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def _sup(sha, rel="odd/LICENSE", build="0", version="1.0", **kw):
    e = {"pin": {"version": version, "build": build}, "reason": "archive ships no text", "basis": "test",
         "texts": [{"file": "notices/upstream/" + rel, "sha256": sha, "source": "upstream tag v1.0"}]}
    e.update(kw)
    return e


def test_a_reviewed_entry_pinned_to_another_build_must_be_re_reviewed(fake, capsys, tmp_path):
    fake.pkg(fake.rt, "odd", "2.0", "SomeCustomTerms-1.0", files=["Lib/odd.py"])
    rev = {"class": "permissive", "reason": "read it", "basis": "b", "pin": {"version": "2.0", "build": "0"}}
    assert fake.run(policy=_policy(tmp_path, reviewed={"odd": rev})) == 0, _out(capsys)
    rev["pin"]["build"] = "1"                                         # the review was of a different build
    code = fake.run(policy=_policy(tmp_path, reviewed={"odd": rev}))
    out = _out(capsys)
    assert code == 1 and "odd 2.0: the reviewed policy entry is pinned to 2.0 build 1" in out
    assert "licence 'SomeCustomTerms-1.0' cannot be classified" in out   # and the review no longer applies


def test_a_pin_without_a_build_is_refused(tmp_path):
    for sec in ({"reviewed": {"x": {"class": "permissive", "reason": "r", "basis": "b", "pin": {"version": "1"}}}},
                {"supplemental_texts": {"x": _sup("0" * 64, build="")}}):
        p = tmp_path / "p.json"
        p.write_text(json.dumps(sec))
        with pytest.raises(ValueError, match="pin"):
            gl.load_policy(str(p))


@pytest.mark.parametrize("bad", [{"file": "notices/other/x", "sha256": "0" * 64, "source": "s"},
                                 {"file": "notices/upstream/../../x", "sha256": "0" * 64, "source": "s"},
                                 {"file": "notices/upstream/x", "sha256": "abc", "source": "s"},
                                 {"file": "notices/upstream/x", "sha256": "0" * 64, "source": ""}])
def test_a_malformed_supplemental_text_is_refused(tmp_path, bad):
    e = _sup("0" * 64)
    e["texts"] = [bad]
    p = tmp_path / "p.json"
    p.write_text(json.dumps({"supplemental_texts": {"x": e}}))
    with pytest.raises(ValueError, match="supplemental_texts"):
        gl.load_policy(str(p))


def test_a_supplemental_text_fills_a_package_whose_archive_has_none(fake, capsys, tmp_path, monkeypatch):
    fake.pkg(fake.rt, "odd", "1.0", "MIT", files=["Lib/odd.py"], texts=False)
    _fails(fake, capsys, expect="odd 1.0: no licence text")
    sha = _vendor(monkeypatch, tmp_path, "odd/LICENSE", b"MIT text\r\nline 2\r\n")   # a CRLF (autocrlf) checkout
    assert fake.run(policy=_policy(tmp_path, supplemental_texts={"odd": _sup(sha)})) == 0, _out(capsys)
    lic = fake.stage / "licenses"
    man = json.loads(_read(str(lic / "manifest.json")))
    row = next(c for c in man["components"] if c["name"] == "odd")
    assert [t["source"] for t in row["texts"]] == ["upstream tag v1.0"]
    shipped = (lic / row["texts"][0]["file"]).read_bytes()
    assert shipped == b"MIT text\nline 2\n"                             # the pinned bytes, not the checkout's
    assert "(vendored; from upstream tag v1.0)" in _read(str(lic / "THIRD_PARTY_NOTICES.txt"))
    assert gl.verify_bundle(str(fake.stage)) == []


def test_a_supplemental_text_is_added_to_the_packages_own(fake, capsys, tmp_path, monkeypatch):
    fake.pkg(fake.rt, "odd", "1.0", "MIT", files=["Lib/odd.py"])
    sha = _vendor(monkeypatch, tmp_path, "odd/BUNDLED", b"bundled component terms\n")
    assert fake.run(policy=_policy(tmp_path, supplemental_texts={"odd": _sup(sha, rel="odd/BUNDLED")})) == 0
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    row = next(c for c in man["components"] if c["name"] == "odd")
    assert sorted(os.path.basename(t["file"]) for t in row["texts"]) == ["BUNDLED", "LICENSE.txt"]


def test_a_supplemental_text_whose_bytes_changed_is_refused(fake, capsys, tmp_path, monkeypatch):
    fake.pkg(fake.rt, "odd", "1.0", "MIT", files=["Lib/odd.py"], texts=False)
    sha = _vendor(monkeypatch, tmp_path, "odd/LICENSE", b"MIT text\n")
    (tmp_path / "tools" / "notices" / "upstream" / "odd" / "LICENSE").write_bytes(b"MIT text, edited\n")
    fake.run(policy=_policy(tmp_path, supplemental_texts={"odd": _sup(sha)}))
    out = _out(capsys)
    assert "odd 1.0: supplemental text notices/upstream/odd/LICENSE does not match its pinned sha256" in out
    assert "odd 1.0: no licence text" in out and not (fake.stage / "licenses").exists()


def test_a_missing_supplemental_file_is_refused(fake, capsys, tmp_path, monkeypatch):
    fake.pkg(fake.rt, "odd", "1.0", "MIT", files=["Lib/odd.py"], texts=False)
    sha = _vendor(monkeypatch, tmp_path, "odd/LICENSE", b"MIT text\n")
    os.remove(str(tmp_path / "tools" / "notices" / "upstream" / "odd" / "LICENSE"))
    fake.run(policy=_policy(tmp_path, supplemental_texts={"odd": _sup(sha)}))
    assert "supplemental text notices/upstream/odd/LICENSE does not exist" in _out(capsys)


def test_a_supplemental_entry_for_another_build_does_not_apply(fake, capsys, tmp_path, monkeypatch):
    fake.pkg(fake.rt, "odd", "1.0", "MIT", files=["Lib/odd.py"], texts=False)
    sha = _vendor(monkeypatch, tmp_path, "odd/LICENSE", b"MIT text\n")
    fake.run(policy=_policy(tmp_path, supplemental_texts={"odd": _sup(sha, build="h123_2")}))
    out = _out(capsys)
    assert "odd 1.0: supplemental_texts entry is pinned to 1.0 build h123_2, staged is build 0" in out
    assert "odd 1.0: no licence text" in out


def test_a_required_acknowledgement_is_printed_in_the_notices(fake, capsys, tmp_path, monkeypatch):
    fake.pkg(fake.gui, "libfreetype6", "2.14.3", "GPL-2.0-only OR FTL", files=["Library/bin/freetype.dll"],
             texts=False, files_on_disk=[])
    fake.dll("freetype.dll")
    sha = _vendor(monkeypatch, tmp_path, "libfreetype6/FTL.TXT", b"The FreeType Project LICENSE\n")
    e = _sup(sha, rel="libfreetype6/FTL.TXT", version="2.14.3",
             notice="Portions of this software are copyright (c) 2026 The FreeType Project.")
    assert fake.run(policy=_policy(tmp_path, supplemental_texts={"libfreetype6": e})) == 0, _out(capsys)
    notices = _read(str(fake.stage / "licenses" / "THIRD_PARTY_NOTICES.txt"))
    head = notices.split("Components\n")[0]
    assert "Acknowledgements required" in head
    assert "copyright (c) 2026 The FreeType Project.  (libfreetype6 2.14.3)" in head
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    assert man["dlls"]["freetype.dll"] == ["libfreetype6"]


def test_a_metapackage_that_installs_nothing_owes_no_text(fake, capsys):
    fake.pkg(fake.rt, "vc", "14.5", "BSD-3-Clause", files=[], texts=False)
    assert fake.run() == 0, _out(capsys)
    man = json.loads(_read(str(fake.stage / "licenses" / "manifest.json")))
    vc = next(c for c in man["components"] if c["name"] == "vc")
    assert vc["metapackage"] is True and vc["texts"] == []
    assert "metapackage: installs no files" in _read(str(fake.stage / "licenses" / "THIRD_PARTY_NOTICES.txt"))
    assert gl.verify_bundle(str(fake.stage)) == []


def test_a_package_that_installs_files_is_not_a_metapackage(fake, capsys):
    fake.pkg(fake.rt, "vc", "14.5", "BSD-3-Clause", files=["Library/bin/x.dll"], texts=False)
    _fails(fake, capsys, expect="vc 14.5: no licence text")
    # and a record with no `files` key at all is not read as "installs nothing"
    meta = fake.rt / "conda-meta" / "vc-14.5-0.json"
    m = json.loads(meta.read_text())
    del m["files"]
    meta.write_text(json.dumps(m))
    _fails(fake, capsys, expect="vc 14.5: no licence text")


def test_verification_still_requires_texts_of_a_real_component(fake, capsys):
    assert fake.run() == 0
    manp = fake.stage / "licenses" / "manifest.json"
    man = json.loads(manp.read_text())
    man["components"][0]["texts"] = []
    manp.write_text(json.dumps(man))
    assert any("no licence text recorded" in p for p in gl.verify_bundle(str(fake.stage)))


def test_stage_and_installer_scripts_implement_the_msvc_decision():
    stage = _read(os.path.join(TOOLS, "stage.ps1"))
    assert "[switch] $ExcludeMsvcRuntime" in stage and '@("vc14_runtime", "vcomp14")' in stage
    assert "-ExcludeMsvcRuntime: still staged" in stage and "-ExcludeUcrt: still staged" in stage
    # both "still staged" checks run only after EVERY package removal
    assert stage.index("$x = Remove-CondaPackage $rt $p") < stage.index("-ExcludeUcrt: still staged")
    mk = _read(os.path.join(TOOLS, "make_installer.ps1"))
    assert "Get-RequiredVcRuntime" in mk and "Get-AuthenticodeSignature" in mk and "-lt $vcNeed" in mk
    assert "@vcDefines" in mk
    iss = _read(os.path.join(ROOT, "desktop", "installer", "tcad.iss"))
    assert "function PrepareToInstall" in iss and "'runas'" in iss and "Flags: dontcopy" in iss
    assert "VC\\Runtimes\\x64" in iss
    assert 'Record "vcruntime"' in _read(os.path.join(TOOLS, "verify_install.ps1"))

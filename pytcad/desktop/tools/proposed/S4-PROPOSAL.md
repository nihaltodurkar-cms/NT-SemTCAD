# S4 evidence-only proposal for the 9 remaining licence blockers — NOT APPLIED

Nothing here is applied. `license_policy.json`, `gen_licenses.py`, `stage.ps1` are unchanged. `S4-entries.proposed.json`
holds the exact policy JSON; `texts/` holds the exact texts (`texts/SHA256SUMS`). Every hash below was recomputed from
the file on disk when this was written.

**Identities (v3, 2026-09-29):** re-pinned to the staged conda-meta inventory you confirmed: libsqlite 3.53.4 hf5d6505_1,
libwinpthread 12.0.0.r4.gg4f2fc60ca h57928b3_10, pyamg 5.3.0 py314hbac2fa4_1, tk 8.6.13 h967ab96_4, tzdata 2026c h151e31d_0,
vc 14.5 ha367084_41, vc14_runtime / vcomp14 14.51.36247 habf1de7_41. v1/v2 were WRONG for libwinpthread (cited 9.0.0.6454…/_0)
and tk (cited 9.0.4/h230c182_1, and libtk/libtcl, which are not in the stage — those entries are removed). Each identity was
re-verified this time by downloading the exact conda-forge archive and comparing its sha256 with conda-forge's repodata.
`libfreetype6` is NOT in the list you confirmed: its pin is still from conda-forge only and must be confirmed.

## Gate changes these proposals need (one change set, all currently unmade)

| # | Where | Change |
|---|---|---|
| G1 | `gen_licenses.py` `load_policy` + `component()` | New optional policy section `supplemental_texts` (per package: `version`, `build`, `text.file`, `text.sha256`, `source`, `basis_type`, `reason`, `reviewed_by`; all required). `component()` consults it ONLY when `texts` is empty after restore. Accepted only if installed version (and build, when pinned) equals the pin, the vendored file exists under `desktop/tools/notices/upstream/<pkg>/`, and its sha256 equals the pin; otherwise the row keeps failing `no licence text`. The text is copied into `licenses/texts/` and the row's `texts_from` says `supplemental: <source>`. |
| G2 | `load_policy` + `component()` | `reviewed` entries may carry `version` and `text_sha256`; when present they are ENFORCED (installed version equals pin; the package's own licence file with that sha256 is among `texts`). Today an entry is keyed by name only, so a version bump silently inherits the review. Unknown keys are currently accepted and ignored, hence the pins in `S4-entries.proposed.json` are inside `basis` until G2 exists. |
| G3 | `component()` | Metapackage rule (item 5): conda-meta `files == []` → row flagged `metapackage`, no licence text required, listed in `manifest.json` under `metapackages`. |
| G4 | `write_bundle` | When `libfreetype6` is staged, `THIRD_PARTY_NOTICES.txt` gets the FreeType credit line (item 4). |
| G5 | tests in `gui/tests/test_desktop_licenses.py` | Mutation-checked: wrong sha → still FAIL; wrong version/build → still FAIL; missing vendored file → FAIL; supplemental never applies when the archive HAS a text; `reviewed` pin mismatch → FAIL; metapackage rule does not excuse a package with files; `--verify-bundle` re-checks supplemental hashes. |

## 1. libsqlite — supplemental text
- **Package (identity confirmed):** libsqlite 3.53.4 build hf5d6505_1, archive sha256 `0a45d7c0f20146fff787a106f8fa187872e309c70975ff8f0936e188914c26ad`. No `info/licenses`; conda licence string `blessing`.
- **Source:** lines 1–9 of `Library/include/sqlite3.h` in the package's own `libsqlite-3.53.4-hf5d6505_1.conda`
  (archive sha256 `0a45d7c0f20146fff787a106f8fa187872e309c70975ff8f0936e188914c26ad`), verbatim. Basis type: package text.
- **File:** `notices/upstream/libsqlite/blessing.txt` (from `texts/libsqlite-blessing.txt`), 291 B,
  sha256 `fa1f26185c2210e7b6e333829a03b8ca6a4180b36381d4b1b6dc1c09077c2f87`.
- **Rationale:** SQLite is dedicated to the public domain by its author; the only text carried with the code is this
  blessing. The independent statement (sqlite.org/copyright.html) could not be fetched from the cloud session — a person
  may add it as a second source; not needed for the entry.
- **Change:** G1 + the `supplemental_texts.libsqlite` block in `S4-entries.proposed.json`; vendor the file.

## 2. libwinpthread 12.0.0.r4.gg4f2fc60ca (h57928b3_10) — text NOT ESTABLISHED
- **Package facts (verified):** archive sha256 `0fccf2d17026255b6e10ace1f191d0a2a18f2d65088fd02430be17c701f8ffe0` (equals
  repodata); installs only `Library/bin/libwinpthread-1.dll`; no `info/licenses`. Conda licence: **`MIT AND BSD-3-Clause-Clear`**
  (the earlier draft's "MIT + BSD-3-Clause-style" wording is superseded). It is built by conda-forge's `m2w64-sysroot`
  recipe (version `12.0.0.r4.gg4f2fc60ca`), whose `about.json` declares two licence files that never reached the package:
  `mingw-w64-libraries/winpthreads/COPYING` and `COPYING.MinGW-w64/COPYING.MinGW-w64.txt`.
- **Searched for an authoritative copy of those two files at source commit `4f2fc60ca`, and did not find one:**
  - not in `libwinpthread` builds _8, _9, _10, nor in `winpthreads-devel` _10 (same recipe/version): none has `info/licenses`;
  - the only other conda-forge package (`mingw-w64-ucrt-x86_64-libwinpthread-git`) is a different commit (`r2.ggc561118da`) and
    uses a non-standard archive layout I could not read — it would not be evidence for this commit anyway;
  - SourceForge (project's own git) answered 404 for the commit path and 403 for git; MSYS2 repos 403. The GitHub mirror is
    outside this session's scope and I did not use it this time.
- **The earlier file is demoted, not used:** `texts/UNVERIFIED-reference/libwinpthread-COPYING.mingw-w64-master-mirror`
  (sha256 `63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177`) came from a mirror's master branch, not this
  commit, and there is a second required file I never had (COPYING.MinGW-w64.txt). It shows what to expect (an MIT-style
  notice plus a Lockless Inc. BSD-3-Clause-style one), not what to ship.
- **Evidence required (you, from any clone of mingw-w64, e.g. `git clone https://git.code.sf.net/p/mingw-w64/mingw-w64`):**
  ```
  git -C mingw-w64 cat-file -t 4f2fc60ca            # must print: commit  (this is the version's 'g4f2fc60ca')
  git -C mingw-w64 show 4f2fc60ca:mingw-w64-libraries/winpthreads/COPYING       > COPYING
  git -C mingw-w64 show 4f2fc60ca:COPYING.MinGW-w64/COPYING.MinGW-w64.txt       > COPYING.MinGW-w64.txt
  (Get-FileHash COPYING, COPYING.MinGW-w64.txt -Algorithm SHA256)
  ```
  Send back both hashes (and the contents if they differ from the reference). Note the DLL itself is built by
  conda-forge; the honest statement is "source at that commit + conda's declared licence", which is what these give.
- **Change:** G1 entry with BOTH files (each with its own sha256), pinned to version+build; `S4-entries.proposed.json` carries a
  `STATUS: TEXT NOT ESTABLISHED` stub with `<REQUIRED>` hashes. Nothing can be applied for this package until then.

## 3. pyamg 5.3.0 (py314hbac2fa4_1) — supplemental text
- **Package (verified):** archive sha256 `744eb20e5b148e9cff9a011ca9cacc9e242595e03f28332e6ce12e324cc3407c`, conda licence `MIT`, no `info/licenses`.
- **Source:** `info/licenses/LICENSE.txt` of the SAME release's sibling build `pyamg-5.3.0-py314hbac2fa4_2.conda` (archive sha256
  `426c45101c21854d54060536685b91421fd5e8073f75c50423b44f6cbbcd5a46`), 1088 B, sha256
  `853c14468ce2622c0e58310ca0823ee10d342897b17d28b8616a4f92fccc106c` (`texts/pyamg-LICENSE.txt`); the project's own MIT LICENSE.txt.
- **Rationale:** same release, only the text file is missing from `_1`. Do NOT swap builds (the compiled `.pyd` differ).
- **Change:** G1 + `supplemental_texts.pyamg`; vendor `notices/upstream/pyamg/LICENSE.txt`.

## 4. libfreetype6 — supplemental text + required credit line
- **Package:** libfreetype6 2.14.3 build hdbac1cb_2 (CONFIRM), licence `GPL-2.0-only OR FTL` (used under FTL).
- **Source:** `info/licenses/docs/FTL.TXT` of `freetype-2.14.3-h57928b3_2.conda` (archive sha256
  `32f7dd8ffe62fd485e9e6570e871f5be45af74c84fde62241a2417046a373efd`; same recipe/version as libfreetype6, whose own
  archive sha256 is `cbc65085…65ce`), 6743 B, sha256 `5a5ee54c5001bbad1cdc1a57cc3dd4c42199b2da09d39c7ee41fab002d02967f`;
  identical to FreeType's `docs/FTL.TXT`. Basis type: package text of a sibling output.
- **Rationale:** FTL section 2 requires documentation to state that the software is based in part on the work of the
  FreeType Team. Required notices addition (G4): "Portions of this software are based on the work of the FreeType Team
  (https://www.freetype.org)."
- **Change:** G1 + G4 + `supplemental_texts.libfreetype6`; vendor `notices/upstream/libfreetype6/FTL.TXT`.

## 5. vc — structural zero-file / metapackage handling
- **Fact (verified against repodata):** `vc` 14.5 build ha367084_41 (archive sha256 `35444c55…b79c`, licence `BSD-3-Clause`, depends `vc14_runtime >=14.51.36247`) is a metapackage: it installs no files, so its archive has no
  licence text and nothing of it ships. (Confirm in your conda-meta: `files: []`, and its licence string.)
- **Proposal (G3):** in `component()`, if the conda-meta record has a `files` key that is an EMPTY list, the "no licence
  text" problem is not raised; the row is marked `metapackage`, the licence string is still classified and still gated,
  and `manifest.json` lists it under `metapackages` with its version/build. What it stands for is covered by the
  `vc14_runtime` row (item 8). No policy entry, no exception by name.
- **Safety properties for G5:** a package with ≥1 file and no text still fails; a record with no `files` key (unknown)
  still fails; a metapackage whose licence string is unclassifiable still fails.

## 6. tk 8.6.13 (h967ab96_4) — reviewed entry (TCL)
- **Identity (verified):** win-64 archive sha256 `f19618a3a82cc483dacf1c70a30367ee7b0c7e71b90f1f80e14feff44fbd3688` (equals
  repodata), conda licence `TCL`. One package bundling Tcl and Tk: there is no `libtk`/`libtcl` in your stage, so the earlier
  three entries collapse to ONE (`tk`).
- **Text (the 8.6.13 package's own):** `info/licenses/tcl8.6.13/license.terms`, 2255 B, sha256
  `c0a69a2bfd757361ec7e6143973b103c90409316b49e9c88db26ad6388e79f16`; (`Library/lib/tk8.6/license.terms` is a different file: Tk's own text.)
  I compared it byte-for-byte with the 9.0.4 file used in v1/v2: identical — the hash did not change, but this entry now
  rests on the 8.6.13 archive, not the 9.0.4 one.
- **Text says (Tcl core only):** permission to use, copy, modify, distribute and license "for any purpose, provided that existing copyright
  notices are retained in all copies and that this notice is included verbatim in any distributions", no royalty → `permissive`.
- **Bundled third-party components: ESTABLISHED in `TK-BUNDLED-EVIDENCE.md`.** `license.terms` (Tcl's) does not cover them: zlib 1.2.13,
  SQLite 3.40.0, itcl 4.2.3, tdbc/tdbcpostgres 1.1.5 and thread 2.8.8 each have their own notice, and Tk's own `license.terms` (which I earlier
  mis-identified as the same file) differs from Tcl's. Seven further texts are needed; the tk entry is on HOLD until the gate can carry them.
- **Entry:** one `reviewed.tk` block in `S4-entries.proposed.json`, pin `{version 8.6.13, build h967ab96_4, text_sha256 c0a69a2b…79f16}` (enforced only once G2 exists).

## 7. tzdata 2026c (h151e31d_0) — reviewed entry (LicenseRef-Public-Domain)
- **Identity (verified now, noarch archive):** sha256 `b928c30ddcb0e3f544c6eade8352737e6e610e263276b90232db6a578ef899d8`;
  `info/licenses/LICENSE` 252 B, sha256 `0613408568889f5739e5ae252b722a2659c02002839ad970a63dc5e9174b27cf`
  (`texts/tzdata-LICENSE`); `info/paths.json` lists 606 paths and NO `date.c`, `newstrftime.3` or `strftime.c` (checked
  by name in this archive — the remaining check is only that your staged conda-meta `files` agrees).
- **Text says:** "Unless specified below, all files in the tz code and data (including this LICENSE file) are in the public
  domain"; the BSD 3-clause carve-out is for those three files, absent here. Class `permissive`.

## 8. vc14_runtime / vcomp14 — evidence required (no entry proposed)
Not obtainable from the cloud session; a person with the VS 2026 installation must supply, and the entry stays unwritten
until they do:
1. **Edition and licence:** the exact Visual Studio 2026 edition (you reported Community); staged identities vc14_runtime/vcomp14 14.51.36247 habf1de7_41, archive sha256s `4e4cb599…48f8` / `731e0433…44d1` and its Software License Terms,
   "Distributable Code" section: document URL, section title, retrieval date, file sha256.
2. **The list:** the REDIST list that section references (online list or `redist.txt`) for MSVC 14.51, saved as a file,
   with URL/section, fed to `redist_table.py --redist-list … --source-url … --source-section …`. Required output: every
   staged MSVC DLL `LISTED`, same hash and version as VS's copy (you reported this for the copies; the LISTED column is what
   is missing; `inRedistTxt` was blank because no `redist*.txt` names DLLs there).
3. **Scope decision:** that the intended distribution (individual/academic research) is within the edition's terms and
   the condition to revisit before commercial distribution (plan §12 item 7) is accepted; a person, not a script.
4. **Not usable as basis:** the package's own `LICENSE.TXT` (sha256 `ed29042f…3159`), whose scope clause forbids combining
   the software with your own application.
5. **Then** the entry is `vc14_runtime` and `vcomp14` `reviewed`, class `proprietary-redistributable`, basis = the
   Distributable Code document + REDIST-list file sha256 + the `msvc-table.md` produced by `redist_table.py` (a draft with a deliberately blank
   basis existed only in the session scratchpad; it is not in the repo).
   With G2, pinned to version 14.51.x + the sha256 of each staged DLL is preferable to a name-only entry.

## Order and what would remain
Applying items 1, 3–7 (with G1–G5) leaves items 2 (libwinpthread text) and 8 (Microsoft) open, plus the tk third-party-DLL question in item 6. Item 2 (libwinpthread) is NOT ready: it needs the two mingw-w64 files at commit 4f2fc60ca (item 2). After any apply: rerun `gen_licenses.py` on the real stage, then S2/S3 gates for the rebuilt stage — S4 closes only
on zero FAILs and a valid bundle on Windows.

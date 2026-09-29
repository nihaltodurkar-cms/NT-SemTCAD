# S4 evidence-only proposal for the 9 remaining licence blockers — NOT APPLIED

Nothing here is applied. `license_policy.json`, `gen_licenses.py`, `stage.ps1` are unchanged. `S4-entries.proposed.json`
holds the exact policy JSON; `texts/` holds the exact texts (`texts/SHA256SUMS`). Every hash below was recomputed from
the file on disk when this was written.

**Unverified in this document** (the Windows log was reported as a summary, not pasted): the exact version/build/archive
sha256 of each package in YOUR environment. Every entry pins the version/build seen in current conda-forge and must be
checked against `s4-evidence.txt` section C / the `gen_licenses-restore.log` before applying. A mismatch means the text
below may not be that build's text.

## Gate changes these proposals need (one change set, all currently unmade)

| # | Where | Change |
|---|---|---|
| G1 | `gen_licenses.py` `load_policy` + `component()` | New optional policy section `supplemental_texts` (per package: `version`, `build`, `text.file`, `text.sha256`, `source`, `basis_type`, `reason`, `reviewed_by`; all required). `component()` consults it ONLY when `texts` is empty after restore. Accepted only if installed version (and build, when pinned) equals the pin, the vendored file exists under `desktop/tools/notices/upstream/<pkg>/`, and its sha256 equals the pin; otherwise the row keeps failing `no licence text`. The text is copied into `licenses/texts/` and the row's `texts_from` says `supplemental: <source>`. |
| G2 | `load_policy` + `component()` | `reviewed` entries may carry `version` and `text_sha256`; when present they are ENFORCED (installed version equals pin; the package's own licence file with that sha256 is among `texts`). Today an entry is keyed by name only, so a version bump silently inherits the review. Unknown keys are currently accepted and ignored, hence the pins in `S4-entries.proposed.json` are inside `basis` until G2 exists. |
| G3 | `component()` | Metapackage rule (item 5): conda-meta `files == []` → row flagged `metapackage`, no licence text required, listed in `manifest.json` under `metapackages`. |
| G4 | `write_bundle` | When `libfreetype6` is staged, `THIRD_PARTY_NOTICES.txt` gets the FreeType credit line (item 4). |
| G5 | tests in `gui/tests/test_desktop_licenses.py` | Mutation-checked: wrong sha → still FAIL; wrong version/build → still FAIL; missing vendored file → FAIL; supplemental never applies when the archive HAS a text; `reviewed` pin mismatch → FAIL; metapackage rule does not excuse a package with files; `--verify-bundle` re-checks supplemental hashes. |

## 1. libsqlite — supplemental text
- **Package:** libsqlite 3.53.4 build hf5d6505_1 (CONFIRM). Archive has no `info/licenses`; licence string public-domain.
- **Source:** lines 1–9 of `Library/include/sqlite3.h` in the package's own `libsqlite-3.53.4-hf5d6505_1.conda`
  (archive sha256 `0a45d7c0f20146fff787a106f8fa187872e309c70975ff8f0936e188914c26ad`), verbatim. Basis type: package text.
- **File:** `notices/upstream/libsqlite/blessing.txt` (from `texts/libsqlite-blessing.txt`), 291 B,
  sha256 `fa1f26185c2210e7b6e333829a03b8ca6a4180b36381d4b1b6dc1c09077c2f87`.
- **Rationale:** SQLite is dedicated to the public domain by its author; the only text carried with the code is this
  blessing. The independent statement (sqlite.org/copyright.html) could not be fetched from the cloud session — a person
  may add it as a second source; not needed for the entry.
- **Change:** G1 + the `supplemental_texts.libsqlite` block in `S4-entries.proposed.json`; vendor the file.

## 2. libwinpthread — supplemental text (weakest evidence)
- **Package:** libwinpthread 9.0.0.6454.b4445ee52.1 build h57928b3_0 (CONFIRM). Archive (sha256 `e94e8659…5d28`) has no licence.
- **Source:** mingw-w64 `mingw-w64-libraries/winpthreads/COPYING`, 2883 B, sha256
  `63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177`. It has TWO notices (mingw-w64 MIT terms and a
  BSD-3-Clause-style notice for code derived from Lockless Inc.); conda's `MIT` understates it, so the whole file ships.
- **Evidence gap:** this came through the session's web proxy from a GitHub mirror, and no conda package carries it.
  Required before applying: a person compares it byte-for-byte (same sha256) with the SourceForge original or the
  `COPYING` in an MSYS2 `mingw-w64-x86_64-libwinpthread` source package for the same 9.0.0 release, and records that.
  If it cannot be verified, ship nothing supplemental and instead drop the DLL from the runtime (not proposed).
- **Change:** G1 + `supplemental_texts.libwinpthread`; vendor `notices/upstream/libwinpthread/COPYING`.

## 3. pyamg — supplemental text
- **Package:** pyamg 5.3.0 build py314hbac2fa4_1 (CONFIRM: the sibling `_2` build ships the text, `_1` does not).
- **Source:** `info/licenses/LICENSE.txt` of `pyamg-5.3.0-py314hbac2fa4_2.conda` (archive sha256
  `426c45101c21854d54060536685b91421fd5e8073f75c50423b44f6cbbcd5a46`), 1088 B, sha256
  `853c14468ce2622c0e58310ca0823ee10d342897b17d28b8616a4f92fccc106c`; the same file the project publishes (MIT).
- **Rationale:** same release, licence declared MIT in this build's about.json; only the text file is missing from `_1`.
  Do NOT swap to build `_2` instead (all 9 compiled `.pyd` differ between the builds). If your runtime already has `_2`,
  the mechanical restore should have found the text — check the log before applying anything.
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
- **Fact:** `vc` (conda-forge) is a metapackage: it pins `vc14_runtime` and installs no files, so its archive has no
  licence text and nothing of it ships. (Confirm in your conda-meta: `files: []`, and its licence string.)
- **Proposal (G3):** in `component()`, if the conda-meta record has a `files` key that is an EMPTY list, the "no licence
  text" problem is not raised; the row is marked `metapackage`, the licence string is still classified and still gated,
  and `manifest.json` lists it under `metapackages` with its version/build. What it stands for is covered by the
  `vc14_runtime` row (item 8). No policy entry, no exception by name.
- **Safety properties for G5:** a package with ≥1 file and no text still fails; a record with no `files` key (unknown)
  still fails; a metapackage whose licence string is unclassifiable still fails.

## 6. tk / libtk / libtcl — reviewed entries (TCL)
- **Package text:** `info/licenses/tcl9.0.4/license.terms`, 2255 B, sha256
  `c0a69a2bfd757361ec7e6143973b103c90409316b49e9c88db26ad6388e79f16` (copy: `texts/tcl-license.terms`). Versions
  tk|libtk|libtcl 9.0.4 build h230c182_1 (CONFIRM).
- **Text says:** permission to "use, copy, modify, distribute, and license this software … for any purpose, provided that
  existing copyright notices are retained in all copies and that this notice is included verbatim in any distributions",
  no royalty. Class `permissive`.
- **Proposed entries:** three `reviewed` blocks in `S4-entries.proposed.json` (tk, libtk, libtcl), keyed by name, each with
  class, reason, and a basis naming the file + sha256 + exact package build. With G2 the version and `text_sha256` become
  enforced fields instead of prose.
- **Also required:** the reviewer name/date placeholders `<REVIEWER>` / `<DATE>` filled by a person.

## 7. tzdata — reviewed entry (LicenseRef-Public-Domain)
- **Package text:** `info/licenses/LICENSE`, 252 B, sha256
  `0613408568889f5739e5ae252b722a2659c02002839ad970a63dc5e9174b27cf` (`texts/tzdata-LICENSE`). tzdata 2026c build
  h151e31d_0 (CONFIRM).
- **Text says:** "Unless specified below, all files in the tz code and data (including this LICENSE file) are in the public
  domain"; date.c, newstrftime.3, strftime.c are BSD-3-Clause if present.
- **Rationale:** the conda package installs data only (`share/zoneinfo/*`, 606 paths in the build examined), none of the
  three BSD files. Class `permissive`. **Evidence still required:** the installed file list of YOUR build
  (`conda-meta/tzdata-*.json` `files`) checked for those three names — a one-line check; if any is present the entry
  must change.

## 8. vc14_runtime / vcomp14 — evidence required (no entry proposed)
Not obtainable from the cloud session; a person with the VS 2026 installation must supply, and the entry stays unwritten
until they do:
1. **Edition and licence:** the exact Visual Studio 2026 edition (you reported Community) and its Software License Terms,
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
Applying items 1–7 (with G1–G5) leaves ONLY item 8 open. Item 2 additionally waits on the byte-level check of the mingw-w64
COPYING. After any apply: rerun `gen_licenses.py` on the real stage, then S2/S3 gates for the rebuilt stage — S4 closes only
on zero FAILs and a valid bundle on Windows.

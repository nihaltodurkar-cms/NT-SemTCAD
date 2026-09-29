# tk 8.6.13 (h967ab96_4): multi-licence representation — evidence-only proposal, NOT APPLIED

Nothing is applied. `license_policy.json`, `gen_licenses.py`, `stage.ps1` and the gate are unchanged. `tk`, `libwinpthread` and the
Microsoft runtime remain UNRESOLVED. Files: `S4-tk-multilicense.proposed.json` (the exact entry), `texts/tk-bundled/` (the texts +
`SHA256SUMS`), `check_tk_partition.py` (read-only coverage checker), `TK-BUNDLED-EVIDENCE.md` (how each licence was established).

## 1. The texts, their sources, hashes and coverage
Package pin: version `8.6.13`, build `h967ab96_4`, archive sha256 `f19618a3a82cc483dacf1c70a30367ee7b0c7e71b90f1f80e14feff44fbd3688`
(1103 files in the package's `info/paths.json`). Upstream source archives (observed, SourceForge Tcl project): tcl8.6.13-src.tar.gz
`43a1fae7…caf066`, tk8.6.13-src.tar.gz `2e65fa06…ce0675` (not compared with the recipe's pinned hashes: out of scope).

| id | source kind and path | upstream | sha256 | files covered | staged binaries it covers |
|---|---|---|---|---|---|
| tcl | archive `info/licenses/tcl8.6.13/license.terms` | tcl8.6.13/license.terms (identical) | `c0a69a2bfd757361ec7e6143973b103c90409316b49e9c88db26ad6388e79f16` | 858 | tcl86t.dll, tclsh.exe, tclsh86.exe, tclsh86t.exe, dde1.4/tcldde14.dll, reg1.3/tclreg13.dll, nmake/x86_64-w64-mingw32-nmakehlp.exe |
| tk | payload `Library/lib/tk8.6/license.terms` (staged file) | tk8.6.13/license.terms and xlib/X11/license.terms (identical) | `2cde822b93ca16ae535c954b7dfe658b4ad10df2a193628d1b358f1765e8b198` | 197 | tk86t.dll, wish.exe, wish86.exe, wish86t.exe |
| zlib | vendored `notices/upstream/tk/zlib-1.2.13.zlib.h-licence-header.txt` | tcl8.6.13/compat/zlib/zlib.h lines 1–23 | `0cb543fd7a38b56ddd988def1dfd681ae526809e6fbbe4225a4a9b6259ab9b36` | 1 | Library/bin/zlib1.dll |
| sqlite | vendored `…/sqlite3-3.40.0.license.terms` | pkgs/sqlite3.40.0/license.terms | `66e056b6e8687f32af30d5187611b98b12a8f46f07aaf62f43585f276e8f0ac9` | 3 | sqlite3.40.0/sqlite3400t.dll |
| itcl | vendored `…/itcl-4.2.3.license.terms` | pkgs/itcl4.2.3/license.terms | `b61edfaeead97546bc62b1f205b046f2e440b83bfc517bc0932b93bc3d505865` | 9 | itcl4.2.3/itcl423t.dll |
| tdbc | vendored `…/tdbc-1.1.5.license.terms` | pkgs/tdbc1.1.5/license.terms (byte-identical to tdbcmysql and tdbcodbc) | `fafd6456aa4b7c80c190df86de5bdfab82c8c227a0c348662432c75ceeb2238d` | 21 | tdbc1.1.5/tdbc115t.dll, tdbcmysql1.1.5/tdbcmysql115t.dll, tdbcodbc1.1.5/tdbcodbc115t.dll |
| tdbcpostgres | vendored `…/tdbcpostgres-1.1.5.license.terms` | pkgs/tdbcpostgres1.1.5/license.terms | `d3aa1cf53b96e69469d46c62bceeebadaa3ecb9699b73962bb207246f4288a08` | 4 | tdbcpostgres1.1.5/tdbcpostgres115t.dll |
| thread | vendored `…/thread-2.8.8.license.terms` | pkgs/thread2.8.8/license.terms | `0a03981c40f7813ce6ddbdfce9882020bcfd696ea044b21ef07619ed9b86abae` | 7 | thread2.8.8/thread288t.dll |
| **tdbcsqlite3 (9th)** | vendored `…/tdbcsqlite3-1.1.5.license.terms` | pkgs/tdbcsqlite3-1.1.5/license.terms | `37186e29fd02702179f7c5e9fc630840429319281fcf5a282caaf7fcb0a1c7f4` | 2 | none (script module `tcl8/8.6/tdbc/sqlite3-1.1.5.tm` + man page) |

(The itcl/tdbc/thread rows include their `.tcl` scripts, `pkgIndex.tcl` and headers; exact `covers` globs are in the JSON.)

**Two findings beyond the eight requested** (both from running the coverage partition over all 1103 files, `check_tk_partition.py`):
1. A ninth text is needed: the tdbc::sqlite3 script module and man page carry their own (Scriptics) `license.terms`.
2. One file has no upstream text at all: `etc/conda/test-files/tk/0/hello.tcl`, a conda-forge recipe test script. Proposed as an
   explicit, named `uncovered_allowed` entry with a stated reason — it needs YOUR decision (the alternative is to keep it out of the stage).
   Not a blanket exception: it names one path and is re-checked on every run.

**Residuals I am NOT resolving with a text (flagged for a decision):**
- 13 `Library/include/X11/*.h` files: each header carries its own in-file MIT/X notice; the Tk `license.terms` is placed with them in
  the source. Proposed: covered by the `tk` text AND recorded as "in-file notices retained" — but a person should confirm.
- Code compiled into `tcl86t.dll` with in-file notices only (e.g. libtommath, `mp_*` symbols present; the Tcl tarball has no LICENSE
  file for it). The Tcl `license.terms` says its terms apply "unless explicitly disclaimed in individual files"; per-file notices are
  what the source form carries. The gate cannot see them; this is a scoping limit to state in the notices, not something a text file fixes.
- Tcl's `tcl8.6/tzdata/*` (608 files: Tcl's generated timezone data, IANA-derived): covered by `tcl` in the partition; whether IANA's public-domain
  statement needs its own text is unresolved (the separate `tzdata` conda package is a different item).

## 2. Minimum G1 / schema change to let one package carry several texts
Today a row already lists several texts, but only from `info/licenses`, and a supplemental text is a single file. The minimum:

**Policy schema (`supplemental_texts[<name>]`, loaded and validated in `load_policy`):**
```
pin:        { version, build, archive_sha256 }          all three REQUIRED (archive_sha256 = conda-meta 'sha256', 64 hex)
class:      one of the permissive/... classes (as `reviewed`)         reason/basis required
coverage:   "total" | "binaries"      total = every file in conda-meta `files` covered exactly once; binaries = every .dll/.exe/.pyd
uncovered_allowed: [ { path, reason } ]                  exact paths only, no globs
texts:      [ { id, source:{kind, path, upstream}, sha256, covers[], except[], covers_binaries[] } ]
            id unique in the package; kind ∈ archive | payload | vendored; sha256 64 hex; covers non-empty
```
- `kind: archive` → `<pkgdir>/info/licenses/<path>`; `payload` → `<stage runtime>/<path>`, which must also be in the package's conda-meta `files`;
  `vendored` → `desktop/tools/<path>` (under `notices/upstream/` only; `..`/absolute/drive paths rejected).
- A text is accepted only when its bytes hash to `sha256`. There is no fallback to another file and no "close enough".

**`component()`:**
1. If the package has an entry: require `meta.version == pin.version`, `meta.build == pin.build`, `meta.sha256 == pin.archive_sha256`; else keep the
   original failure and add "supplemental entry does not match this build".
2. Resolve and hash-check every text (kind rules above). Any miss → problem, text not used.
3. Evaluate coverage against conda-meta `files` with `covers`/`except` globs (case-sensitive on the conda-meta names, `/` separators): report uncovered,
   multiply-covered, covers matching nothing, stale `uncovered_allowed`, and any binary not in some `covers_binaries`.
4. An entry applies only when the archive/cache has no texts of its own for that role (never overrides real package texts silently); for `tk`, the
   `archive` text is listed inside the entry itself so it is pinned like the rest.
5. Row gets `texts` = the resolved list, each `{id, file, sha256, source_kind, covers_summary, binaries}`; `texts_from` = `supplemental (pinned)`.

**`write_bundle`:** files are written as `texts/<name>-<version>/<id>-<basename>` (today two `license.terms` would become `license.terms` and `2-license.terms`, which
loses which is which); manifest keeps `covers`/`binaries` per text. **`THIRD_PARTY_NOTICES.txt`:** one block per text naming what it covers.
**`verify_bundle`:** re-hash every text; for a component with coverage, check every `covers_binaries` path exists in the installed tree and every DLL/EXE
owned by the component is in some text's binaries (so an installed file added after generation is caught).

## 3. Preserving strict pinning
- Two independent pins: the package (`version`, `build`, `archive_sha256`) and each text (`sha256`). A version/build/archive change fails the entry, it never inherits.
- The `reviewed` entry for `tk` (G2) additionally pins `text_sha256` list = the nine hashes above, so a reviewer's decision is tied to exactly these bytes.
- No policy field can widen coverage silently: `coverage:"total"` fails a new file; `uncovered_allowed` is exact-path; globs must each match something.

## 4. Mutation tests required (each must FAIL the gate; unmodified control must pass)
1. `pin.version`, `pin.build` or `pin.archive_sha256` changed → entry rejected, original "no licence text"/coverage failure remains.
2. A vendored file's bytes changed by one byte → sha mismatch.  3. A vendored file deleted → FAIL.
4. `payload` file (`tk8.6/license.terms`) altered in the stage → FAIL.  5. `payload` path not in conda-meta `files` → FAIL.
6. `archive` text absent from `info/licenses` → FAIL, no fallback.
7. Drop one text (e.g. zlib) → its DLL uncovered → FAIL (proven: dropping zlib gives "zlib1.dll covered by no licence text").
8. Overlap: add a glob to a second text → "covered by [..]" FAIL (proven).  9. Remove an `except` → overlap FAIL (proven).
10. A `covers` glob matching nothing → FAIL (stale entry).  11. A new `.dll` in the package not in any `covers_binaries` → FAIL.
12. `uncovered_allowed` path not in the package → FAIL; a glob in it → rejected at load.
13. Duplicate `id`, empty `covers`, non-hex sha, unknown `kind`, `..`/absolute/drive-letter `path` → rejected in `load_policy`.
14. Entry keyed for package A does not apply to package B, nor to A at another version/build.
15. `--verify-bundle`: text modified → FAIL; a `covers_binaries` file missing from the installed tree → FAIL; an extra installed DLL of the component → FAIL.
16. Basename collision: two texts named `license.terms` land as distinct `<id>-license.terms` files and both are recorded.
17. Control: the real `tk` entry against the real package file list → exit 0 (today only the coverage part is runnable here:
    `python check_tk_partition.py --spec S4-tk-multilicense.proposed.json --files <info/paths.json | conda-meta/tk-*.json>` → "partition OK", 1103 files;
    mutations 7–9 above were run against it and fail as required).

## 5. Still open (unchanged)
`tk` reviewed entry (this representation is the precondition, not the approval; plus the decisions in §1: hello.tcl, X11 headers, in-DLL notices, Tcl tzdata),
`libwinpthread` (mingw-w64 commit files), Microsoft runtime (Distributable Code list). S4 stays open.

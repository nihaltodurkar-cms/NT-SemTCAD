# tk 8.6.13 (h967ab96_4): licences of the components it bundles — evidence only, NOT APPLIED

Package archive sha256 `f19618a3a82cc483dacf1c70a30367ee7b0c7e71b90f1f80e14feff44fbd3688` (= conda-forge repodata). Ownership of the
eight DLLs below by exactly this package: confirmed by you. Everything else here was measured in the cloud session from
(a) the archive's own payload and (b) the official Tcl/Tk 8.6.13 source distributions, downloaded from the Tcl project's
SourceForge release area:

| source archive | sha256 (observed) |
|---|---|
| tcl8.6.13-src.tar.gz | `43a1fae7412f61ff11de2cfd05d28cfc3a73762f354a417c62370a54e2caf066` |
| tk8.6.13-src.tar.gz  | `2e65fa069a23365440a3c56c556b8673b5e32a283800d8d9b257e3f584ce0675` |

I could not compare those two hashes with the ones pinned in conda-forge's tk recipe (its feedstock is on GitHub, out of this
session's scope). Instead the source is tied to the package by content: the source's Tcl `license.terms` is byte-identical to the
package's `info/licenses/tcl8.6.13/license.terms` (`c0a69a2b…79f16`), the source's Tk `license.terms` is byte-identical to the
package's `Library/lib/tk8.6/license.terms` (`2cde822b…e8b198`), and the zlib and SQLite version strings inside the DLLs equal the
bundled source versions (below). That is strong corroboration, not proof that conda built from exactly this tarball.

## Correction to my previous message
I wrote that `Library/lib/tk8.6/license.terms` is "the same file" as Tcl's. That was WRONG. It is **Tk's own** text (2267 B, sha256
`2cde822b93ca16ae535c954b7dfe658b4ad10df2a193628d1b358f1765e8b198`): it differs from Tcl's (2255 B) by naming "Apple Inc." as a
copyright holder and one DFARS clause number (7013 vs 7014). It is in the package payload, NOT in `info/licenses`, so the gate's
bundle (which copies only `info/licenses`) would ship Tcl's text and not Tk's.

## Table

| file / component | upstream source | licence | licence text / notice (file in `texts/`) | sha256 of the text | covered by tk's TCL text (`c0a69a2b…`)? |
|---|---|---|---|---|---|
| `Library/bin/tcl86t.dll` (also `tclsh*.exe`, `dde1.4/tcldde14.dll`, `reg1.3/tclreg13.dll`: Tcl core source) | Tcl 8.6.13 | TCL | `texts/tcl-license.terms` (= package `info/licenses/tcl8.6.13/license.terms`) | `c0a69a2bfd757361ec7e6143973b103c90409316b49e9c88db26ad6388e79f16` | **Yes** — this is that text. |
| `Library/bin/tk86t.dll` (and `wish*.exe`) — not in your eight, but the same finding | Tk 8.6.13 | TCL-family (permissive, Tk's copy of the terms) | `texts/tk-bundled/tk-8.6.13.license.terms` (package payload `Library/lib/tk8.6/license.terms`; identical to the Tk source's `license.terms`) | `2cde822b93ca16ae535c954b7dfe658b4ad10df2a193628d1b358f1765e8b198` | **Not by the Tcl text.** Same terms in substance, but a different notice (adds Apple Inc.); the terms require "this notice … included verbatim in any distributions", so this file must ship too. |
| `Library/bin/zlib1.dll` | zlib **1.2.13** (Oct 13 2022): the DLL contains "deflate 1.2.13 Copyright 1995-2022 Jean-loup Gailly and Mark Adler"; Tcl's `compat/zlib` is 1.2.13 and its win Makefile builds `zlib1.dll` | zlib licence (permissive, own terms: no misrepresentation, mark altered sources, keep notice in source) | `texts/tk-bundled/zlib-1.2.13.zlib.h-licence-header.txt`: lines 1–23 of `compat/zlib/zlib.h` (the licence as zlib ships it in its header) | `0cb543fd7a38b56ddd988def1dfd681ae526809e6fbbe4225a4a9b6259ab9b36` | **No.** Different licence text and different copyright holders (Gailly, Adler). |
| `Library/lib/sqlite3.40.0/sqlite3400t.dll` | Tcl's `pkgs/sqlite3.40.0` (the Tcl SQLite interface + the SQLite amalgamation); the DLL contains "3.40.0 2022-11-16 12:10:08 89c459e7…" | public domain (author disclaims copyright; a blessing, not a licence grant) | `texts/tk-bundled/sqlite3-3.40.0.license.terms` (`pkgs/sqlite3.40.0/license.terms`) | `66e056b6e8687f32af30d5187611b98b12a8f46f07aaf62f43585f276e8f0ac9` | **No.** Opposite mechanism (disclaimer of copyright, no permissions text). |
| `Library/lib/itcl4.2.3/itcl423t.dll` | Tcl's `pkgs/itcl4.2.3` ([incr Tcl] 4.2.3) | Rewritten parts: "BSD license or Public Domain at your choice" (A. P. Wiedemann, 2008); original Lucent Technologies terms (Tcl-style permissive) | `texts/tk-bundled/itcl-4.2.3.license.terms` | `b61edfaeead97546bc62b1f205b046f2e440b83bfc517bc0932b93bc3d505865` | **No.** Different rights holders and a dual-choice clause; only the Lucent part resembles the Tcl text. |
| `Library/lib/tdbc1.1.5/tdbc115t.dll` | Tcl's `pkgs/tdbc1.1.5` (Tcl DataBase Connectivity) | Tcl-style permissive, copyright Kevin B. Kenny and others | `texts/tk-bundled/tdbc-1.1.5.license.terms` | `fafd6456aa4b7c80c190df86de5bdfab82c8c227a0c348662432c75ceeb2238d` | **No** (same terms, different copyright-holder notice). |
| `…/tdbcmysql1.1.5/tdbcmysql115t.dll` | `pkgs/tdbcmysql1.1.5` | same text as tdbc (byte-identical in the source) | same file as tdbc | `fafd6456…238d` | **No** (as tdbc). |
| `…/tdbcodbc1.1.5/tdbcodbc115t.dll` | `pkgs/tdbcodbc1.1.5` | same text as tdbc (byte-identical) | same file as tdbc | `fafd6456…238d` | **No** (as tdbc). |
| `…/tdbcpostgres1.1.5/tdbcpostgres115t.dll` | `pkgs/tdbcpostgres1.1.5` | Tcl-style permissive, copyright Slawomir Cygan and others | `texts/tk-bundled/tdbcpostgres-1.1.5.license.terms` | `d3aa1cf53b96e69469d46c62bceeebadaa3ecb9699b73962bb207246f4288a08` | **No** (different holder). |
| `Library/lib/thread2.8.8/thread288t.dll` | Tcl's `pkgs/thread2.8.8` (Thread extension) | Tcl-style permissive (Regents of UC, Sun, Scriptics and others) | `texts/tk-bundled/thread-2.8.8.license.terms` | `0a03981c40f7813ce6ddbdfce9882020bcfd696ea044b21ef07619ed9b86abae` | **No** (different notice wording: no ActiveState line). |

Also checked: the three `tdbc{mysql,odbc,postgres}` DLLs import only kernel32, the CRT, VCRUNTIME140 (and ws2_32 for postgres): they
do not link MySQL/ODBC/libpq; those client libraries are loaded at run time if present and are NOT in the package, so no further
licence text is triggered by shipping these bindings. (`tdbcsqlite3` is Tcl script only in the source; no DLL.)

## Answer
- The package's `license.terms` (Tcl's) covers the Tcl core only. It does **not** cover the eight DLLs you listed, and not `tk86t.dll`.
- All nine are permissive: seven Tcl-style/BSD-or-public-domain notices (Tcl, Tk, itcl, tdbc×3 sharing one text, tdbcpostgres, thread), the SQLite
  public-domain blessing, and the zlib licence. So classification stays `permissive`; nothing is copyleft or proprietary.
- Separate notice texts are nevertheless REQUIRED for compliance, because the terms ask for "this notice" verbatim: **seven files** beyond Tcl's:
  Tk, zlib, SQLite blessing, itcl, tdbc (one file for three DLLs), tdbcpostgres, thread — all present in `texts/tk-bundled/`.
- A single `reviewed.tk` entry whose text is Tcl's alone would therefore be incomplete. It stays UNAPPLIED. What the gate would need
  (not made): let one package carry several licence texts from named payload files / upstream sources (G1's `supplemental_texts`
  generalised to a list per package, each with path-or-source, sha256 and the DLLs it covers), and have the row list them all.
- Still unverified: that conda-forge built these from exactly the tarballs above (hash comparison with the recipe was out of scope), and the
  content of the zlib text vs the zlib project's own `LICENSE` file (the header is the licence as zlib itself embeds it).

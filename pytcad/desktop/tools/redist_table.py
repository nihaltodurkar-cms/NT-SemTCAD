"""The MSVC-runtime evidence table for S4 (NATIVE-DESKTOP-PLAN.md 26.9.3):

    DLL | staged version | VS version | same hash | REDIST-listed | source URL/section

Built from `s4_evidence.ps1`'s JSON (section B) and the REDIST list YOU supply from Microsoft's "Distributable Code"
page for your Visual Studio edition. Nothing is inferred: without a list the REDIST-listed column says NOT ESTABLISHED,
and with one the source URL and section are required and recorded next to the list file's SHA-256.

    python redist_table.py --evidence build\\s4-evidence\\s4-evidence.json
    python redist_table.py --evidence ... --redist-list vs2026-redist.txt --source-url <url> --source-section "<section>"

The list file is plain text: copy the list from the page. Every `*.dll` name in it counts; a name with `*` or `?`
(e.g. `msvcp140*.dll`) is matched as a wildcard. Exit status 1 if any staged DLL is not on a supplied list.
Stdlib only.
"""
import argparse
import fnmatch
import hashlib
import json
import os
import re
import sys

MIN_NAMES = 5


def parse_list(text):
    """Distinct lower-case DLL names / wildcard patterns in a pasted REDIST list."""
    return sorted({m.group(0).lower() for m in re.finditer(r"[A-Za-z0-9_.\-*?]+\.dll", text, re.I)})


def is_listed(name, patterns):
    return any(fnmatch.fnmatchcase(name.lower(), p) for p in patterns)


def rows_from_evidence(ev):
    """One row per STAGED file (a name can be staged in several places), from section B."""
    stage = ev.get("generated_for", "")
    out = []
    for r in ev["B_msvc"]["comparison"]:
        staged_at = r["stagedAt"]
        rel = os.path.relpath(staged_at, stage).replace("\\", "/") if stage else staged_at
        vs_version = r.get("vsVersion")
        if not vs_version:
            # an older evidence file has no VS version column: "same version" (True) means it equals the staged one
            vs_version = r["version"] if r.get("sameVersionAsVs") else "unknown (re-run s4_evidence.ps1)"
        out.append({"dll": rel, "name": r["file"], "staged_version": r["version"], "vs_version": vs_version,
                    "same_hash": bool(r["sameHashAsVs"]), "in_vs_redist_folder": bool(r["inVsRedistFolder"]),
                    "signature": r.get("signature")})
    return sorted(out, key=lambda x: (x["name"], x["dll"]))


def build(ev, patterns=None, source=None):
    rows = rows_from_evidence(ev)
    for r in rows:
        if patterns is None:
            r["redist_listed"] = "NOT ESTABLISHED (no list supplied)"
            r["source"] = "-"
        else:
            r["redist_listed"] = is_listed(r["name"], patterns)
            r["source"] = source
    return rows


def to_markdown(rows, header=""):
    lines = [header] if header else []
    lines += ["| DLL | staged version | VS version | same hash | REDIST-listed | source URL/section |", "|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['dll']} | {r['staged_version']} | {r['vs_version']} | {r['same_hash']} | {r['redist_listed']} | {r['source']} |")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--evidence", required=True, help="s4-evidence.json from s4_evidence.ps1")
    ap.add_argument("--redist-list", help="text file holding the REDIST list copied from Microsoft's page")
    ap.add_argument("--source-url", help="URL of the page the list was copied from (required with --redist-list)")
    ap.add_argument("--source-section", help="section of that page/licence (required with --redist-list)")
    ap.add_argument("--out", help="also write the table (markdown) here; <out>.json gets the rows")
    args = ap.parse_args(argv)
    with open(args.evidence, "r", encoding="utf-8-sig") as fh:
        ev = json.load(fh)
    patterns, source, header = None, None, "REDIST-list authorisation: NOT ESTABLISHED (no list supplied)\n"
    if args.redist_list:
        if not (args.source_url and args.source_section):
            print("FAIL  --redist-list needs --source-url and --source-section: the table must say where the list came from")
            return 2
        with open(args.redist_list, "rb") as fh:
            raw = fh.read()
        patterns = parse_list(raw.decode("utf-8", errors="replace"))
        if len(patterns) < MIN_NAMES:
            print(f"FAIL  {args.redist_list} names only {len(patterns)} DLL(s): that does not look like a REDIST list")
            return 2
        source = f"{args.source_url} , section: {args.source_section}"
        header = (f"REDIST list: {args.redist_list} (sha256 {hashlib.sha256(raw).hexdigest()}, {len(patterns)} names/patterns)\n"
                  f"Source: {source}\nVisual Studio: {ev['B_msvc'].get('vs', {}).get('displayName', '?')} "
                  f"{ev['B_msvc'].get('vs', {}).get('installationVersion', '')}\n")
    rows = build(ev, patterns, source)
    md = to_markdown(rows, header)
    print(md)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(md)
        with open(args.out + ".json", "w", encoding="utf-8", newline="\n") as fh:
            json.dump({"header": header, "rows": rows}, fh, indent=2)
    bad = [r for r in rows if r["redist_listed"] is False or not r["same_hash"]]
    if patterns is not None and bad:
        print(f"FAIL  {len(bad)} staged DLL(s) are not on the supplied list or differ from Visual Studio's copy")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

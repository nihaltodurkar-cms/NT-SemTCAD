"""Evidence tool: does the proposed multi-licence spec cover every file of a conda package exactly once?

    python check_tk_partition.py --spec S4-tk-multilicense.proposed.json --files <conda-meta/tk-*.json | info/paths.json>

Reads the package's file list (conda-meta 'files', or info/paths.json), applies each text's covers/except globs
('*' within a path segment, '**' any depth), and reports files covered by no text, by more than one, covers that match
nothing, and any binary (.dll/.exe/.pyd) that is not in its text's 'covers_binaries'. Exit 1 on any problem. Read-only.
"""
import argparse, json, re, sys


def rx(pat):
    return re.compile(re.escape(pat).replace(r"\*\*", ".*").replace(r"\*", "[^/]*") + r"\Z")


def load_files(path):
    d = json.load(open(path, encoding="utf-8"))
    if "paths" in d:
        return [p["_path"] for p in d["paths"]]
    return [f.replace("\\", "/") for f in d["files"]]


def check(spec_pkg, files):
    problems, cover = [], {f: [] for f in files}
    allowed = {u["path"] for u in spec_pkg.get("uncovered_allowed", [])}
    for t in spec_pkg["texts"]:
        inc = [rx(c) for c in t["covers"]]
        exc = [rx(e) for e in t.get("except", [])]
        hit = [f for f in files if any(r.match(f) for r in inc) and not any(r.match(f) for r in exc)]
        for c, r in zip(t["covers"], inc):
            if not any(r.match(f) for f in files):
                problems.append(f"{t['id']}: cover '{c}' matches no file of the package")
        for f in hit:
            cover[f].append(t["id"])
        for b in t.get("covers_binaries", []):
            if b not in hit:
                problems.append(f"{t['id']}: declared binary {b} is not covered by its own globs / not in the package")
    for f, ids in cover.items():
        if len(ids) > 1:
            problems.append(f"{f}: covered by {ids}")
        if not ids and f not in allowed:
            problems.append(f"{f}: covered by no licence text")
    bins = [f for f in files if f.lower().endswith((".dll", ".exe", ".pyd"))]
    declared = {b for t in spec_pkg["texts"] for b in t.get("covers_binaries", [])}
    for b in bins:
        if b not in declared:
            problems.append(f"{b}: binary not listed in any text's covers_binaries")
    counts = {t["id"]: sum(1 for f in files if t["id"] in cover[f]) for t in spec_pkg["texts"]}
    return problems, counts


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", required=True)
    ap.add_argument("--files", required=True)
    ap.add_argument("--package", default="tk")
    a = ap.parse_args()
    files = load_files(a.files)
    problems, counts = check(json.load(open(a.spec, encoding="utf-8"))[a.package], files)
    print(f"{len(files)} files; per text: {counts}")
    for p in problems:
        print("FAIL  " + p)
    print("partition OK" if not problems else f"{len(problems)} problem(s)")
    sys.exit(1 if problems else 0)

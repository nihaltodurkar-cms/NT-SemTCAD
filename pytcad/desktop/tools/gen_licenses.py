"""Third-party licence inventory and gate for the staged desktop app
(NATIVE-DESKTOP-PLAN.md section 26.3, P5-S4).

Generates <stage>\\licenses\\ from what is ACTUALLY staged -- never from a hand-kept list:

    THIRD_PARTY_NOTICES.txt   every component, its licence expression, class and licence-text files,
                              plus the LGPL relinking statement when an LGPL component is staged
    manifest.json             the same, machine readable (with SHA-256 of every text), used by
                              --verify-bundle on the INSTALLED copy
    texts\\<name>-<version>\\   each package's own licence files (conda: info\\licenses\\**)

    python gen_licenses.py --stage dist\\TCAD --gui-env <tcad-gui prefix> [--addon] [--policy P] [--out O]
    python gen_licenses.py --verify-bundle <installed app dir>          (stdlib only; no conda needed)

Where components come from:
    runtime\\   every package in runtime\\conda-meta\\*.json (a conda-pack'd env keeps conda-meta)
    the app     every DLL staged next to the exe (Qt, VTK, ADS, the MSVC runtime, ...) is traced to the
                tcad-gui conda package that owns it (that env's conda-meta `files` lists) -- the same
                env stage.ps1 copied it from -- plus the policy's `embedded` packages (header-only code
                compiled into tcad_desktop.exe, e.g. nlohmann_json)

The gate -- every problem is printed at once, exit status 1 if there is any (no bundle is written):
    * a package with no licence string, or one that cannot be classified and has no reviewed policy entry
    * a package with no licence TEXT on disk (its extracted conda package dir must still exist)
    * a strong-copyleft (GPL/AGPL) package in the base runtime -- allowed only with --addon AND a policy
      `addon_copyleft` entry (decision 26.4-4: gmsh GPL-2.0+, tetgen AGPL-3.0)
    * a staged DLL that no package owns and the policy does not list
    * a DLL of a GPL/commercial-only Qt module (section 7.3: Charts, Graphs, Data Visualization)
    * a runtime binary (.dll/.pyd) that no conda package owns, or a pip-installed distribution

Licence CLASSIFICATION is mechanical (an SPDX-ish expression: OR = the least restrictive alternative,
AND = the most restrictive, `X WITH <exception>` = runtime-exception). It is an engineering inventory,
not legal advice; anything it cannot classify needs a reviewed entry in license_policy.json with a
reason, made by a person.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
DEFAULT_POLICY = os.path.join(TOOLS, "license_policy.json")
RELINK_TEMPLATE = os.path.join(TOOLS, "notices", "LGPL-relinking.txt")

# rank: a smaller number is less restrictive; AND takes the max, OR the min
RANK = {"permissive": 0, "weak-copyleft": 1, "runtime-exception": 2, "proprietary-redistributable": 3,
        "strong-copyleft": 4, "unknown": 5}
BASE_ALLOWED = ("permissive", "weak-copyleft", "runtime-exception", "proprietary-redistributable")

# SPDX-style id prefixes read as permissive. Deliberately short: anything else (OpenSSL's advertising
# clause, OFL font terms, "custom", LicenseRef-...) is "unknown" and needs a reviewed policy entry.
_PERMISSIVE = ("MIT", "BSD", "APACHE", "ISC", "ZLIB", "PSF", "PYTHON", "BSL", "BOOST", "HPND", "UNLICENSE",
               "CC0", "LIBPNG", "CURL", "X11", "NCSA", "ZPL", "FTL", "IJG", "LIBTIFF", "BLESSING",
               "PUBLIC-DOMAIN", "UNICODE", "0BSD", "BZIP2")
_WEAK = ("LGPL", "MPL", "EPL", "CDDL")
_STRONG = ("AGPL", "GPL", "SSPL")
_EXCEPTION_WORDS = ("EXCEPTION", "CLASSPATH", "LINKING")
# NATIVE-DESKTOP-PLAN.md section 7.3: the Qt modules that are GPL-or-commercial only, by staged DLL
# name (case-insensitive). Only the ones that table names -- add another only after checking qt.io.
FORBIDDEN_DLLS = (r"^qt6charts", r"^qt6graphs", r"^qt6datavisualization")


def _norm(term):
    t = re.sub(r"[\s_/]+", "-", term.strip().upper())
    t = re.sub(r"-+", "-", t).strip("-()")
    return t


def _base_class(token):
    t = _norm(token)
    if not t:
        return "unknown"
    # AGPL/LGPL before GPL: "LGPL" must never be read as GPL
    for pre in _STRONG[:1]:                       # AGPL
        if t.startswith(pre):
            return "strong-copyleft"
    if t.startswith("LGPL"):
        return "weak-copyleft"
    for pre in _WEAK[1:]:
        if t.startswith(pre):
            return "weak-copyleft"
    for pre in _STRONG[1:]:
        if t.startswith(pre):
            return "strong-copyleft"
    for pre in _PERMISSIVE:
        if t.startswith(pre):
            return "permissive"
    return "unknown"


def classify(expr):
    """(class, explanation) of a licence expression. OR: the least restrictive alternative (the
    licensee may pick it); AND: the most restrictive term; WITH <exception> on a copyleft term:
    runtime-exception."""
    if not expr or not expr.strip():
        return "unknown", "no licence string"
    text = expr.strip().strip("()")
    alts = re.split(r"\s+OR\s+|\s+\|\s+", text, flags=re.I)
    best = None
    for alt in alts:
        # NOT split on commas: "Boost Software License, Version 1.0" is one licence name
        terms = re.split(r"\s+AND\s+|\s+&\s+", alt.strip().strip("()"), flags=re.I)
        worst = "permissive"
        for term in terms:
            term = term.strip().strip("()")
            if not term:
                continue
            m = re.split(r"\s+WITH\s+", term, maxsplit=1, flags=re.I)
            cls = _base_class(m[0])
            if len(m) == 2 and cls in ("strong-copyleft", "weak-copyleft") \
                    and any(w in m[1].upper() for w in _EXCEPTION_WORDS):
                cls = "runtime-exception" if cls == "strong-copyleft" else cls
            if RANK[cls] > RANK[worst]:
                worst = cls
        if best is None or RANK[worst] < RANK[best]:
            best = worst
    return best or "unknown", f"from '{expr.strip()}'"


# -- policy ----------------------------------------------------------------------------

def load_policy(path):
    with open(path, "r", encoding="utf-8") as fh:
        pol = json.load(fh)
    errs = []
    for name, e in pol.get("reviewed", {}).items():
        if e.get("class") not in RANK or e.get("class") == "unknown":
            errs.append(f"policy reviewed[{name}]: 'class' must be one of {sorted(set(RANK) - {'unknown'})}")
        for k in ("reason", "basis"):
            if not str(e.get(k, "")).strip():
                errs.append(f"policy reviewed[{name}]: '{k}' is required (a reviewed entry is a human decision)")
    for name, e in pol.get("addon_copyleft", {}).items():
        for k in ("reason", "basis"):
            if not str(e.get(k, "")).strip():
                errs.append(f"policy addon_copyleft[{name}]: '{k}' is required")
    for name, e in pol.get("unowned_dlls", {}).items():
        for k in ("package", "license", "reason", "text_file"):
            if not str(e.get(k, "")).strip():
                errs.append(f"policy unowned_dlls[{name}]: '{k}' is required")
    for e in pol.get("embedded", []):
        if not e.get("package") or not str(e.get("reason", "")).strip():
            errs.append(f"policy embedded entry needs 'package' and 'reason': {e}")
    if errs:
        raise ValueError("; ".join(errs))
    return pol


# -- reading conda metadata --------------------------------------------------------------

def load_conda_meta(prefix):
    metas, problems = [], []
    for f in sorted(glob.glob(os.path.join(prefix, "conda-meta", "*.json"))):
        try:
            with open(f, "r", encoding="utf-8") as fh:
                m = json.load(fh)
        except (OSError, ValueError) as exc:
            problems.append(f"cannot read {f}: {exc}")
            continue
        if isinstance(m, dict) and m.get("name"):
            metas.append(m)
    return metas, problems


def package_dir(meta):
    """The extracted package directory (holds info/licenses), or None."""
    d = meta.get("extracted_package_dir")
    if d and os.path.isdir(d):
        return d
    tb = meta.get("package_tarball_full_path")
    if tb:
        stem = re.sub(r"\.(conda|tar\.bz2)$", "", os.path.basename(tb))
        cand = os.path.join(os.path.dirname(tb), stem)
        if os.path.isdir(cand):
            return cand
    return None


def license_string(meta, pkgdir):
    lic = (meta.get("license") or "").strip()
    if not lic and pkgdir:
        try:
            with open(os.path.join(pkgdir, "info", "about.json"), "r", encoding="utf-8") as fh:
                lic = (json.load(fh).get("license") or "").strip()
        except (OSError, ValueError):
            pass
    return lic


def license_text_files(pkgdir):
    out = []
    if not pkgdir:
        return out
    root = os.path.join(pkgdir, "info", "licenses")
    for base, _dirs, files in os.walk(root):
        for f in sorted(files):
            out.append(os.path.join(base, f))
    return sorted(out)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _key(rel):
    return rel.replace("\\", "/").lower()


# -- building the inventory --------------------------------------------------------------

def component(meta, source, policy, addon):
    """One inventory row and its problems for a conda package."""
    name, version = meta["name"], meta.get("version", "?")
    pkgdir = package_dir(meta)
    expr = license_string(meta, pkgdir)
    cls, why = classify(expr)
    problems = []
    who = f"{name} {version}"
    reviewed = policy.get("reviewed", {}).get(name)
    if reviewed:
        cls, why = reviewed["class"], f"reviewed: {reviewed['reason']} ({reviewed['basis']})"
    if not expr and not reviewed:
        problems.append(f"{who}: no licence string in conda-meta or info/about.json")
    elif cls == "unknown":
        problems.append(f"{who}: licence '{expr}' cannot be classified -- add a reviewed entry with a reason to "
                        f"license_policy.json if it is acceptable")
    elif cls == "strong-copyleft":
        allowed = policy.get("addon_copyleft", {}).get(name)
        if not (addon and allowed):
            problems.append(f"{who}: strong copyleft licence '{expr}' in the base runtime -- not allowed "
                            f"(decision 26.4-4 only permits it in the separate add-on, via --addon and an "
                            f"addon_copyleft policy entry)")
        else:
            why = f"add-on copyleft: {allowed['reason']} ({allowed['basis']})"
    texts = license_text_files(pkgdir)
    if not texts:
        problems.append(f"{who}: no licence text on disk ({'package dir ' + pkgdir + ' has no info/licenses' if pkgdir else 'its extracted package directory no longer exists -- restore it, e.g. reinstall the package'})")
    row = {"name": name, "version": version, "build": meta.get("build", ""), "channel": str(meta.get("channel", "")),
           "url": meta.get("url", ""), "license": expr, "class": cls, "why": why, "source": source,
           "_texts": texts, "texts": []}
    return row, problems


def owned_files(metas):
    s = set()
    for m in metas:
        for f in m.get("files", []) or []:
            s.add(_key(f))
    return s


def staged_dlls(stage):
    out = []
    for base, dirs, files in os.walk(stage):
        rel_base = os.path.relpath(base, stage)
        top = rel_base.split(os.sep)[0]
        if top in ("runtime", "backend", "licenses"):
            dirs[:] = []
            continue
        for f in files:
            if f.lower().endswith(".dll"):
                out.append(os.path.normpath(os.path.join(rel_base, f)) if rel_base != "." else f)
    return sorted(out)


def build_inventory(stage, gui_env, policy, addon):
    rows, problems, dll_owner = [], [], {}
    seen = set()

    def add(meta, source):
        key = (meta["name"], meta.get("version"))
        if key in seen:
            return
        seen.add(key)
        row, probs = component(meta, source, policy, addon)
        rows.append(row)
        problems.extend(probs)

    # 1. the runtime
    rt = os.path.join(stage, "runtime")
    rt_owned = set()
    if os.path.isdir(rt):
        metas, errs = load_conda_meta(rt)
        problems.extend(errs)
        if not metas:
            problems.append(f"{rt} has no conda-meta: cannot inventory the runtime (was it packed with conda-pack?)")
        for m in metas:
            add(m, "runtime")
        rt_owned = owned_files(metas)
        for base, _d, files in os.walk(rt):
            for f in files:
                low = f.lower()
                rel = _key(os.path.relpath(os.path.join(base, f), rt))
                if low.endswith((".dll", ".pyd")) and rel not in rt_owned:
                    problems.append(f"runtime binary with no owning conda package: runtime/{rel}")
                if low == "installer" and base.endswith(".dist-info"):
                    try:
                        with open(os.path.join(base, f), "r", encoding="utf-8") as fh:
                            if fh.read().strip().lower() == "pip":
                                problems.append(f"pip-installed distribution not covered by conda licence metadata: "
                                                f"runtime/{os.path.basename(base)}")
                    except OSError:
                        pass

    # 2. the app's DLLs, traced to the tcad-gui packages that own them
    dlls = staged_dlls(stage)
    gui_metas, errs = ([], [])
    if dlls or policy.get("embedded"):
        if not gui_env:
            problems.append("--gui-env is required to trace the staged DLLs to their packages")
        else:
            gui_metas, errs = load_conda_meta(gui_env)
            problems.extend(errs)
    index = {}
    for m in gui_metas:
        for f in m.get("files", []) or []:
            k = _key(f)
            if k.endswith(".dll"):
                index.setdefault(os.path.basename(k), []).append(m)
    for rel in dlls:
        name = os.path.basename(rel)
        if any(re.search(p, name.lower()) for p in FORBIDDEN_DLLS):
            problems.append(f"{rel}: DLL of a GPL/commercial-only Qt module (section 7.3) -- must not be shipped")
        owners = index.get(name.lower())
        if owners:
            for m in owners:
                add(m, "app")
            dll_owner[rel] = sorted({m["name"] for m in owners})
            continue
        u = policy.get("unowned_dlls", {}).get(name.lower()) or policy.get("unowned_dlls", {}).get(name)
        if u:
            dll_owner[rel] = [u["package"]]
            row = {"name": u["package"], "version": u.get("version", "?"), "build": "", "channel": "policy",
                   "url": u.get("source", ""), "license": u["license"], "class": classify(u["license"])[0],
                   "why": f"policy unowned_dlls: {u['reason']}", "source": "app", "_texts": [], "texts": []}
            if (u["package"], u.get("version", "?")) not in seen:
                seen.add((u["package"], u.get("version", "?")))
                tf = u["text_file"]
                if os.path.isfile(tf):
                    row["_texts"] = [tf]
                else:
                    problems.append(f"{rel}: policy text_file {tf} does not exist")
                rows.append(row)
        else:
            problems.append(f"{rel}: no licence entry -- no tcad-gui package owns this DLL and license_policy.json "
                            f"does not list it under unowned_dlls")
    for e in policy.get("embedded", []):
        hit = [m for m in gui_metas if m["name"] == e["package"]]
        if not hit:
            problems.append(f"embedded package {e['package']} is not installed in {gui_env}")
        for m in hit:
            add(m, "app")
            for r in rows:
                if r["name"] == m["name"]:
                    r["why"] += f"; embedded in tcad_desktop.exe: {e['reason']}"

    rows.sort(key=lambda r: (r["source"], r["name"].lower(), str(r["version"])))
    return rows, problems, dll_owner


# -- writing the bundle ------------------------------------------------------------------

def relink_statement(lgpl_rows):
    with open(RELINK_TEMPLATE, "r", encoding="utf-8") as fh:
        text = fh.read()
    names = "\n".join(f"  - {r['name']} {r['version']} ({r['license']})" for r in lgpl_rows)
    return text.replace("@LGPL_COMPONENTS@", names)


def write_bundle(stage, out, rows, dll_owner, policy_path, addon):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    tdir = os.path.join(out, "texts")
    for r in rows:
        sub = os.path.join(tdir, f"{r['name']}-{r['version']}")
        for src in r.pop("_texts"):
            rel = os.path.basename(src)
            dst = os.path.join(sub, rel)
            n = 1
            while os.path.exists(dst):                      # two files with one name in one package
                n += 1
                dst = os.path.join(sub, f"{n}-{rel}")
            os.makedirs(sub, exist_ok=True)
            shutil.copyfile(src, dst)
            r["texts"].append({"file": os.path.relpath(dst, out).replace("\\", "/"), "sha256": sha256_file(dst)})
    counts = {}
    for r in rows:
        counts[r["class"]] = counts.get(r["class"], 0) + 1
    manifest = {"generator": "desktop/tools/gen_licenses.py", "addon": bool(addon),
                "policy_sha256": sha256_file(policy_path), "counts": counts, "components": rows,
                "dlls": {k.replace("\\", "/"): v for k, v in sorted(dll_owner.items())}}
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(manifest, fh, indent=2, sort_keys=True)
        fh.write("\n")

    lines = ["PyTCAD Desktop -- third-party notices", "=" * 60, "",
             "Generated by desktop/tools/gen_licenses.py from the files actually staged in this installation.",
             "This is an engineering inventory of the components and their licences, not legal advice.", ""]
    lines.append("Summary: " + ", ".join(f"{n} {c}" for c, n in sorted(counts.items())) + f" ({len(rows)} components)")
    lines.append("")
    lgpl = [r for r in rows if "LGPL" in r["license"].upper() and r["class"] in ("weak-copyleft", "runtime-exception")]
    if lgpl:
        lines += ["-" * 60, relink_statement(lgpl).rstrip(), ""]
    special = [r for r in rows if r["class"] in ("proprietary-redistributable", "runtime-exception", "strong-copyleft")]
    if special:
        lines += ["-" * 60, "Components under terms that need particular attention:"]
        for r in special:
            lines.append(f"  - {r['name']} {r['version']}: {r['license'] or '(policy)'} [{r['class']}] -- {r['why']}")
        lines.append("")
    lines += ["-" * 60, "Components", ""]
    for r in rows:
        lines.append(f"{r['name']} {r['version']}  [{r['source']}]")
        lines.append(f"    licence : {r['license'] or '(see policy)'}   class: {r['class']}")
        if r["url"]:
            lines.append(f"    source  : {r['url']}")
        if r["why"] != f"from '{r['license'].strip()}'":      # anything beyond the mechanical default: a
            # reviewed / embedded / add-on / runtime-exception decision
            lines.append(f"    note    : {r['why']}")
        provided = sorted(k for k, v in dll_owner.items() if r["name"] in v)
        if provided:
            lines.append("    provides: " + ", ".join(p.replace("\\", "/") for p in provided))
        for t in r["texts"]:
            lines.append(f"    text    : licenses/{t['file']}")
        lines.append("")
    with open(os.path.join(out, "THIRD_PARTY_NOTICES.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    return manifest


# -- verifying an installed bundle (stdlib only; needs no conda) --------------------------

def verify_bundle(app):
    """Problems with the licence bundle of an installed/staged app dir (empty = fine)."""
    lic = os.path.join(app, "licenses")
    problems = []
    for f in ("THIRD_PARTY_NOTICES.txt", "manifest.json"):
        if not os.path.isfile(os.path.join(lic, f)):
            problems.append(f"licenses/{f} is missing")
    if problems:
        return problems
    with open(os.path.join(lic, "manifest.json"), "r", encoding="utf-8") as fh:
        man = json.load(fh)
    listed = {(c["name"], str(c["version"])) for c in man["components"]}
    for c in man["components"]:
        if not c.get("texts"):
            problems.append(f"{c['name']}: no licence text recorded")
        for t in c.get("texts", []):
            p = os.path.join(lic, t["file"])
            if not os.path.isfile(p):
                problems.append(f"{t['file']} is missing")
            elif sha256_file(p) != t["sha256"]:
                problems.append(f"{t['file']} was modified (sha256 mismatch)")
        if c["class"] == "strong-copyleft" and not man.get("addon"):
            problems.append(f"{c['name']}: strong copyleft in a base bundle")
    rt = os.path.join(app, "runtime")
    if os.path.isdir(rt):
        metas, _errs = load_conda_meta(rt)
        for m in metas:
            if (m["name"], str(m.get("version", "?"))) not in listed:
                problems.append(f"runtime package {m['name']} {m.get('version')} is not in the bundle (stale bundle?)")
    for rel in staged_dlls(app):
        if rel.replace("\\", "/") not in man.get("dlls", {}):
            problems.append(f"{rel}: DLL not covered by the bundle")
    for pat in FORBIDDEN_DLLS:
        for rel in man.get("dlls", {}):
            if re.search(pat, os.path.basename(rel).lower()):
                problems.append(f"{rel}: forbidden GPL/commercial-only module in the bundle")
    return problems


# -- CLI ---------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stage", help="the stage.ps1 output directory")
    ap.add_argument("--gui-env", help="the tcad-gui conda prefix the DLLs were copied from")
    ap.add_argument("--policy", default=DEFAULT_POLICY)
    ap.add_argument("--out", help="default: <stage>/licenses")
    ap.add_argument("--addon", action="store_true", help="the add-on runtime: policy addon_copyleft packages are allowed")
    ap.add_argument("--check-only", action="store_true", help="report the inventory and problems; write nothing")
    ap.add_argument("--verify-bundle", metavar="APP_DIR", help="verify an installed/staged app's licence bundle and exit")
    args = ap.parse_args(argv)

    if args.verify_bundle:
        probs = verify_bundle(os.path.abspath(args.verify_bundle))
        for p in probs:
            print("FAIL  " + p)
        print("licence bundle OK" if not probs else f"{len(probs)} problem(s)")
        return 1 if probs else 0
    if not args.stage:
        ap.error("--stage is required")
    stage = os.path.abspath(args.stage)
    policy_path = os.path.abspath(args.policy)
    try:
        policy = load_policy(policy_path)
    except (OSError, ValueError) as exc:
        print(f"FAIL  policy {policy_path}: {exc}")
        return 1
    rows, problems, dll_owner = build_inventory(stage, os.path.abspath(args.gui_env) if args.gui_env else None,
                                                policy, args.addon)
    print(f"{'source':8} {'class':28} {'component':34} licence")
    for r in rows:
        print(f"{r['source']:8} {r['class']:28} {(r['name'] + ' ' + str(r['version']))[:34]:34} {r['license']}")
    if problems:
        print()
        for p in problems:
            print("FAIL  " + p)
        # a failed gate must not leave a plausible-looking bundle from an earlier run behind
        stale = os.path.abspath(args.out) if args.out else os.path.join(stage, "licenses")
        if os.path.isdir(stale) and not args.check_only:
            shutil.rmtree(stale)
        print(f"\n{len(problems)} licence problem(s) in {len(rows)} components: no bundle written")
        return 1
    if args.check_only:
        print(f"\n{len(rows)} components, no problems (--check-only: nothing written)")
        return 0
    out = os.path.abspath(args.out) if args.out else os.path.join(stage, "licenses")
    man = write_bundle(stage, out, rows, dll_owner, policy_path, args.addon)
    print(f"\nwrote {out}: {len(rows)} components ({', '.join(f'{n} {c}' for c, n in sorted(man['counts'].items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

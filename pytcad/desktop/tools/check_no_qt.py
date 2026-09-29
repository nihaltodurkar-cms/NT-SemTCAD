"""P6 hard gate (NATIVE-DESKTOP-PLAN.md 27): a staged/installed TCAD tree contains NO Qt.

    python check_no_qt.py <dist\\TCAD> [--json report.json]

Exit 0 only if nothing Qt is found; otherwise every hit is listed and the exit code is 1. Stdlib only (the staged
runtime python can run it). Four independent checks, so removing one kind of evidence cannot hide the others:
  1. FILE NAMES anywhere in the tree: Qt5*/Qt6*.dll, qtadvanceddocking*, qt.conf, PySide/PyQt/shiboken files,
     Qt plugin DLLs (q*.dll inside the plugin directories windeployqt creates), *.qm translations.
  2. PLUGIN DIRECTORIES: platforms, styles, imageformats, iconengines, generic, tls, networkinformation, sqldrivers,
     platforminputcontexts, qmltooling, multimedia, position, sensors, printsupport, translations -- flagged when
     they hold any file (an empty directory is not a Qt file).
  3. BINARY CONTENT: every .dll/.exe/.pyd/.so is scanned for an import/reference to a Qt DLL name (Qt5Core.dll, Qt6Widgets.dll,
     ... , qtadvanceddocking-qt6.dll, vtkGUISupportQt, vtkRenderingQt): a renamed or relocated Qt DLL, or any VTK
     module that links Qt, still shows up here.
  4. CONDA RECORDS: runtime\\conda-meta/*.json packages named qt*, qt6-*, pyside*, pyqt*, shiboken*, qtpy, or whose
     depends[] names one; and licences/manifest.json components of that kind.
"""
import argparse
import json
import os
import re
import sys

PLUGIN_DIRS = {"platforms", "styles", "imageformats", "iconengines", "generic", "tls", "networkinformation",
               "sqldrivers", "platforminputcontexts", "platformthemes", "qmltooling", "multimedia", "position",
               "sensors", "printsupport", "translations", "bearer", "canbus", "designer", "webview"}
NAME_RES = [re.compile(p, re.I) for p in (
    r"^qt[56]\w*\.dll$", r"^qtadvanceddocking\S*\.dll$", r"^qt\.conf$", r"^qt[56]?\w*\.pyd$", r"^(pyside|pyqt|shiboken)\S*",
    r"^qt_\w+\.qm$", r"^qwindows\w*\.dll$", r"^q(svg|gif|ico|jpeg|tga|tiff|wbmp|webp|icns|pdf)\w*\.dll$",
    r"^qt(uiotouchplugin|diag)\S*", r"^windeployqt\S*")]
BINARY_EXT = (".dll", ".exe", ".pyd", ".so")
CONTENT_RE = re.compile(rb"(?i)(qt[56](core|gui|widgets|opengl|openglwidgets|network|svg|svgwidgets|xml|printsupport|test|concurrent|sql|dbus)\w*\.dll"
                        rb"|qtadvanceddocking[\w\-]*\.dll|vtkguisupportqt[\w\-.]*|vtkrenderingqt[\w\-.]*|vtkviewsqt[\w\-.]*)")
QT_PKG_RE = re.compile(r"^(qt\d?|qt[56]?-.*|qt6-.*|qt5-.*|pyside\d*.*|pyqt\d*.*|shiboken\d*.*|qtpy|qt-main|qtconsole)$", re.I)


def scan(root):
    hits = []
    root = os.path.abspath(root)
    for base, dirs, files in os.walk(root):
        rel_base = os.path.relpath(base, root).replace("\\", "/")
        leaf = os.path.basename(base).lower()
        if leaf in PLUGIN_DIRS and files:
            hits.append({"check": "plugin-dir", "path": rel_base, "why": f"directory '{leaf}' holds {len(files)} file(s)"})
        for f in files:
            rel = (f if rel_base == "." else rel_base + "/" + f)
            if any(r.match(f) for r in NAME_RES):
                hits.append({"check": "file-name", "path": rel, "why": "Qt-named file"})
            if f.lower().endswith(BINARY_EXT):
                try:
                    with open(os.path.join(base, f), "rb") as fh:
                        data = fh.read()
                except OSError as exc:
                    hits.append({"check": "unreadable", "path": rel, "why": f"cannot be scanned: {exc}"})
                    continue
                m = CONTENT_RE.search(data)
                if m:
                    hits.append({"check": "binary-content", "path": rel, "why": f"references {m.group().decode('latin1')}"})
    meta = os.path.join(root, "runtime", "conda-meta")
    if os.path.isdir(meta):
        for f in sorted(os.listdir(meta)):
            if not f.endswith(".json"):
                continue
            try:
                m = json.load(open(os.path.join(meta, f), encoding="utf-8"))
            except (OSError, ValueError):
                hits.append({"check": "unreadable", "path": f"runtime/conda-meta/{f}", "why": "unparsable conda-meta record"})
                continue
            name = str(m.get("name", ""))
            if QT_PKG_RE.match(name):
                hits.append({"check": "conda-package", "path": f"runtime/conda-meta/{f}", "why": f"package '{name}'"})
            for d in m.get("depends", []) or []:
                if QT_PKG_RE.match(str(d).split()[0]):
                    hits.append({"check": "conda-depends", "path": f"runtime/conda-meta/{f}", "why": f"{name} depends on '{d}'"})
    man = os.path.join(root, "licenses", "manifest.json")
    if os.path.isfile(man):
        try:
            for c in json.load(open(man, encoding="utf-8")).get("components", []):
                if QT_PKG_RE.match(str(c.get("name", ""))):
                    hits.append({"check": "licence-manifest", "path": "licenses/manifest.json", "why": f"component '{c['name']}'"})
        except (OSError, ValueError):
            hits.append({"check": "unreadable", "path": "licenses/manifest.json", "why": "unparsable"})
    return hits


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    if not os.path.isdir(a.root):
        print(f"FAIL  {a.root} is not a directory (a missing stage must not pass)")
        return 2
    n_files = sum(len(f) for _b, _d, f in os.walk(a.root))
    if n_files == 0:
        print(f"FAIL  {a.root} is empty (an empty tree must not pass)")
        return 2
    hits = scan(a.root)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"root": os.path.abspath(a.root), "files_scanned": n_files, "hits": hits}, fh, indent=2)
    for h in hits:
        print(f"FAIL  [{h['check']}] {h['path']}: {h['why']}")
    print(f"{n_files} files scanned; " + ("NO Qt found" if not hits else f"{len(hits)} Qt hit(s)"))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())

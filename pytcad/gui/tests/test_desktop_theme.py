"""NATIVE-DESKTOP-PLAN.md P1 S3c / section 26 theme gates for the native
app, for ONE light engineering scheme (2026-09-27, superseding the
2026-09-26 "no modes, just black and white" decision).

1. The palette matches tokens.hpp exactly: a regression pin on the
   handful of tokens most likely to drift silently (background, base,
   text, the accent, and the status-colour set), not an inventory of
   every token. Data colours (plot series, region palette, colour maps,
   and the Structure Editor's doping/contact/gate colours) are not theme
   tokens and keep their own colours -- see tokens.hpp's own
   StructureColour for that scheme's home.
2. No hard-coded colours in the native sources outside src/theme/ and the
   colour-map module (colour maps encode values, not UI). This was the
   QML GUI's Phase 3/4 review finding; here it is a gate. The patterns
   are checked against known-bad snippets first, so a pattern that
   matches nothing cannot pass vacuously.

What the running window paints (palette, VTK background, ADS panels, the
plot) is in the shell e2e test (desktop/tests/test_shell.cpp).

PySide6/QML removed from this repo: the "drift" gate (every native token
that mirrors a gui/qml/Theme.qml colour equals it exactly) was removed
with it -- Theme.qml no longer exists, and the native theme it was once
derived from is now the sole implementation.
"""
import json
import os
import re
import subprocess

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build", "desktop")
MANIFEST = os.path.join(BUILD, "desktop_runtime.json")
SRC = os.path.join(ROOT, "desktop", "src")


def _tokens():
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    out = subprocess.run([os.path.join(BUILD, manifest["tools"]["theme_dump"])],
                         capture_output=True, text=True, timeout=60)
    assert out.returncode == 0, out.stderr
    return json.loads(out.stdout)


needs_build = pytest.mark.skipif(not os.path.isfile(MANIFEST),
                                 reason="native desktop app not built (powershell -File desktop\build.ps1)")


@needs_build
def test_native_tokens_match_the_light_palette():
    tokens = {t["name"]: t for t in _tokens()}
    assert {t for t in tokens if tokens[t]["status"]} == {"warning", "error", "ok"}
    assert tokens["background"]["hex"] == "#f0f2f4"
    assert tokens["base"]["hex"] == "#ffffff"
    assert tokens["text"]["hex"] == "#1a2027"
    assert tokens["accent"]["hex"] == "#2b6cb0"
    assert tokens["focus"]["hex"] == tokens["accent"]["hex"]


# -- no hard-coded colours --------------------------------------------------------

EXEMPT = {os.path.join("views", "colormaps.cpp"), os.path.join("views", "colormaps.hpp"),
          os.path.join("views", "colormap_tables.hpp")}  # generated from matplotlib (S5b)
PATTERNS = {
    # anywhere in a line (a stylesheet string embeds it: "color: #ff0000");
    # the \b ends exclude preprocessor lines (#define: "#def" + "i")
    "hex colour literal": re.compile(r"(?<![\w&])#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b"),
    "QColor from literals": re.compile(r"QColor\s*[({]\s*[0-9\"#]"),
    "setRgb": re.compile(r"\bsetRgbF?\s*\("),
    "named Qt colour": re.compile(r"\bQt::(white|black|red|green|blue|cyan|magenta|yellow|gray|darkGray|lightGray)\b"),
    "VTK colour from literals": re.compile(r"\b(SetBackground|SetColor|SetTableValue|AddRGBPoint)\s*\(\s*[-+.\d]"),
}
KNOWN_BAD = [
    'p.setColor(QPalette::Window, QColor(30, 32, 36));',   # main.cpp before S3c
    'p.setColor(QPalette::HighlightedText, Qt::white);',
    'renderer_->SetBackground(0.11, 0.12, 0.14);',            # field_view.cpp before S3c
    'label->setStyleSheet("color: #ff0000");',
    'c.setRgb(1, 2, 3);',
]


KNOWN_GOOD = [
    "#include <QColor>",
    "#define TCAD_HAVE_ZLIB 1",
    "#endif",
    "renderer_->SetBackground(bg.r, bg.g, bg.b);",
    "tp->SetColor(fg.r, fg.g, fg.b);",
]


def test_colour_patterns_catch_known_bad_code():
    for snippet in KNOWN_BAD:
        assert any(p.search(snippet) for p in PATTERNS.values()), f"no pattern flags: {snippet}"
    for snippet in KNOWN_GOOD:
        hits = [w for w, p in PATTERNS.items() if p.search(snippet)]
        assert not hits, f"{snippet!r} wrongly flagged as {hits}"


def test_no_hard_coded_colours_outside_the_theme():
    offenders = []
    for dirpath, _, files in os.walk(SRC):
        for f in files:
            if not f.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, SRC)
            if rel.startswith("theme" + os.sep) or rel in EXEMPT:
                continue
            with open(path, encoding="utf-8") as fh:
                for n, line in enumerate(fh, 1):
                    code = line.split("//", 1)[0]
                    for what, p in PATTERNS.items():
                        if p.search(code):
                            offenders.append(f"{rel}:{n}: {what}: {line.strip()}")
    assert not offenders, "\n".join(offenders)

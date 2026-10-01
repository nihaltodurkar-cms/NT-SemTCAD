// The drawing interface widgets paint through (N2b). Portable: implemented by Direct2D (ui/win32/d2d_painter) and
// by the RecordingPainter (display lists for tests that need no pixels). Coordinates are DIPs, relative to the
// widget being painted (the tree translates before each widget and clips to it). The text engine measures, wraps and
// hit-tests text (N2d).
//
// N2d SCOPE, measured in desktop/src on 2026-09-30: word-wrapped labels (8 setWordWrap), advance measurement for plot
// axes (10 horizontalAdvance, 5 QFontMetricsF), aligned drawText (17), fonts and sizes (11 setFont, 2 setPointSize),
// one monospace console (QFontDatabase::FixedFont), one selectable label. NOT used, so not built: eliding (0), rich
// text (0), QTextLayout/QTextDocument (0).
//
// Positions inside text are UTF-8 BYTE offsets that always fall on a cluster boundary -- the unit a caret moves by
// (a base letter with its combining marks, a Devanagari conjunct, a surrogate pair, an emoji ZWJ sequence).
#pragma once

#include "ui/core/geometry.hpp"

#include <string_view>
#include <vector>

namespace tcad::ui {

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };
enum class FontFamily { Ui, Monospace };  // Segoe UI; Consolas (the console's FixedFont)

struct TextStyle {
    float size = 12.0f;  // DIPs (the em size)
    bool bold = false;
    bool italic = false;  // N3d: the console's notes
    Color color{};
    HAlign halign = HAlign::Left;
    VAlign valign = VAlign::Center;
    FontFamily family = FontFamily::Ui;
    bool wrap = false;  // break lines at word boundaries to fit the rectangle's width
    bool operator==(const TextStyle&) const = default;
};

struct TextHit {
    std::size_t offset = 0;  // UTF-8 byte offset of the nearest cluster boundary (the caret position)
    bool inside = false;     // the point is over a glyph (not in the margin after a line)
};

class TextEngine {
public:
    virtual ~TextEngine() = default;
    // The size of one line of `utf8` (no wrapping) in DIPs: advance width x line height.
    virtual SizeF measure(std::string_view utf8, const TextStyle& style) = 0;
    // Wrapped to `max_width` DIPs: the widest line x the total height. The defaults are for engines (test fakes) that
    // only measure one line.
    virtual SizeF measureWrapped(std::string_view utf8, const TextStyle& style, float max_width) {
        (void)max_width;
        return measure(utf8, style);
    }
    virtual int lineCount(std::string_view utf8, const TextStyle& style, float max_width) {
        (void)utf8, (void)style, (void)max_width;
        return 1;
    }
    // The caret position nearest to point `p` (DIPs from the top of the text's first line, laid out at `max_width` with
    // the style's wrapping and horizontal alignment; the style's vertical alignment is NOT applied -- that offset is the
    // caller's), and the caret rectangle for a UTF-8 offset. At a boundary between runs of opposite direction one
    // visual position is two logical offsets; either may come back.
    virtual TextHit hitTest(std::string_view utf8, const TextStyle& style, float max_width, PointF p) {
        (void)utf8, (void)style, (void)max_width, (void)p;
        return {};
    }
    virtual RectF caretRect(std::string_view utf8, const TextStyle& style, float max_width, std::size_t offset) {
        (void)utf8, (void)style, (void)max_width, (void)offset;
        return {};
    }
    // The rectangles (DIPs from the top of the text's first line; one per visual line and per run of one direction) that
    // a selection of [start, end) covers (N3d). Right-to-left runs inside left-to-right text give pieces that are not
    // between the two caret positions, which is why this is the engine's to say. The default is one rectangle between
    // the two caret positions, right for one line of left-to-right text.
    virtual std::vector<RectF> selectionRects(std::string_view utf8, const TextStyle& style, float max_width, std::size_t start,
                                              std::size_t end) {
        if (start > end) std::swap(start, end);
        const RectF a = caretRect(utf8, style, max_width, start), b = caretRect(utf8, style, max_width, end);
        if (start == end) return {};
        return {{std::min(a.x, b.x), a.y, std::fabs(b.x - a.x), a.height}};
    }
    // The UTF-8 offsets where a caret may stand: 0, every cluster start, and utf8.size(). The default: every code
    // point (right for simple text; a real engine knows conjuncts and emoji sequences).
    virtual std::vector<std::size_t> caretStops(std::string_view utf8, const TextStyle& style);
};

class Painter {
public:
    virtual ~Painter() = default;
    // save/restore: the translation and clip state (a stack)
    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void translate(float dx, float dy) = 0;
    virtual void clipRect(const RectF& r) = 0;  // intersects the current clip until the matching restore()

    virtual void fillRect(const RectF& r, Color c) = 0;
    virtual void strokeRect(const RectF& r, Color c, float width) = 0;
    virtual void fillRoundedRect(const RectF& r, float radius, Color c) = 0;
    virtual void strokeRoundedRect(const RectF& r, float radius, Color c, float width) = 0;
    virtual void drawLine(PointF a, PointF b, Color c, float width) = 0;
    virtual void fillEllipse(const RectF& bounds, Color c) = 0;
    virtual void fillPolygon(const std::vector<PointF>& pts, Color c) = 0;
    // Text in `r`, aligned per `style`, wrapped to r's width when style.wrap (clipped to r).
    virtual void drawText(const RectF& r, std::string_view utf8, const TextStyle& style) = 0;

    virtual double scale() const = 0;  // device px per DIP

    // An outline of about `width` DIPs just inside `r` that lands on whole device pixels: the stroke becomes a whole
    // number of pixels (at least 1) and the rectangle is snapped and inset by half of it. Assumes the current
    // translation is on whole pixels, which the widget tree guarantees (widget origins are integer pixels).
    struct Crisp {
        RectF rect;
        float width;
    };
    Crisp crisp(const RectF& r, float width) const;
};

}  // namespace tcad::ui

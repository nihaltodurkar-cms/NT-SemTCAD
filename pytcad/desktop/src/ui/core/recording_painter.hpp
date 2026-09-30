// A Painter that records what was drawn instead of drawing it (N2b): the display list portable tests assert on,
// with no GPU and no pixels. Every operation is stored with ABSOLUTE DIP coordinates (the translation applied)
// and the clip in force, and has a stable one-line text form for readable test failures.
#pragma once

#include "ui/core/painter.hpp"

#include <string>
#include <vector>

namespace tcad::ui {

struct PaintOp {
    std::string kind;       // "fillRect", "strokeRect", "fillRoundedRect", "strokeRoundedRect", "line", "ellipse",
                            // "polygon", "text"
    RectF rect;             // absolute DIPs (for "line": from (x, y) to (right, bottom))
    Color color;
    float width = 0;        // stroke width, or the radius for the rounded kinds
    std::string text;       // "text" only
    TextStyle style;        // "text" only
    RectF clip;             // absolute DIPs; the whole canvas when unclipped
    std::string str() const;
};

class RecordingPainter final : public Painter {
public:
    explicit RecordingPainter(double scale = 1.0, SizeF canvas = {kMaxDip, kMaxDip});

    void save() override;
    void restore() override;
    void translate(float dx, float dy) override;
    void clipRect(const RectF& r) override;
    void fillRect(const RectF& r, Color c) override;
    void strokeRect(const RectF& r, Color c, float width) override;
    void fillRoundedRect(const RectF& r, float radius, Color c) override;
    void strokeRoundedRect(const RectF& r, float radius, Color c, float width) override;
    void drawLine(PointF a, PointF b, Color c, float width) override;
    void fillEllipse(const RectF& bounds, Color c) override;
    void fillPolygon(const std::vector<PointF>& pts, Color c) override;
    void drawText(const RectF& r, std::string_view utf8, const TextStyle& style) override;
    double scale() const override { return scale_; }

    const std::vector<PaintOp>& ops() const { return ops_; }
    std::string dump() const;  // one op per line
    int depth() const { return static_cast<int>(stack_.size()); }

private:
    struct State {
        float dx = 0, dy = 0;
        RectF clip;
    };
    void add(PaintOp op, const RectF& local);

    double scale_;
    State cur_;
    std::vector<State> stack_;
    std::vector<PaintOp> ops_;
};

}  // namespace tcad::ui

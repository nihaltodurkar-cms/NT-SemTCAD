#include "ui/core/recording_painter.hpp"

#include <cstdio>

namespace tcad::ui {

std::vector<std::size_t> TextEngine::caretStops(std::string_view utf8, const TextStyle&) {
    std::vector<std::size_t> stops;
    for (std::size_t i = 0; i < utf8.size(); ++i)
        if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) stops.push_back(i);  // not a continuation byte
    stops.push_back(utf8.size());
    return stops;
}

Painter::Crisp Painter::crisp(const RectF& r, float width) const {
    const double s = scale();
    const int sw = std::max(1, static_cast<int>(std::lround(width * s)));
    const double l = std::round(r.x * s), t = std::round(r.y * s);
    const double rr = std::round(r.right() * s), b = std::round(r.bottom() * s);
    const double h = sw / 2.0;
    return {RectF{static_cast<float>((l + h) / s), static_cast<float>((t + h) / s),
                  static_cast<float>(std::max(0.0, rr - l - sw) / s), static_cast<float>(std::max(0.0, b - t - sw) / s)},
            static_cast<float>(sw / s)};
}

namespace {

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v));
    std::string s = b;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

std::string hex(Color c) {
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char b[16];
    std::snprintf(b, sizeof b, "#%02x%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b), byte(c.a));
    return b;
}

PaintOp op(const char* kind, Color c, float width = 0) {
    PaintOp o;
    o.kind = kind;
    o.color = c;
    o.width = width;
    return o;
}

RectF intersect(const RectF& a, const RectF& b) {
    const float l = std::max(a.x, b.x), t = std::max(a.y, b.y);
    const float r = std::min(a.right(), b.right()), bo = std::min(a.bottom(), b.bottom());
    return {l, t, std::max(0.0f, r - l), std::max(0.0f, bo - t)};
}

}  // namespace

std::string PaintOp::str() const {
    std::string s = kind + " " + fmt(rect.x) + "," + fmt(rect.y) + " ";
    if (kind == "line") s += "-> " + fmt(rect.right()) + "," + fmt(rect.bottom());
    else s += fmt(rect.width) + "x" + fmt(rect.height);
    s += " " + hex(color);
    if (width) s += " w=" + fmt(width);
    if (kind == "text")
        s += " \"" + text + "\" size=" + fmt(style.size) + (style.bold ? " bold" : "") +
             (style.family == FontFamily::Monospace ? " mono" : "") + (style.wrap ? " wrap" : "");
    return s;
}

RecordingPainter::RecordingPainter(double scale, SizeF canvas) : scale_(scale) {
    cur_.clip = {0, 0, canvas.width, canvas.height};
}

void RecordingPainter::save() { stack_.push_back(cur_); }

void RecordingPainter::restore() {
    if (stack_.empty()) return;
    cur_ = stack_.back();
    stack_.pop_back();
}

void RecordingPainter::translate(float dx, float dy) {
    cur_.dx += dx;
    cur_.dy += dy;
}

void RecordingPainter::clipRect(const RectF& r) {
    cur_.clip = intersect(cur_.clip, {r.x + cur_.dx, r.y + cur_.dy, r.width, r.height});
}

void RecordingPainter::add(PaintOp op, const RectF& local) {
    op.rect = {local.x + cur_.dx, local.y + cur_.dy, local.width, local.height};
    op.clip = cur_.clip;
    ops_.push_back(std::move(op));
}

void RecordingPainter::fillRect(const RectF& r, Color c) { add(op("fillRect", c), r); }

void RecordingPainter::strokeRect(const RectF& r, Color c, float w) { add(op("strokeRect", c, w), r); }

void RecordingPainter::fillRoundedRect(const RectF& r, float radius, Color c) {
    add(op("fillRoundedRect", c, radius), r);
}

void RecordingPainter::strokeRoundedRect(const RectF& r, float /*radius*/, Color c, float w) {
    add(op("strokeRoundedRect", c, w), r);
}

void RecordingPainter::drawLine(PointF a, PointF b, Color c, float w) {
    add(op("line", c, w), {a.x, a.y, b.x - a.x, b.y - a.y});
}

void RecordingPainter::fillEllipse(const RectF& r, Color c) { add(op("ellipse", c), r); }

void RecordingPainter::fillPolygon(const std::vector<PointF>& pts, Color c) {
    if (pts.empty()) return;
    float l = pts[0].x, t = pts[0].y, r = l, b = t;
    for (const auto& p : pts) {
        l = std::min(l, p.x), t = std::min(t, p.y), r = std::max(r, p.x), b = std::max(b, p.y);
    }
    add(op("polygon", c), {l, t, r - l, b - t});
}

void RecordingPainter::drawText(const RectF& r, std::string_view utf8, const TextStyle& style) {
    PaintOp o = op("text", style.color);
    o.text = std::string(utf8);
    o.style = style;
    add(std::move(o), r);
}

std::string RecordingPainter::dump() const {
    std::string s;
    for (const auto& op : ops_) s += op.str() + "\n";
    return s;
}

}  // namespace tcad::ui

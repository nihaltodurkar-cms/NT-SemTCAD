#include "ui/win32/d2d_painter.hpp"

#include <algorithm>

namespace tcad::ui {

namespace {

D2D1_RECT_F rf(const RectF& r) { return D2D1::RectF(r.x, r.y, r.right(), r.bottom()); }

}  // namespace

D2DPainter::D2DPainter(ID2D1DeviceContext2* ctx, RenderDevice& device, DWriteTextEngine& text, double scale)
    : ctx_(ctx), device_(device), text_(text), scale_(scale) {
    ctx_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_);
    apply();
}

D2DPainter::~D2DPainter() {
    while (!stack_.empty()) restore();
    for (; cur_.clips > 0; --cur_.clips) ctx_->PopAxisAlignedClip();
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
}

void D2DPainter::apply() { ctx_->SetTransform(D2D1::Matrix3x2F::Translation(cur_.dx, cur_.dy)); }

ID2D1SolidColorBrush* D2DPainter::brush(Color c) {
    brush_->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
    return brush_.Get();
}

void D2DPainter::save() {
    stack_.push_back(cur_);
    cur_.clips = 0;  // clips pushed from here on belong to this level
}

void D2DPainter::restore() {
    if (stack_.empty()) return;
    for (; cur_.clips > 0; --cur_.clips) ctx_->PopAxisAlignedClip();
    cur_ = stack_.back();
    stack_.pop_back();
    apply();
}

void D2DPainter::translate(float dx, float dy) {
    cur_.dx += dx;
    cur_.dy += dy;
    apply();
}

void D2DPainter::clipRect(const RectF& r) {
    // aliased: widget rectangles are whole pixels, so the clip edges are exact
    ctx_->PushAxisAlignedClip(rf(r), D2D1_ANTIALIAS_MODE_ALIASED);
    ++cur_.clips;
}

void D2DPainter::fillRect(const RectF& r, Color c) { ctx_->FillRectangle(rf(r), brush(c)); }

void D2DPainter::strokeRect(const RectF& r, Color c, float width) { ctx_->DrawRectangle(rf(r), brush(c), width); }

void D2DPainter::fillRoundedRect(const RectF& r, float radius, Color c) {
    ctx_->FillRoundedRectangle(D2D1::RoundedRect(rf(r), radius, radius), brush(c));
}

void D2DPainter::strokeRoundedRect(const RectF& r, float radius, Color c, float width) {
    ctx_->DrawRoundedRectangle(D2D1::RoundedRect(rf(r), radius, radius), brush(c), width);
}

void D2DPainter::drawLine(PointF a, PointF b, Color c, float width) {
    ctx_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush(c), width);
}

void D2DPainter::fillEllipse(const RectF& r, Color c) {
    ctx_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.x + r.width / 2, r.y + r.height / 2), r.width / 2, r.height / 2), brush(c));
}

void D2DPainter::fillPolygon(const std::vector<PointF>& pts, Color c) {
    if (pts.size() < 3) return;
    ComPtr<ID2D1PathGeometry> g;
    if (FAILED(device_.d2dFactory()->CreatePathGeometry(&g))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t i = 1; i < pts.size(); ++i) sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    ctx_->FillGeometry(g.Get(), brush(c));
}

void D2DPainter::drawText(const RectF& r, std::string_view utf8, const TextStyle& style) {
    // the engine's cached layout for this box: the same object measuring and hit-testing use
    IDWriteTextLayout* l = text_.layout(utf8, style, std::max(0.0f, r.width), std::max(0.0f, r.height));
    if (!l) return;
    ctx_->DrawTextLayout(D2D1::Point2F(r.x, r.y), l, brush(style.color),
                         D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

}  // namespace tcad::ui

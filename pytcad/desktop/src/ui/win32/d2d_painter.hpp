// The portable Painter on a Direct2D device context (N2b). Coordinates are DIPs (the context's DPI is the surface's
// scale); the widget tree only ever translates, so the state is an offset plus the number of clips pushed.
#pragma once

#include "ui/core/painter.hpp"
#include "ui/render/render_device.hpp"
#include "ui/win32/dwrite_text.hpp"

#include <vector>

namespace tcad::ui {

class D2DPainter final : public Painter {
public:
    D2DPainter(ID2D1DeviceContext2* ctx, RenderDevice& device, DWriteTextEngine& text, double scale);
    ~D2DPainter() override;  // pops anything left pushed

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

private:
    struct State {
        float dx = 0, dy = 0;
        int clips = 0;
    };
    ID2D1SolidColorBrush* brush(Color c);
    void apply();

    ID2D1DeviceContext2* ctx_;
    RenderDevice& device_;
    DWriteTextEngine& text_;
    double scale_;
    State cur_;
    std::vector<State> stack_;
    ComPtr<ID2D1SolidColorBrush> brush_;
};

}  // namespace tcad::ui

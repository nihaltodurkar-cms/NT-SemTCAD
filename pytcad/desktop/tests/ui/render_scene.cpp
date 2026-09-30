#include "render_scene.hpp"

namespace tcad::ui::testing {

namespace {

D2D1_COLOR_F rgb(unsigned hex) {
    return D2D1::ColorF(static_cast<UINT32>(hex), 1.0f);
}

}  // namespace

void drawScene(ID2D1DeviceContext2* ctx, RenderDevice& device) {
    ctx->Clear(rgb(0xF4F4F4));
    ComPtr<ID2D1SolidColorBrush> b;
    ctx->CreateSolidColorBrush(rgb(0x1F4E79), &b);
    ctx->FillRectangle(D2D1::RectF(16, 16, 112, 64), b.Get());

    b->SetColor(rgb(0x000000));
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(128, 16, 224, 64), 8, 8), b.Get(), 1.5f);

    b->SetColor(rgb(0xC0392B));
    ctx->DrawLine(D2D1::Point2F(16, 80), D2D1::Point2F(304, 120), b.Get(), 2.0f);

    b->SetColor(rgb(0x27AE60));
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(264, 40), 28, 20), b.Get());

    ComPtr<ID2D1PathGeometry> tri;
    if (SUCCEEDED(device.d2dFactory()->CreatePathGeometry(&tri))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(tri->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(250, 130), D2D1_FIGURE_BEGIN_FILLED);
            const D2D1_POINT_2F pts[] = {{304, 186}, {196, 186}};
            sink->AddLines(pts, 2);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            b->SetColor(rgb(0x8E44AD));
            ctx->FillGeometry(tri.Get(), b.Get());
        }
    }

    // A 1-DIP hairline on half-DIP coordinates: crisp at 100%, and the snapping behaviour is visible at 150%.
    b->SetColor(rgb(0x333333));
    ctx->DrawRectangle(D2D1::RectF(16.5f, 170.5f, 180.5f, 186.5f), b.Get(), 1.0f);

    ComPtr<IDWriteTextFormat> fmt;
    if (SUCCEEDED(device.dwrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f,
                                                    L"en-us", &fmt))) {
        b->SetColor(rgb(0x111111));
        const wchar_t text[] = L"PyTCAD N2a — µm, ∇·J = 0, 1.5×";
        ctx->DrawText(text, static_cast<UINT32>(wcslen(text)), fmt.Get(), D2D1::RectF(16, 128, 190, 164), b.Get());
    }
}

}  // namespace tcad::ui::testing

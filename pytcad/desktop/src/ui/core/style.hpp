// The framework's style (N2b): colours from theme/tokens.hpp -- still the ONE place a UI colour is written -- and
// the metrics layouts and widgets share, in DIPs. The metrics are the Qt Fusion values the Qt panels were laid
// out with (layout margin 9, spacing 6, a 12-DIP UI font), so ported panels keep their proportions.
#pragma once

#include "theme/tokens.hpp"
#include "ui/core/geometry.hpp"

namespace tcad::ui {

// Windows high contrast (N2f): when on, every CHROME token maps to one of the system's high-contrast colours, as
// Windows' own controls do; the data colours (Overlay, Context) keep theirs. The Win32 side fills this from
// SPI_GETHIGHCONTRAST/GetSysColor (UiWindow::applySystemHighContrast); tests set a fixed palette.
struct HighContrast {
    bool on = false;
    Color window, window_text, highlight, highlight_text, gray_text;
};
inline HighContrast& highContrast() {
    static HighContrast h;
    return h;
}

inline Color token(tcad::desktop::theme::T t) {
    using tcad::desktop::theme::T;
    if (const HighContrast& hc = highContrast(); hc.on) {
        switch (t) {
            case T::Background: case T::Window: case T::Base: case T::AlternateBase: return hc.window;
            case T::Border: case T::BorderStrong: case T::Text: case T::TextDim:
            case T::Warning: case T::Error: case T::Ok: return hc.window_text;
            case T::TextFaint: return hc.gray_text;
            case T::Focus: case T::Accent: case T::AccentSoft: case T::Selection: return hc.highlight;
            case T::OnAccent: return hc.highlight_text;
            default: break;  // Overlay, Context: data colours
        }
    }
    const auto c = tcad::desktop::theme::rgb(t);
    return {static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), 1.0f};
}

struct Style {
    float layout_margin = 9.0f;    // a widget's top-level layout (Fusion PM_LayoutLeftMargin); nested layouts: 0
    float layout_spacing = 6.0f;   // between items (Fusion PM_LayoutHorizontal/VerticalSpacing)
    float font_size = 12.0f;       // the UI font's em size
    float border_width = 1.0f;
    float corner_radius = 3.0f;
    // N3a widgets. Push buttons: Fusion's 80-DIP minimum width for a text button, the text padded 8 DIPs a side and
    // the line height plus 10 (26 DIPs for Segoe UI at 12). Check/radio indicators: 14 DIPs, 6 to the text
    // (Fusion's PM_IndicatorWidth and PM_CheckBoxLabelSpacing).
    float button_min_width = 80.0f;
    float button_pad_h = 8.0f;
    float button_pad_v = 5.0f;
    float indicator = 14.0f;
    float indicator_spacing = 6.0f;
    float focus_width = 2.0f;
    // A word-wrapped label's preferred width: Qt's 80 average characters, an average taken as 0.5 em.
    float wrap_hint_ems = 40.0f;

    static const Style& standard() {
        static const Style s;
        return s;
    }
};

}  // namespace tcad::ui

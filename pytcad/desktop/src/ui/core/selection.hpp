// Drawing a text selection (N3d, NATIVE-DESKTOP-PLAN.md 27.8.5), shared by the edits and the selectable label.
// Portable.
//
// ACTIVE (the widget has the focus): the Selection fill; in Windows high contrast the text inside it is drawn again in
// the system's highlight-text colour, as Windows' own edits do (the high-contrast Selection is the system highlight,
// and window-text on it can be unreadable).
// INACTIVE (the window or the widget lost the focus): the selection stays visible, as in Windows' edits that keep it
// (ES_NOHIDESEL) -- the alternate-base fill, which in high contrast is the window colour itself and so would vanish; there
// it is a one-DIP outline in the text colour (N2's carry-over: "the inactive selection in high contrast").
#pragma once

#include "ui/core/painter.hpp"
#include "ui/core/style.hpp"

#include <functional>
#include <vector>

namespace tcad::ui {

// Two steps, in this order: fillSelection (under the text), the text, then redrawSelectedText (over it).
// `rects`: the selection's rectangles in the painter's current coordinates.
inline void fillSelection(Painter& p, const std::vector<RectF>& rects, bool active) {
    using tcad::desktop::theme::T;
    const bool hc = highContrast().on;
    for (const RectF& r : rects) {
        if (active) {
            p.fillRect(r, token(T::Selection));
        } else if (hc) {
            const auto c = p.crisp(r, 1.0f);
            p.strokeRect(c.rect, token(T::Text), c.width);
        } else {
            p.fillRect(r, token(T::AlternateBase));
        }
    }
}

// `redraw_text(color)` draws the widget's text again in `color`, with the clip set to one selection rectangle.
inline void redrawSelectedText(Painter& p, const std::vector<RectF>& rects, bool active, const std::function<void(Color)>& redraw_text) {
    using tcad::desktop::theme::T;
    if (active && highContrast().on && redraw_text) {
        for (const RectF& r : rects) {
            p.save();
            p.clipRect(r);
            redraw_text(token(T::OnAccent));
            p.restore();
        }
    }
}

}  // namespace tcad::ui

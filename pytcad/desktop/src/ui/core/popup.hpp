// Popup windows (N3c, NATIVE-DESKTOP-PLAN.md 27.8.4; decision 3 of 27.8: menus and drop-downs are drawn in TOP-LEVEL
// popup windows, never inside the owner's client area -- a child surface can be clipped by, or drawn under, another
// window's content; the airspace rule the VTK view already taught). Portable: the interface, and the one pure piece of
// logic -- where a popup goes.
//
// A popup has its own window, its own widget tree (the `content`) and its own DPI (the monitor it opens on). It never
// takes the keyboard focus: the widget that opened it keeps the focus and drives the content (a combo box moves the
// content's highlight from its own key handler), and the content only takes the mouse. The service closes a popup by
// itself -- and says so through on_dismissed -- when the owner window is pressed outside the popup, deactivated, moved
// or resized; a popup never outlives its owner's interest in it.
#pragma once

#include "ui/core/geometry.hpp"
#include "ui/core/widget.hpp"

#include <functional>
#include <memory>

namespace tcad::ui {

class PopupHandle {
public:
    virtual ~PopupHandle() = default;
    // The popup went away without the opener asking (see above). Not called by close(). After it, the handle is gone.
    std::function<void()> on_dismissed;
    // Closes it; the handle is gone afterwards. Safe to call from the popup's own content (a click on a row).
    virtual void close() = 0;
    virtual Widget* content() const = 0;
    virtual RectI screenRectPx() const = 0;  // where it is, in device px on the virtual screen
};

class PopupService {
public:
    virtual ~PopupService() = default;
    // Opens `content` below `anchor` (a widget of the owner), at least as wide as the anchor, flipped above or shrunk
    // where the screen ends (placePopup). The content is laid out at the final size before it is shown. The handle is
    // owned by the service; it is valid until close() or on_dismissed.
    virtual PopupHandle* show(Widget* anchor, std::unique_ptr<Widget> content) = 0;
};

struct PopupPlacement {
    RectI rect;            // device px
    bool above = false;    // it opened above the anchor
    bool shrunk = false;   // it is shorter than asked: the content scrolls
};

// Below the anchor, left edges aligned, at least as wide as the anchor; moved left to fit the work area; above the
// anchor when it does not fit below and does fit above; else on the roomier side, cut to fit. All in device px;
// `work` is the monitor's work area (the screen minus the taskbar).
inline PopupPlacement placePopup(const RectI& anchor, SizeI want, const RectI& work) {
    PopupPlacement out;
    int w = std::min(std::max(want.width, anchor.width), work.width);
    int h = want.height;  // (taller than the work area: neither side fits it, so it is cut below)
    int x = anchor.x;
    if (x + w > work.right()) x = work.right() - w;
    x = std::max(x, work.x);
    const int below = work.bottom() - anchor.bottom();
    const int over = anchor.y - work.y;
    int y;
    if (h <= below) {
        y = anchor.bottom();
    } else if (h <= over) {
        y = anchor.y - h;
        out.above = true;
    } else if (below >= over) {
        y = anchor.bottom();
        h = std::max(below, 0);
        out.shrunk = true;
    } else {
        h = std::max(over, 0);
        y = anchor.y - h;
        out.above = true;
        out.shrunk = true;
    }
    out.rect = {x, y, w, h};
    return out;
}

}  // namespace tcad::ui

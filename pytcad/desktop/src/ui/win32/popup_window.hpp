// The Win32 side of popups (N3c, NATIVE-DESKTOP-PLAN.md 27.8.4; ui/core/popup.hpp has the contract): each popup is a
// UiWindow of its own -- a borderless, never-activated, owned top-level window (platform::PopupOptions) with its own
// surface, widget tree and text engine -- so it can never be clipped by, or drawn under, the owner's other children.
//
// Placement: the popup is first created 1x1 at its anchor (below it, beside it, or at a point), which puts it on that
// MONITOR and so gives it that monitor's DPI; its content is measured with its own text engine at that DPI, placePopup /
// placePopupBeside chooses the rectangle in the monitor's work area (flipping above or to the other side, or shrinking,
// where the screen ends), and the window is moved there. The content is shown at once (one frame is drawn before the
// window appears). The anchor may be a widget of ANOTHER popup (a submenu's entry): its own window's position and scale
// are used.
//
// A STACK: showing a popup normally dismisses the ones open (a combo's drop-down, a menu); a CHILD request (PopupRequest::child,
// a submenu) goes on top of them. Closing one closes everything above it. A TOOL TIP (PopupRequest::tooltip) lives apart: it
// neither dismisses nor is dismissed by the others, and goes when anything else is shown or dismissAll() runs.
//
// The owner calls dismissAll() when it is deactivated, moved, resized or its DPI changes. A closed popup's window is hidden
// at once and destroyed later (closing is called from inside the popup's own event handlers, which must finish first).
#pragma once

#include "ui/core/popup.hpp"

#include <memory>
#include <vector>

namespace tcad::ui {

class UiWindow;

class PopupWindowService final : public PopupService {
public:
    explicit PopupWindowService(UiWindow& owner);
    ~PopupWindowService() override;

    PopupHandle* show(Widget* anchor, std::unique_ptr<Widget> content) override;
    PopupHandle* showRequest(const PopupRequest& request, std::unique_ptr<Widget> content) override;
    // The owner's state changed under the popups: close them all (top first) and tell the openers.
    void dismissAll();
    void hideToolTip();

    PopupHandle* current() const;      // the top of the stack
    UiWindow* currentWindow() const;   // its window (tests drive it like any other)
    std::vector<UiWindow*> windows() const;  // the stack, bottom first
    UiWindow* toolTipWindow() const;
    int depth() const { return static_cast<int>(stack_.size()); }
    int shown() const { return shown_; }  // popups opened, ever (tool tips included)

private:
    class Popup;
    PopupHandle* open(const PopupRequest& request, std::unique_ptr<Widget> content);
    void closeOne(Popup* p, bool dismissed);
    UiWindow& owner_;
    std::vector<std::unique_ptr<Popup>> stack_;
    std::unique_ptr<Popup> tip_;
    std::vector<std::unique_ptr<Popup>> graveyard_;
    int shown_ = 0;
};

}  // namespace tcad::ui

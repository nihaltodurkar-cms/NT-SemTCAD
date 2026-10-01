// The Win32 side of popups (N3c, NATIVE-DESKTOP-PLAN.md 27.8.4; ui/core/popup.hpp has the contract): each popup is a
// UiWindow of its own -- a borderless, never-activated, owned top-level window (platform::PopupOptions) with its own
// surface, widget tree and text engine -- so it can never be clipped by, or drawn under, the owner's other children.
//
// Placement: the popup is first created 1x1 at the anchor's bottom-left corner, which puts it on the anchor's
// monitor and so gives it that monitor's DPI; its content is measured with its own text engine at that DPI, placePopup
// chooses the rectangle in the monitor's work area (flipping above, or shrinking, where the screen ends), and the
// window is moved there. The content is shown at once (one frame is drawn before the window appears).
//
// One popup at a time: showing a second dismisses the first. The owner calls dismissAll() when it is deactivated,
// moved, resized or its DPI changes. A closed popup's window is hidden at once and destroyed later (closing is
// called from inside the popup's own event handlers, which must finish first).
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
    // The owner's state changed under the popup: close it and tell the opener.
    void dismissAll();

    PopupHandle* current() const;
    UiWindow* currentWindow() const;  // the popup's window (tests drive it like any other)
    int shown() const { return shown_; }  // popups opened, ever

private:
    class Popup;
    void closeOne(Popup* p, bool dismissed);
    UiWindow& owner_;
    std::unique_ptr<Popup> current_;
    std::vector<std::unique_ptr<Popup>> graveyard_;
    int shown_ = 0;
};

}  // namespace tcad::ui

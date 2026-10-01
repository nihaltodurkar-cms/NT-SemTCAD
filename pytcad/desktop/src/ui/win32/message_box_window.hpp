// The Win32 side of the message box (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7; the content and its behaviour are
// ui/widgets/message_box.hpp): a real top-level DIALOG window -- a caption with a close button, no sizing, centred on its
// owner, drawn by the framework like any UiWindow.
//
//   exec():  MODAL. The owner is disabled and its popups are dismissed; a nested message loop runs until a button is pressed
//            (or the window is closed with the escape button's meaning), then the owner is enabled and activated again.
//            Other windows of the process keep running (timers, posted work): it is not a freeze.
//   show():  NON-MODAL. Returns at once; the window destroys itself after the button; `done` hears which.
// The window's close button presses the escape button (MessageBoxContent's rule: the named one, else Cancel, else the only
// button, else No, else Close); with none it is ignored. Showing one while another is open is allowed (each is its own window).
#pragma once

#include "ui/widgets/message_box.hpp"
#include "ui/win32/ui_window.hpp"

#include <functional>
#include <memory>

namespace tcad::ui {

class MessageBoxWindow {
public:
    ~MessageBoxWindow();
    MessageBoxWindow(const MessageBoxWindow&) = delete;
    MessageBoxWindow& operator=(const MessageBoxWindow&) = delete;

    static StandardButton exec(UiWindow& owner, const MessageBoxSpec& spec);
    static MessageBoxWindow* show(UiWindow& owner, const MessageBoxSpec& spec, std::function<void(StandardButton)> done = {});
    // The newest one still open (tests drive it with real messages through window()).
    static MessageBoxWindow* active();
    static int openCount();

    UiWindow& window() { return *window_; }
    MessageBoxContent& content() { return *content_; }
    bool isModal() const { return modal_; }
    StandardButton result() const { return result_; }
    bool isDone() const { return done_; }

private:
    MessageBoxWindow() = default;
    static std::unique_ptr<MessageBoxWindow> create(UiWindow& owner, const MessageBoxSpec& spec, bool modal);
    void finished(StandardButton b);
    UiWindow* owner_ = nullptr;
    std::unique_ptr<UiWindow> window_;
    MessageBoxContent* content_ = nullptr;
    std::function<void(StandardButton)> done_cb_;
    StandardButton result_ = StandardButton::None;
    bool modal_ = false;
    bool done_ = false;
};

}  // namespace tcad::ui

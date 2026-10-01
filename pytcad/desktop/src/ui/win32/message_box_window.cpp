#include "ui/win32/message_box_window.hpp"

#include "platform/app.hpp"
#include "ui/win32/dwrite_text.hpp"
#include "ui/win32/popup_window.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tcad::ui {

namespace {

std::vector<MessageBoxWindow*>& open() {
    static std::vector<MessageBoxWindow*> v;
    return v;
}

// Non-modal boxes own themselves: here until their button is pressed, then destroyed on the next turn of the loop.
std::vector<std::unique_ptr<MessageBoxWindow>>& selfOwned() {
    static std::vector<std::unique_ptr<MessageBoxWindow>> v;
    return v;
}

}  // namespace

MessageBoxWindow::~MessageBoxWindow() {
    open().erase(std::remove(open().begin(), open().end(), this), open().end());
}

MessageBoxWindow* MessageBoxWindow::active() { return open().empty() ? nullptr : open().back(); }
int MessageBoxWindow::openCount() { return static_cast<int>(open().size()); }

std::unique_ptr<MessageBoxWindow> MessageBoxWindow::create(UiWindow& owner, const MessageBoxSpec& spec, bool modal) {
    std::unique_ptr<MessageBoxWindow> mb(new MessageBoxWindow());
    mb->owner_ = &owner;
    mb->modal_ = modal;
    UiWindow::Options o;
    o.title = toUtf16(spec.title);
    o.dialog_owner = owner.window().hwnd();
    if (owner.hasScaleOverride()) o.scale_override = owner.scale();
    // a first guess at the size from a throw-away content measured with the owner's text engine; the real one is measured by
    // the dialog's own engine below
    o.width = 360;
    o.height = 140;
    auto made = UiWindow::create(owner.renderDevice(), o);
    if (!made) return nullptr;
    mb->window_ = std::move(*made);
    UiWindow& w = *mb->window_;
    mb->content_ = w.root().addChild<MessageBoxContent>(spec);
    mb->content_->name = "message_box";
    const SizeF hint = mb->content_->sizeHint();
    const int pw = static_cast<int>(std::ceil(hint.width * w.scale())), ph = static_cast<int>(std::ceil(hint.height * w.scale()));
    // size the CLIENT area, then centre the window on its owner
    RECT want{0, 0, pw, ph};
    const LONG style = GetWindowLongW(w.window().hwnd(), GWL_STYLE), ex = GetWindowLongW(w.window().hwnd(), GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&want, static_cast<DWORD>(style), FALSE, static_cast<DWORD>(ex), GetDpiForWindow(w.window().hwnd()));
    RECT orc{};
    GetWindowRect(owner.window().hwnd(), &orc);
    const int ww = want.right - want.left, wh = want.bottom - want.top;
    const int x = orc.left + ((orc.right - orc.left) - ww) / 2, y = orc.top + ((orc.bottom - orc.top) - wh) / 2;
    SetWindowPos(w.window().hwnd(), nullptr, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
    w.resizeClient(pw, ph);
    mb->content_->setGeometry({0, 0, pw, ph});
    MessageBoxWindow* raw = mb.get();
    mb->content_->on_finished = [raw](StandardButton b) { raw->finished(b); };
    // the window's close button: the escape button's meaning; with none, it stays open
    w.window().handlers.on_close_requested = [raw] {
        const StandardButton e = raw->content_->escapeButtonId();
        if (e == StandardButton::None) return false;
        raw->content_->press(e);
        return false;  // finished() closes it
    };
    open().push_back(raw);
    return mb;
}

void MessageBoxWindow::finished(StandardButton b) {
    if (done_) return;
    done_ = true;
    result_ = b;
    ShowWindow(window_->window().hwnd(), SW_HIDE);
    if (owner_ && modal_) {
        EnableWindow(owner_->window().hwnd(), TRUE);
        SetActiveWindow(owner_->window().hwnd());
    }
    if (done_cb_) done_cb_(b);
    if (!modal_) {  // destroyed after this handler (which is inside the box's own window) has returned
        MessageBoxWindow* self = this;
        platform::Application::instance().post([self] {
            auto& v = selfOwned();
            v.erase(std::remove_if(v.begin(), v.end(), [self](const auto& p) { return p.get() == self; }), v.end());
        });
    }
}

StandardButton MessageBoxWindow::exec(UiWindow& owner, const MessageBoxSpec& spec) {
    auto mb = create(owner, spec, true);
    if (!mb) return StandardButton::None;
    owner.popupService().dismissAll();
    EnableWindow(owner.window().hwnd(), FALSE);  // modal: the owner takes no input until the box is answered
    ShowWindow(mb->window_->window().hwnd(), SW_SHOW);
    SetForegroundWindow(mb->window_->window().hwnd());
    mb->window_->renderNow();
    if (PushButton* d = mb->content_->defaultButton()) d->setFocus(FocusReason::Other);
    platform::Application& app = platform::Application::instance();
    while (!mb->done_) {
        int code = 0;
        if (!app.pump(&code)) {  // the application is quitting under the box: let it go on quitting
            PostQuitMessage(code);
            break;
        }
        if (mb->done_) break;
        WaitMessage();
    }
    if (!mb->done_) {  // left without an answer (WM_QUIT): the owner must not stay disabled
        EnableWindow(owner.window().hwnd(), TRUE);
    }
    return mb->result_;
}

MessageBoxWindow* MessageBoxWindow::show(UiWindow& owner, const MessageBoxSpec& spec, std::function<void(StandardButton)> done) {
    auto mb = create(owner, spec, false);
    if (!mb) return nullptr;
    mb->done_cb_ = std::move(done);
    ShowWindow(mb->window_->window().hwnd(), SW_SHOWNOACTIVATE);
    mb->window_->renderNow();
    if (PushButton* d = mb->content_->defaultButton()) d->setFocus(FocusReason::Other);
    MessageBoxWindow* raw = mb.get();
    selfOwned().push_back(std::move(mb));
    return raw;
}

}  // namespace tcad::ui

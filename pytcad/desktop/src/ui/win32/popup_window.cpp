#include "ui/win32/popup_window.hpp"

#include "ui/win32/ui_window.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

class PopupWindowService::Popup final : public PopupHandle {
public:
    Popup(PopupWindowService& svc, std::unique_ptr<UiWindow> win) : svc_(svc), win_(std::move(win)) {}
    void close() override { svc_.closeOne(this, false); }
    Widget* content() const override { return content_; }
    RectI screenRectPx() const override {
        RECT r{};
        GetWindowRect(win_->window().hwnd(), &r);
        return {r.left, r.top, r.right - r.left, r.bottom - r.top};
    }
    PopupWindowService& svc_;
    std::unique_ptr<UiWindow> win_;
    Widget* content_ = nullptr;
};

PopupWindowService::PopupWindowService(UiWindow& owner) : owner_(owner) {}
PopupWindowService::~PopupWindowService() = default;

PopupHandle* PopupWindowService::current() const { return current_.get(); }
UiWindow* PopupWindowService::currentWindow() const { return current_ ? current_->win_.get() : nullptr; }

void PopupWindowService::closeOne(Popup* p, bool dismissed) {
    if (!p || p != current_.get()) return;
    auto cb = p->on_dismissed;
    ShowWindow(p->win_->window().hwnd(), SW_HIDE);
    graveyard_.push_back(std::move(current_));  // destroyed at the next show: we may be inside its own handler
    if (dismissed && cb) cb();
}

void PopupWindowService::dismissAll() {
    if (current_) closeOne(current_.get(), true);
}

PopupHandle* PopupWindowService::show(Widget* anchor, std::unique_ptr<Widget> content) {
    if (!anchor || !content) return nullptr;
    dismissAll();
    graveyard_.clear();  // earlier popups' windows, closed since
    HWND owner = owner_.window().hwnd();
    // the anchor on the screen, in device px (the owner's tree px, scaled as the mouse events are)
    const double k = owner_.window().dpiScale() / owner_.scale();
    const RectI a = anchor->windowRect();
    POINT origin{0, 0};
    ClientToScreen(owner, &origin);
    const RectI anchor_screen{origin.x + static_cast<int>(std::lround(a.x * k)), origin.y + static_cast<int>(std::lround(a.y * k)),
                              static_cast<int>(std::lround(a.width * k)), static_cast<int>(std::lround(a.height * k))};

    const RECT tentative{anchor_screen.x, anchor_screen.bottom(), anchor_screen.x + 1, anchor_screen.bottom() + 1};
    UiWindow::Options o;
    o.title = L"popup";
    o.popup = platform::PopupOptions{owner, tentative};
    if (owner_.hasScaleOverride()) o.scale_override = owner_.scale();
    auto made = UiWindow::create(owner_.renderDevice(), o);
    if (!made) return nullptr;
    auto win = std::move(*made);
    auto popup = std::make_unique<Popup>(*this, std::move(win));
    UiWindow& uw = *popup->win_;
    popup->content_ = uw.root().adopt(std::move(content));

    const SizeF hint = popup->content_->sizeHint();
    const SizeI want{static_cast<int>(std::ceil(hint.width * uw.scale())), static_cast<int>(std::ceil(hint.height * uw.scale()))};
    RECT work_rc{};
    const RECT as{anchor_screen.x, anchor_screen.y, anchor_screen.right(), anchor_screen.bottom()};
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoW(MonitorFromRect(&as, MONITOR_DEFAULTTONEAREST), &mi)) work_rc = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_rc, 0);
    const RectI work{work_rc.left, work_rc.top, work_rc.right - work_rc.left, work_rc.bottom - work_rc.top};
    const PopupPlacement pl = placePopup(anchor_screen, want, work);

    SetWindowPos(uw.window().hwnd(), HWND_TOP, pl.rect.x, pl.rect.y, pl.rect.width, pl.rect.height, SWP_NOACTIVATE);
    uw.resizeClient(pl.rect.width, pl.rect.height);  // (WM_SIZE did it; explicit for windows the OS does not resize)
    popup->content_->setGeometry({0, 0, pl.rect.width, pl.rect.height});
    uw.renderNow(nullptr, false);
    ShowWindow(uw.window().hwnd(), SW_SHOWNOACTIVATE);
    uw.renderNow();
    ++shown_;
    current_ = std::move(popup);
    return current_.get();
}

}  // namespace tcad::ui

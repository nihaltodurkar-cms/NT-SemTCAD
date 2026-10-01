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
    bool tip_ = false;
};

PopupWindowService::PopupWindowService(UiWindow& owner) : owner_(owner) {}
PopupWindowService::~PopupWindowService() = default;

PopupHandle* PopupWindowService::current() const { return stack_.empty() ? nullptr : stack_.back().get(); }
UiWindow* PopupWindowService::currentWindow() const { return stack_.empty() ? nullptr : stack_.back()->win_.get(); }
UiWindow* PopupWindowService::toolTipWindow() const { return tip_ ? tip_->win_.get() : nullptr; }

std::vector<UiWindow*> PopupWindowService::windows() const {
    std::vector<UiWindow*> out;
    for (auto& p : stack_) out.push_back(p->win_.get());
    return out;
}

void PopupWindowService::hideToolTip() {
    if (!tip_) return;
    ShowWindow(tip_->win_->window().hwnd(), SW_HIDE);
    graveyard_.push_back(std::move(tip_));
}

void PopupWindowService::closeOne(Popup* p, bool dismissed) {
    if (!p) return;
    if (p == tip_.get()) {
        hideToolTip();
        return;
    }
    const auto at = std::find_if(stack_.begin(), stack_.end(), [&](const auto& x) { return x.get() == p; });
    if (at == stack_.end()) return;
    // everything above it goes first (a submenu with its parent), top down
    while (stack_.size() > static_cast<std::size_t>(at - stack_.begin())) {
        std::unique_ptr<Popup> top = std::move(stack_.back());
        stack_.pop_back();
        auto cb = top->on_dismissed;
        ShowWindow(top->win_->window().hwnd(), SW_HIDE);
        graveyard_.push_back(std::move(top));  // destroyed at the next show: we may be inside its own handler
        if (dismissed && cb) cb();
    }
}

void PopupWindowService::dismissAll() {
    hideToolTip();
    while (!stack_.empty()) closeOne(stack_.front().get(), true);
}

PopupHandle* PopupWindowService::show(Widget* anchor, std::unique_ptr<Widget> content) {
    PopupRequest r;
    r.anchor = anchor;
    return showRequest(r, std::move(content));
}

PopupHandle* PopupWindowService::showRequest(const PopupRequest& request, std::unique_ptr<Widget> content) {
    if (!request.anchor || !content) return nullptr;
    if (request.tooltip) hideToolTip();
    else {
        hideToolTip();
        if (!request.child) dismissAll();
    }
    graveyard_.clear();  // earlier popups' windows, closed since
    return open(request, std::move(content));
}

PopupHandle* PopupWindowService::open(const PopupRequest& request, std::unique_ptr<Widget> content) {
    HWND owner = owner_.window().hwnd();
    // the anchor's own window: a submenu's entry lives in the parent menu's window, not the owner's
    UiWindow* aw = dynamic_cast<UiWindow*>(request.anchor->host());
    if (!aw) aw = &owner_;
    // the anchor on the screen, in device px (the window's tree px, scaled as the mouse events are)
    const double k = aw->window().dpiScale() / aw->scale();
    POINT origin{0, 0};
    ClientToScreen(aw->window().hwnd(), &origin);
    RectI anchor_screen;
    if (request.side == PopupSide::AtPoint) {
        const double d = aw->window().dpiScale();
        anchor_screen = {origin.x + static_cast<int>(std::lround(request.point.x * d)), origin.y + static_cast<int>(std::lround(request.point.y * d)), 0, 0};
    } else {
        const RectI a = request.anchor->windowRect();
        anchor_screen = {origin.x + static_cast<int>(std::lround(a.x * k)), origin.y + static_cast<int>(std::lround(a.y * k)),
                         static_cast<int>(std::lround(a.width * k)), static_cast<int>(std::lround(a.height * k))};
    }
    const RECT tentative = request.side == PopupSide::Right
                               ? RECT{anchor_screen.right(), anchor_screen.y, anchor_screen.right() + 1, anchor_screen.y + 1}
                               : RECT{anchor_screen.x, anchor_screen.bottom(), anchor_screen.x + 1, anchor_screen.bottom() + 1};
    UiWindow::Options o;
    o.title = L"popup";
    o.popup = platform::PopupOptions{owner, tentative};
    if (owner_.hasScaleOverride()) o.scale_override = owner_.scale();
    auto made = UiWindow::create(owner_.renderDevice(), o);
    if (!made) return nullptr;
    auto win = std::move(*made);
    auto popup = std::make_unique<Popup>(*this, std::move(win));
    popup->tip_ = request.tooltip;
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
    PopupPlacement pl;
    if (request.side == PopupSide::Right) pl = placePopupBeside(anchor_screen, want, work);
    else if (request.tooltip) {  // a tip hangs below-right of the pointer, clear of the cursor image
        RectI at = anchor_screen;
        at.x += static_cast<int>(std::lround(12 * aw->window().dpiScale()));
        at.y += static_cast<int>(std::lround(16 * aw->window().dpiScale()));
        pl = placePopup(at, want, work);
    } else pl = placePopup(anchor_screen, want, work);

    SetWindowPos(uw.window().hwnd(), HWND_TOP, pl.rect.x, pl.rect.y, pl.rect.width, pl.rect.height, SWP_NOACTIVATE);
    uw.resizeClient(pl.rect.width, pl.rect.height);  // (WM_SIZE did it; explicit for windows the OS does not resize)
    popup->content_->setGeometry({0, 0, pl.rect.width, pl.rect.height});
    uw.renderNow(nullptr, false);
    ShowWindow(uw.window().hwnd(), SW_SHOWNOACTIVATE);
    uw.renderNow();
    ++shown_;
    if (request.tooltip) {
        tip_ = std::move(popup);
        return tip_.get();
    }
    stack_.push_back(std::move(popup));
    return stack_.back().get();
}

}  // namespace tcad::ui

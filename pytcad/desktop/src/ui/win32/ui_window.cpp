#include "ui/win32/ui_window.hpp"

#include "ui/core/style.hpp"
#include "ui/win32/popup_window.hpp"

#include <UIAutomationCoreApi.h>
#include "platform/app.hpp"
#include "ui/win32/d2d_painter.hpp"

#include <stdexcept>

namespace tcad::ui {

namespace {

// The root: the window's background (the theme's Window colour) behind everything.
class RootWidget final : public Widget {
public:
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({0, 0, s.width, s.height}, token(tcad::desktop::theme::T::Window));
    }
};

// Widget timers on N1's Application (the process's message loop), when one exists.
class AppTimers final : public TimerService {
public:
    TimerId start(int ms, bool repeat, std::function<void()> fn) override {
        return platform::Application::instance().startTimer(ms, repeat, std::move(fn));
    }
    void stop(TimerId id) override { platform::Application::instance().stopTimer(id); }
};

bool haveApplication() {
    try {
        platform::Application::instance();
        return true;
    } catch (const std::logic_error&) {
        return false;
    }
}

LPCWSTR cursorId(Cursor c) {
    switch (c) {
        case Cursor::IBeam: return IDC_IBEAM;
        case Cursor::Hand: return IDC_HAND;
        case Cursor::SizeWE: return IDC_SIZEWE;
        case Cursor::SizeNS: return IDC_SIZENS;
        case Cursor::SizeAll: return IDC_SIZEALL;
        case Cursor::Cross: return IDC_CROSS;
        case Cursor::Wait: return IDC_WAIT;
        default: return IDC_ARROW;
    }
}

}  // namespace

std::expected<std::unique_ptr<UiWindow>, std::string> UiWindow::create(std::shared_ptr<RenderDevice> device,
                                                                      const Options& o) {
    std::unique_ptr<UiWindow> w(new UiWindow());
    w->device_ = std::move(device);
    w->scale_override_ = o.scale_override;
    auto win = platform::Window::create({.title = o.title, .width = o.width, .height = o.height, .popup = o.popup});
    if (!win) return std::unexpected(win.error());
    w->window_ = std::move(*win);
    w->scale_ = o.scale_override.value_or(w->window_->dpiScale());
    const auto [cw, ch] = w->window_->clientSize();
    w->client_ = {cw, ch};
    auto s = WindowSurface::create(w->device_, w->window_->hwnd(), cw, ch, w->scale_);
    if (!s) return std::unexpected(s.error());
    w->surface_ = std::move(*s);
    w->text_ = std::make_unique<DWriteTextEngine>(w->device_->dwrite());
    w->root_ = std::make_unique<RootWidget>();
    w->root_->name = "root";
    w->root_->setHost(w.get());
    w->router_ = std::make_unique<InputRouter>(*w->root_);
    if (haveApplication()) {
        w->timers_ = std::make_unique<AppTimers>();
        w->router_->setTimers(w->timers_.get());
    }
    w->router_->setDragThresholdPx(std::max(GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG)));
    w->uia_ = std::make_unique<UiaHost>(w->window_->hwnd(), *w->root_, *w->router_);
    w->router_->on_focus_changed = [u = w->uia_.get()](Widget* f) { u->focusChanged(f); };
    if (!o.popup) applySystemHighContrast();  // (a popup shares the palette its owner already read)
    // keyboard cues (N3a): the system setting "underline access keys" shows mnemonics always; otherwise after Alt
    BOOL cues = FALSE;
    if (SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &cues, 0)) w->router_->setAlwaysShowCues(cues != FALSE);

    UiWindow* self = w.get();
    auto& h = w->window_->handlers;
    h.on_resize = [self](int wpx, int hpx) {
        self->resizeClient(wpx, hpx);
        if (self->popups_) self->popups_->dismissAll();  // N3c: a popup does not outlive its window's geometry
    };
    h.on_moved = [self] {
        if (self->popups_) self->popups_->dismissAll();
    };
    h.on_dpi_changed = [self](double scale) {
        if (self->popups_) self->popups_->dismissAll();
        if (!self->scale_override_) self->setScale(scale);
    };
    h.on_paint = [self](HDC, const RECT&) { self->renderNow(); };
    // platform events are in LOGICAL px of the window's real DPI: to device px, then to this tree's DIPs
    h.on_mouse = [self](const platform::MouseEvent& e) {
        platform::MouseEvent d = e;
        const double k = self->window_->dpiScale() / self->scale_;
        d.x = e.x * k;
        d.y = e.y * k;
        self->router_->mouse(d);
    };
    h.on_key = [self](const platform::KeyEvent& e) { return self->router_->key(e); };
    h.on_char = [self](char32_t c) { self->router_->character(c); };
    h.on_focus = [self](bool focused) {
        self->router_->windowActivated(focused);
        if (!focused && self->popups_) self->popups_->dismissAll();  // deactivated: a drop-down closes
    };
    h.on_capture_lost = [self] { self->router_->cancelGrab(); };
    h.on_get_object = [self](WPARAM wp, LPARAM lp) -> std::optional<LRESULT> {
        LRESULT r = 0;
        if (self->uia_ && self->uia_->onGetObject(wp, lp, &r)) return r;
        return std::nullopt;
    };
    h.on_set_cursor = [self] {
        SetCursor(LoadCursorW(nullptr, cursorId(self->router_->cursor())));
        return true;
    };
    return w;
}

UiWindow::UiWindow() = default;

UiWindow::~UiWindow() {
    if (window_) window_->handlers = {};  // no more events into a half-destroyed window
    // The tree dies while still attached to this host: every widget stops its timers (on the Application, which
    // outlives this window) and the router forgets it. Detaching first would leave timers calling dead widgets.
    root_.reset();
    popups_.reset();  // after the tree: a dying combo box closes its popup through it
    if (window_) UiaReturnRawElementProvider(window_->hwnd(), 0, 0, nullptr);  // UIA releases what it cached
    uia_.reset();
    router_.reset();
    surface_.reset();
    window_.reset();
}

TimerService* UiWindow::timers() { return timers_.get(); }

PopupWindowService& UiWindow::popupService() {
    if (!popups_) popups_ = std::make_unique<PopupWindowService>(*this);
    return *popups_;
}

PopupService* UiWindow::popups() { return &popupService(); }

void UiWindow::announce(Widget* w, std::string_view text) {
    if (uia_) uia_->announce(w, text);
}

void UiWindow::widgetGone(Widget* w) {
    if (uia_) uia_->widgetGone(w);
    if (router_) router_->widgetGone(w);
}

bool UiWindow::applySystemHighContrast() {
    HIGHCONTRASTW hc{sizeof hc};
    const bool on = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    auto sys = [](int idx) {
        const COLORREF c = GetSysColor(idx);
        return Color{GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f};
    };
    HighContrast& p = highContrast();
    p.on = on;
    if (on) {
        p.window = sys(COLOR_WINDOW);
        p.window_text = sys(COLOR_WINDOWTEXT);
        p.highlight = sys(COLOR_HIGHLIGHT);
        p.highlight_text = sys(COLOR_HIGHLIGHTTEXT);
        p.gray_text = sys(COLOR_GRAYTEXT);
    }
    return on;
}

void UiWindow::resizeClient(int w_px, int h_px) {
    client_ = {std::max(0, w_px), std::max(0, h_px)};
    (void)surface_->resize(client_.width, client_.height);
    scheduleLayout();
}

void UiWindow::setScale(double scale) {
    if (scale <= 0 || scale == scale_) return;
    scale_ = scale;
    surface_->setDpiScale(scale);
    scheduleLayout();  // every hint changes in pixels
}

void UiWindow::requestPaint() {
    if (paint_requested_ || in_frame_) return;
    paint_requested_ = true;
    ++paint_requests_;
    InvalidateRect(window_->hwnd(), nullptr, FALSE);  // one WM_PAINT for any number of updates
}

void UiWindow::invalidate(const RectI& r) {
    const RectI clipped = r.intersected({0, 0, client_.width, client_.height});
    if (clipped.empty()) return;
    if (in_frame_) return;  // the frame being drawn repaints everything anyway
    dirty_ = dirty_.united(clipped);
    requestPaint();
}

void UiWindow::scheduleLayout() {
    layout_pending_ = true;
    invalidate({0, 0, client_.width, client_.height});
}

FrameStatus UiWindow::renderNow(Image* capture, bool present) {
    paint_requested_ = false;
    in_frame_ = true;
    if (layout_pending_) {
        layout_pending_ = false;
        ++layout_passes_;
        root_->setGeometry({0, 0, client_.width, client_.height});
    }
    const FrameStatus st = surface_->render(
        [&](ID2D1DeviceContext2* ctx) {
            D2DPainter p(ctx, *device_, *text_, scale_);
            root_->paintTree(p);
        },
        {.present = present, .vsync = capture == nullptr, .capture = capture});
    in_frame_ = false;
    dirty_ = {};
    if (st == FrameStatus::Presented) ++frames_;
    return st;
}

}  // namespace tcad::ui

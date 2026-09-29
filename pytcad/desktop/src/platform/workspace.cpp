#include "platform/workspace.hpp"

#include "platform/win32_util.hpp"
#include "theme/tokens.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::platform {
namespace {

COLORREF colour(tcad::desktop::theme::T t) {
    const auto c = tcad::desktop::theme::rgb(t);
    auto b = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return RGB(b(c.r), b(c.g), b(c.b));
}

}  // namespace

Workspace::Workspace(Window& window, Settings* settings) : window_(window), settings_(settings) {
    if (settings_)
        if (auto p = settings_->windowPlacement()) window_.applyPlacement(*p);
    window_.handlers.on_resize = [this](int, int) { relayout(); };
    window_.handlers.on_dpi_changed = [this](double) {
        relayout();
        window_.invalidate();
    };
    window_.handlers.on_key = [this](const KeyEvent& e) { return routeKey(e); };
    window_.handlers.on_paint = [this](HDC dc, const RECT& rc) { paint(dc, rc); };
    window_.handlers.on_close_requested = [this] {
        if (on_close_requested && !on_close_requested()) return false;
        savePlacement();
        return true;
    };
    setTitle("");
}

Workspace::~Workspace() {
    window_.handlers.on_resize = nullptr;
    window_.handlers.on_dpi_changed = nullptr;
    window_.handlers.on_key = nullptr;
    window_.handlers.on_paint = nullptr;
    window_.handlers.on_close_requested = nullptr;
}

WorkspaceLayout Workspace::layout() const {
    const auto [w, h] = window_.clientSize();
    return computeLayout(w, h, window_.dpiScale(), kStatusHeightLogical);
}

void Workspace::setContent(ChildHost* host) {
    content_ = host;
    relayout();
}

void Workspace::relayout() {
    if (!content_) return;
    const WorkspaceLayout l = layout();
    if (!l.content.empty()) content_->resizeTo(l.content);
    RECT strip{l.status.x, l.status.y, l.status.x + l.status.w, l.status.y + l.status.h};
    InvalidateRect(window_.hwnd(), &strip, FALSE);
}

void Workspace::setStatus(const std::string& utf8, StatusKind kind) {
    status_ = utf8;
    status_kind_ = kind;
    const WorkspaceLayout l = layout();
    RECT strip{l.status.x, l.status.y, l.status.x + l.status.w, l.status.y + l.status.h};
    InvalidateRect(window_.hwnd(), &strip, FALSE);
}

void Workspace::setTitle(const std::string& utf8) {
    window_.setTitle(widen(utf8.empty() ? std::string("PyTCAD") : utf8 + " - PyTCAD"));
}

void Workspace::savePlacement() {
    if (settings_) settings_->setWindowPlacement(window_.placement());
}

void Workspace::paint(HDC dc, const RECT& client) {
    using tcad::desktop::theme::T;
    const WorkspaceLayout l = computeLayout(client.right - client.left, client.bottom - client.top, window_.dpiScale(), kStatusHeightLogical);
    // Behind the content region only shows while a child is absent or resizing: the app background.
    RECT content{l.content.x, l.content.y, l.content.x + l.content.w, l.content.y + l.content.h};
    HBRUSH bg = CreateSolidBrush(colour(T::Background));
    FillRect(dc, &content, bg);
    DeleteObject(bg);
    RECT strip{l.status.x, l.status.y, l.status.x + l.status.w, l.status.y + l.status.h};
    HBRUSH sb = CreateSolidBrush(colour(T::Window));
    FillRect(dc, &strip, sb);
    DeleteObject(sb);
    if (l.status.empty()) return;
    HPEN pen = CreatePen(PS_SOLID, 1, colour(T::Border));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, strip.left, strip.top, nullptr);
    LineTo(dc, strip.right, strip.top);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    const double scale = window_.dpiScale();
    HFONT font = CreateFontW(-static_cast<int>(std::lround(9.0 * 96.0 * scale / 72.0)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour(status_kind_ == StatusKind::Error ? T::Error : T::Text));
    RECT text = strip;
    text.left += toDevice(8, scale);
    text.right -= toDevice(8, scale);
    const std::wstring w = widen(status_);
    DrawTextW(dc, w.c_str(), static_cast<int>(w.size()), &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font);
}

}  // namespace tcad::platform

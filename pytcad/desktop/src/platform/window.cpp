#include "platform/window.hpp"

#include <windowsx.h>

#include <format>

namespace tcad::platform {
namespace {

constexpr const wchar_t* kClassName = L"TcadPlatformWindow";

std::string lastErrorText(const char* what) {
    return std::format("{} failed (Win32 error {})", what, static_cast<unsigned long>(GetLastError()));
}

unsigned buttonBit(MouseButton b) { return 1u << static_cast<int>(b); }

}  // namespace

Mod Window::currentMods() {
    Mod m = Mod::None;
    if (GetKeyState(VK_CONTROL) & 0x8000) m = m | Mod::Ctrl;
    if (GetKeyState(VK_SHIFT) & 0x8000) m = m | Mod::Shift;
    if (GetKeyState(VK_MENU) & 0x8000) m = m | Mod::Alt;
    return m;
}

std::expected<std::unique_ptr<Window>, std::string> Window::create(const WindowOptions& o) {
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;  // double clicks; no CS_HREDRAW/VREDRAW: children and the paint handler cover the client area
        wc.lpfnWndProc = &Window::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) return std::unexpected(lastErrorText("RegisterClassEx"));
    if (o.popup) {
        std::unique_ptr<Window> pw(new Window());
        pw->popup_ = true;
        const RECT& pr = o.popup->rect;
        HWND ph = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClassName, o.title.c_str(), WS_POPUP | WS_CLIPCHILDREN,
                                  pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top, o.popup->owner, nullptr,
                                  GetModuleHandleW(nullptr), pw.get());
        if (!ph) return std::unexpected(lastErrorText("CreateWindowEx (popup)"));
        pw->hwnd_ = ph;
        return pw;
    }
    if (o.dialog_owner) {  // a dialog: a caption and a close button, never sized, not in the taskbar (it is owned)
        const DWORD dstyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        const DWORD dex = WS_EX_DLGMODALFRAME;
        const UINT ddpi = GetDpiForWindow(o.dialog_owner);
        RECT dr{0, 0, MulDiv(o.width, static_cast<int>(ddpi), 96), MulDiv(o.height, static_cast<int>(ddpi), 96)};
        AdjustWindowRectExForDpi(&dr, dstyle, FALSE, dex, ddpi);
        std::unique_ptr<Window> dw(new Window());
        dw->dialog_ = true;
        HWND dh = CreateWindowExW(dex, kClassName, o.title.c_str(), dstyle, CW_USEDEFAULT, CW_USEDEFAULT, dr.right - dr.left, dr.bottom - dr.top,
                                  o.dialog_owner, nullptr, GetModuleHandleW(nullptr), dw.get());
        if (!dh) return std::unexpected(lastErrorText("CreateWindowEx (dialog)"));
        dw->hwnd_ = dh;
        return dw;
    }
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    const UINT dpi = GetDpiForSystem();
    RECT r{0, 0, MulDiv(o.width, static_cast<int>(dpi), 96), MulDiv(o.height, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    std::unique_ptr<Window> w(new Window());
    HWND h = CreateWindowExW(0, kClassName, o.title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, GetModuleHandleW(nullptr), w.get());
    if (!h) return std::unexpected(lastErrorText("CreateWindowEx"));
    w->hwnd_ = h;
    if (o.placement) w->applyPlacement(*o.placement);
    return w;
}

Window::~Window() {
    if (hwnd_ && IsWindow(hwnd_)) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);  // no callbacks into a dead object
        DestroyWindow(hwnd_);
    }
}

std::pair<int, int> Window::clientSize() const {
    RECT r{};
    GetClientRect(hwnd_, &r);
    return {static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top)};
}

double Window::dpiScale() const { return GetDpiForWindow(hwnd_) / 96.0; }
void Window::setTitle(const std::wstring& t) { SetWindowTextW(hwnd_, t.c_str()); }
void Window::show(int cmd) {
    ShowWindow(hwnd_, cmd);
    UpdateWindow(hwnd_);
}
void Window::invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }
void Window::requestClose() { PostMessageW(hwnd_, WM_CLOSE, 0, 0); }

WindowPlacement Window::placement() const {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    GetWindowPlacement(hwnd_, &wp);
    const RECT& n = wp.rcNormalPosition;
    return {static_cast<int>(n.left), static_cast<int>(n.top), static_cast<int>(n.right - n.left), static_cast<int>(n.bottom - n.top),
            wp.showCmd == SW_SHOWMAXIMIZED};
}

bool Window::applyPlacement(const WindowPlacement& p) {
    if (p.w <= 0 || p.h <= 0) return false;
    RECT want{p.x, p.y, p.x + p.w, p.y + p.h};
    HMONITOR mon = MonitorFromRect(&want, MONITOR_DEFAULTTONULL);  // a saved position on a monitor that is gone: ignore
    if (!mon) return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    RECT inter{};
    if (!IntersectRect(&inter, &want, &mi.rcWork) || (inter.right - inter.left) < 100 || (inter.bottom - inter.top) < 50) return false;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    wp.rcNormalPosition = want;
    wp.showCmd = p.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    return SetWindowPlacement(hwnd_, &wp) != 0;
}

LRESULT CALLBACK Window::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (!self->hwnd_) self->hwnd_ = h;  // messages arrive during CreateWindowEx, before it returns
    return self->handle(msg, wp, lp);
}

void Window::mouse(MouseType type, MouseButton button, LPARAM lp, double wheel, bool screen_coords) {
    if (!handlers.on_mouse) return;
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (screen_coords) ScreenToClient(hwnd_, &pt);
    const double s = dpiScale();
    MouseEvent e;
    e.type = type;
    e.button = button;
    e.x = pt.x / s;
    e.y = pt.y / s;
    e.wheel_steps = wheel;
    e.mods = currentMods();
    e.buttons_down = buttons_down_;
    handlers.on_mouse(e);
}

LRESULT Window::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && handlers.on_resize) handlers.on_resize(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_DPICHANGED: {  // per-monitor v2: Windows hands us the rectangle that keeps the size
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (handlers.on_dpi_changed) handlers.on_dpi_changed(HIWORD(wp) / 96.0);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_GETOBJECT:  // N2f: UI Automation asks for this window's provider
            if (handlers.on_get_object)
                if (auto r = handlers.on_get_object(wp, lp)) return *r;
            break;
        case WM_SETCURSOR:  // N2c: the widget under the pointer chooses the cursor
            if (LOWORD(lp) == HTCLIENT && handlers.on_set_cursor && handlers.on_set_cursor()) return TRUE;
            break;
        case WM_CAPTURECHANGED:  // capture taken away (Alt+Tab, a dialog): no button is held for us any more
            if (reinterpret_cast<HWND>(lp) != hwnd_ && buttons_down_) {
                buttons_down_ = 0;
                if (handlers.on_capture_lost) handlers.on_capture_lost();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc;
            GetClientRect(hwnd_, &rc);
            if (handlers.on_paint) handlers.on_paint(dc, rc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_CLOSE:
            if (!handlers.on_close_requested || handlers.on_close_requested()) DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            if (!popup_ && !dialog_) PostQuitMessage(0);  // a popup or a dialog going away is not the application ending
            return 0;
        case WM_MOUSEACTIVATE:
            if (popup_) return MA_NOACTIVATE;  // clicking a popup leaves the owner active
            break;
        case WM_MOVE:
            if (handlers.on_moved) handlers.on_moved();
            break;
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:  // high contrast turned on or off, the colours changed: the framework re-reads them (N3f)
            if (handlers.on_system_colors_changed) handlers.on_system_colors_changed();
            break;
        case WM_SYSCHAR:  // the framework's own Alt+<letter> mnemonics: nothing for Windows to beep about (Alt+Space stays the system menu's)
            if (wp != L' ' && handlers.on_key) return 0;
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (handlers.on_focus) handlers.on_focus(msg == WM_SETFOCUS);
            return 0;
        case WM_MOUSEMOVE:
            if (!tracking_leave_) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
                tracking_leave_ = TrackMouseEvent(&t) != 0;
            }
            mouse(MouseType::Move, MouseButton::None, lp);
            return 0;
        case WM_MOUSELEAVE:
            tracking_leave_ = false;
            mouse(MouseType::Leave, MouseButton::None, lp);
            return 0;
        case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN: case WM_XBUTTONDOWN:
        case WM_LBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_XBUTTONDBLCLK: {
            MouseButton b = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK ? MouseButton::Left
                          : msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK ? MouseButton::Middle
                          : msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK ? MouseButton::Right
                          : GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            const bool dbl = msg == WM_LBUTTONDBLCLK || msg == WM_MBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK || msg == WM_XBUTTONDBLCLK;
            if (!buttons_down_) SetCapture(hwnd_);
            buttons_down_ |= buttonBit(b);
            mouse(dbl ? MouseType::DoubleClick : MouseType::Down, b, lp);
            return msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK ? TRUE : 0;
        }
        case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP: case WM_XBUTTONUP: {
            MouseButton b = msg == WM_LBUTTONUP ? MouseButton::Left : msg == WM_MBUTTONUP ? MouseButton::Middle
                          : msg == WM_RBUTTONUP ? MouseButton::Right : GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            buttons_down_ &= ~buttonBit(b);
            if (!buttons_down_ && GetCapture() == hwnd_) ReleaseCapture();
            mouse(MouseType::Up, b, lp);
            return msg == WM_XBUTTONUP ? TRUE : 0;
        }
        case WM_MOUSEWHEEL:   // lParam is in SCREEN coordinates
            mouse(MouseType::Wheel, MouseButton::None, lp, GET_WHEEL_DELTA_WPARAM(wp) / static_cast<double>(WHEEL_DELTA), true);
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            if (!handlers.on_key) break;
            KeyEvent e;
            e.vk = static_cast<int>(wp);
            e.mods = currentMods();
            e.down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            e.repeat = e.down && (lp & (1 << 30)) != 0;
            if (handlers.on_key(e)) return 0;
            break;  // not handled: Alt+F4, the system menu, ...
        }
        case WM_CHAR: {
            if (!handlers.on_char) return 0;
            const wchar_t c = static_cast<wchar_t>(wp);
            if (c >= 0xD800 && c <= 0xDBFF) { high_surrogate_ = c; return 0; }
            char32_t cp = c;
            if (c >= 0xDC00 && c <= 0xDFFF && high_surrogate_) cp = 0x10000 + ((static_cast<char32_t>(high_surrogate_) - 0xD800) << 10) + (c - 0xDC00);
            high_surrogate_ = 0;
            if (cp >= 0x20 && cp != 0x7F) handlers.on_char(cp);  // control characters arrive as keys
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace tcad::platform

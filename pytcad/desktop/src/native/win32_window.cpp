#include "native/win32_window.hpp"

#include <format>

namespace tcad::native {
namespace {

constexpr const wchar_t* kClassName = L"TcadNativeWindow";

std::string lastErrorText(const char* what) {
    return std::format("{} failed (Win32 error {})", what, static_cast<unsigned long>(GetLastError()));
}

}  // namespace

std::expected<std::unique_ptr<Window>, std::string> Window::create(const std::wstring& title, int client_w,
                                                                   int client_h) {
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Window::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // the render window paints everything; no flicker
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) return std::unexpected(lastErrorText("RegisterClassEx"));

    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    const UINT dpi = GetDpiForSystem();
    RECT r{0, 0, MulDiv(client_w, static_cast<int>(dpi), 96), MulDiv(client_h, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);

    std::unique_ptr<Window> w(new Window());
    HWND h = CreateWindowExW(0, kClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                             r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), w.get());
    if (!h) return std::unexpected(lastErrorText("CreateWindowEx"));
    w->hwnd_ = h;
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

void Window::setTitle(const std::wstring& title) { SetWindowTextW(hwnd_, title.c_str()); }

void Window::show() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

bool Window::pump(int* exit_code) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            if (exit_code) *exit_code = static_cast<int>(msg.wParam);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

int Window::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void Window::requestClose() { PostMessageW(hwnd_, WM_CLOSE, 0, 0); }

LRESULT CALLBACK Window::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

LRESULT Window::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && callbacks.on_size) callbacks.on_size(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_DPICHANGED: {  // per-monitor v2: Windows hands us the rectangle that keeps the size
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            if (callbacks.on_close) callbacks.on_close();
            else DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

}  // namespace tcad::native

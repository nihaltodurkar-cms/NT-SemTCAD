// A plain Win32 top-level window (NATIVE-DESKTOP-PLAN.md section 27.4, spike). No UI
// toolkit: RegisterClassEx / CreateWindowEx / a message loop. The TCAD widget framework
// (Direct2D + DirectWrite, section 27.5) is built on windows like this one.
#pragma once

#include <windows.h>

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace tcad::native {

class Window {
public:
    struct Callbacks {
        std::function<void(int, int)> on_size;  // client size in DEVICE pixels; not called while minimised
        std::function<void()> on_close;         // WM_CLOSE; if empty the window is destroyed
    };

    // A client area of client_w x client_h LOGICAL pixels at the system DPI.
    static std::expected<std::unique_ptr<Window>, std::string> create(const std::wstring& title, int client_w,
                                                                       int client_h);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    HWND hwnd() const { return hwnd_; }
    Callbacks callbacks;

    std::pair<int, int> clientSize() const;  // device pixels
    double dpiScale() const;                 // this window's DPI / 96
    void setTitle(const std::wstring& title);
    void show();
    // Handle every queued message; false once WM_QUIT was seen (then *exit_code is set).
    bool pump(int* exit_code = nullptr);
    // Blocking loop until the window closes; returns the exit code.
    int run();
    void requestClose();

private:
    Window() = default;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    HWND hwnd_ = nullptr;
};

}  // namespace tcad::native

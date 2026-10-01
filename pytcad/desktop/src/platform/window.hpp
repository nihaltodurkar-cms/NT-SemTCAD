// A native top-level window with input and DPI handling (N1). The Win32 messages become the platform-neutral
// events of input.hpp (coordinates in LOGICAL pixels); DPI changes (per-monitor v2) are reported and applied.
// No UI toolkit: this is RegisterClassEx / CreateWindowEx and a WndProc. The TCAD widget framework (N2) is built
// on windows like this one.
#pragma once

#include "platform/input.hpp"
#include "platform/settings.hpp"

#include <windows.h>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace tcad::platform {

// A popup (N3c): a borderless top-level window that is never activated (clicking it keeps the owner active), has no
// taskbar button, and is placed exactly: `rect` in device px on the virtual screen. Closing one never ends the app.
struct PopupOptions {
    HWND owner = nullptr;
    RECT rect{};
};

struct WindowOptions {
    std::wstring title = L"PyTCAD";
    int width = 1280, height = 800;                  // the client area, LOGICAL px at the system DPI
    std::optional<WindowPlacement> placement;        // restored when it still lies on a monitor
    std::optional<PopupOptions> popup;               // a popup instead of a normal window (width/height are ignored)
    HWND dialog_owner = nullptr;                     // N3f: a DIALOG owned by this window (caption, no sizing; centred by the caller)
};

class Window {
public:
    struct Handlers {
        std::function<void(int w_px, int h_px)> on_resize;   // client size, DEVICE px; not while minimised
        std::function<bool()> on_close_requested;            // false: keep the window open
        std::function<void(double scale)> on_dpi_changed;    // after the window was moved/resized to the new DPI
        std::function<void(const MouseEvent&)> on_mouse;
        std::function<bool(const KeyEvent&)> on_key;         // true: handled (else the default runs: Alt+F4, menu keys)
        std::function<void(char32_t)> on_char;
        std::function<void(bool focused)> on_focus;
        std::function<void(HDC dc, const RECT& client)> on_paint;
        std::function<bool()> on_set_cursor;                 // WM_SETCURSOR over the client area: true = cursor set (N2c)
        std::function<void()> on_moved;                      // WM_MOVE: the window moved on the screen (N3c: popups close)
        std::function<void()> on_capture_lost;               // the mouse capture was taken away mid-press (N2c)
        std::function<void()> on_system_colors_changed;      // WM_SYSCOLORCHANGE / WM_THEMECHANGED / WM_SETTINGCHANGE (N3f: high contrast on or off)
        std::function<std::optional<LRESULT>(WPARAM, LPARAM)> on_get_object;  // WM_GETOBJECT: UI Automation (N2f)
    };

    static std::expected<std::unique_ptr<Window>, std::string> create(const WindowOptions& options);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Handlers handlers;

    HWND hwnd() const { return hwnd_; }
    std::pair<int, int> clientSize() const;  // device px
    double dpiScale() const;                 // this window's DPI / 96
    void setTitle(const std::wstring& title);
    void show(int cmd = SW_SHOW);
    void invalidate();
    void requestClose();                     // like the close button
    WindowPlacement placement() const;       // normal rectangle + maximised, device px
    // Apply a saved placement if it still intersects a monitor's work area; false (nothing changed) otherwise.
    bool applyPlacement(const WindowPlacement& p);

private:
    Window() = default;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void mouse(MouseType type, MouseButton button, LPARAM lp, double wheel = 0, bool screen_coords = false);
    static Mod currentMods();
    HWND hwnd_ = nullptr;
    bool tracking_leave_ = false;
    bool popup_ = false;
    bool dialog_ = false;
    unsigned buttons_down_ = 0;
    wchar_t high_surrogate_ = 0;
};

}  // namespace tcad::platform

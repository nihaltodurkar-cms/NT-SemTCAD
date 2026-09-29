// The Window/Workspace foundation (N1): a top-level Window plus the arrangement of what it shows -- one content
// region that hosts a native child view (the VTK field view today), a status strip, keyboard shortcuts, and
// the window's saved placement. It is the seed of the docking workspace (N4); N2's widget framework will draw
// the strip with Direct2D/DirectWrite -- here it is plain GDI text, a deliberate placeholder.
#pragma once

#include "platform/dpi.hpp"
#include "platform/input.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

#include <windows.h>

#include <functional>
#include <string>

namespace tcad::platform {

// Something a Workspace can host in its content region: a native child window that it sizes.
class ChildHost {
public:
    virtual ~ChildHost() = default;
    virtual HWND childWindow() const = 0;
    virtual void resizeTo(const Rect& device_px) = 0;  // the content region, in the window's client coordinates
};

enum class StatusKind { Normal, Error };

class Workspace {
public:
    static constexpr double kStatusHeightLogical = 24.0;

    // `settings` may be null (nothing is restored or saved).
    Workspace(Window& window, Settings* settings);
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    void setContent(ChildHost* host);  // sizes it now and on every resize / DPI change
    ShortcutMap& shortcuts() { return shortcuts_; }
    // A key from the window or from a hosted child window (which owns the keyboard focus when it is clicked).
    bool routeKey(const KeyEvent& e) { return shortcuts_.dispatch(e); }

    void setStatus(const std::string& utf8, StatusKind kind = StatusKind::Normal);
    const std::string& status() const { return status_; }
    void setTitle(const std::string& utf8);  // "<utf8> - PyTCAD" (empty: just the application name)

    WorkspaceLayout layout() const;  // current, from the window's client size and DPI
    void relayout();
    void savePlacement();            // into the settings (the caller syncs)
    // Set by the application: return false to keep the window open (unsaved work).
    std::function<bool()> on_close_requested;

private:
    void paint(HDC dc, const RECT& client);
    Window& window_;
    Settings* settings_;
    ChildHost* content_ = nullptr;
    ShortcutMap shortcuts_;
    std::string status_;
    StatusKind status_kind_ = StatusKind::Normal;
};

}  // namespace tcad::platform

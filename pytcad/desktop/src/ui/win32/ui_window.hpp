// A window of the native UI framework (N2b): an N1 platform::Window, its N2a WindowSurface, and a widget tree whose
// root fills the client area. It is the tree's UiHost:
//   * invalidate() accumulates a dirty rectangle and asks Windows for WM_PAINT (coalesced: many updates, one frame);
//   * scheduleLayout() marks a layout pass, done once before the next frame;
//   * a frame lays out if needed, then repaints the WHOLE tree (27.7: full-frame, Present1 dirty rects only if
//     measured to matter), then presents.
// A scale override pins the DPI scale (tests render 150% and 200% on a 100% monitor); without it the window's DPI is
// followed, including WM_DPICHANGED.
//
// Input (N2c): the window's mouse, key, character, focus, cursor and capture messages go to an InputRouter with
// coordinates converted to window DIPs (logical px x the window's DPI scale = device px; / the UI scale = DIPs).
// Widget timers run on N1's Application timers when an Application exists (otherwise there are none).
#pragma once

#include "platform/window.hpp"
#include "ui/core/input_router.hpp"
#include "ui/core/widget.hpp"
#include "ui/render/window_surface.hpp"
#include "ui/win32/uia_provider.hpp"
#include "ui/win32/dwrite_text.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace tcad::ui {

class PopupWindowService;

class UiWindow final : public UiHost {
public:
    struct Options {
        std::wstring title = L"PyTCAD";
        int width = 800, height = 600;         // client area, logical px at the system DPI
        std::optional<double> scale_override;  // pin the DPI scale (tests)
        std::optional<platform::PopupOptions> popup;  // a popup window (N3c; ui/win32/popup_window.hpp), not a normal one
    };

    static std::expected<std::unique_ptr<UiWindow>, std::string> create(std::shared_ptr<RenderDevice> device,
                                                                        const Options& options);
    ~UiWindow() override;
    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;

    Widget& root() { return *root_; }
    platform::Window& window() { return *window_; }
    WindowSurface& surface() { return *surface_; }
    DWriteTextEngine& text() { return *text_; }
    InputRouter& router() { return *router_; }
    UiaHost& uia() { return *uia_; }
    std::shared_ptr<RenderDevice> renderDevice() const { return device_; }
    bool hasScaleOverride() const { return scale_override_.has_value(); }
    PopupWindowService& popupService();  // made on first use; also what popups() hands to widgets
    // Windows high contrast (N2f): read SPI_GETHIGHCONTRAST and the system colours into the style's palette.
    static bool applySystemHighContrast();

    // Set the client size in device px without moving the real window (hidden test windows); a real window is
    // resized by the user and follows through WM_SIZE.
    void resizeClient(int w_px, int h_px);
    SizeI clientSize() const { return client_; }
    void setScale(double scale);  // as a DPI change would

    // Lay out if needed, paint the whole tree, present (and optionally capture the frame).
    FrameStatus renderNow(Image* capture = nullptr, bool present = true);

    const RectI& dirty() const { return dirty_; }  // accumulated since the last frame
    bool layoutPending() const { return layout_pending_; }
    int layoutPasses() const { return layout_passes_; }
    int frames() const { return frames_; }
    int paintRequests() const { return paint_requests_; }  // InvalidateRect calls made (coalesced)

    // UiHost
    void invalidate(const RectI& window_px) override;
    void scheduleLayout() override;
    TextEngine& textEngine() override { return *text_; }
    double scale() const override { return scale_; }
    InputRouter* input() override { return router_.get(); }
    TimerService* timers() override;
    void widgetGone(Widget* w) override;
    PopupService* popups() override;
    void announce(Widget* w, std::string_view text) override;

private:
    UiWindow();
    void requestPaint();

    std::shared_ptr<RenderDevice> device_;
    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<WindowSurface> surface_;
    std::unique_ptr<DWriteTextEngine> text_;
    std::unique_ptr<Widget> root_;
    std::unique_ptr<InputRouter> router_;
    std::unique_ptr<UiaHost> uia_;
    std::unique_ptr<TimerService> timers_;
    std::unique_ptr<PopupWindowService> popups_;
    std::optional<double> scale_override_;
    double scale_ = 1.0;
    SizeI client_;
    RectI dirty_;
    bool layout_pending_ = true;
    bool paint_requested_ = false;
    bool in_frame_ = false;
    int layout_passes_ = 0;
    int frames_ = 0;
    int paint_requests_ = 0;
};

}  // namespace tcad::ui

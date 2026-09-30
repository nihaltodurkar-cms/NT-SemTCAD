// A window's drawing surface (N2a, NATIVE-DESKTOP-PLAN.md 27.7): a DXGI flip-model swap chain on the process's
// D3D12 queue, whose back buffers are wrapped for D3D11On12 and targeted by a Direct2D device context.
//
// A frame:  beginFrame()  ->  draw with the returned ID2D1DeviceContext (in DIPs; the context's DPI is the
// surface's scale)  ->  endFrame().  endFrame releases the wrapped buffer, flushes D3D11On12 onto the D3D12 queue
// and presents. It can also copy the finished frame to a CPU Image first (screenshots, goldens) -- before Present,
// because a FLIP_DISCARD back buffer is undefined afterwards.
//
// Device loss: endFrame reports Lost (D2DERR_RECREATE_TARGET, DXGI_ERROR_DEVICE_REMOVED/RESET). render() handles it:
// RenderDevice::recover(), rebuild this surface for the new device generation, draw the frame again.
//
// Size 0 (a minimised window) keeps the buffers and skips frames. Not thread-safe; one UI thread.
#pragma once

#include "ui/render/image.hpp"
#include "ui/render/render_device.hpp"

#include <windows.h>

#include <array>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace tcad::ui {

enum class FrameStatus {
    Presented,  // drawn (and presented, unless FrameOptions::present was false)
    Skipped,    // nothing to draw: zero size
    Lost,       // the device was lost during the frame; recover and redraw (render() does)
    Failed,     // any other error: lastError() says what
};

struct FrameOptions {
    bool present = true;
    bool vsync = true;          // Present(1) when true, Present(0) otherwise (tests)
    Image* capture = nullptr;   // when set, the finished frame is copied here before Present
};

class WindowSurface final : public DeviceClient {
public:
    static constexpr UINT kBufferCount = 2;

    // w_px/h_px: the back-buffer size in device pixels (normally the window's client size).
    static std::expected<std::unique_ptr<WindowSurface>, std::string> create(std::shared_ptr<RenderDevice> device,
                                                                             HWND hwnd, int w_px, int h_px,
                                                                             double dpi_scale = 1.0);
    ~WindowSurface() override;
    WindowSurface(const WindowSurface&) = delete;
    WindowSurface& operator=(const WindowSurface&) = delete;

    std::expected<void, std::string> resize(int w_px, int h_px);
    void setDpiScale(double scale) { dpi_scale_ = scale; }
    double dpiScale() const { return dpi_scale_; }
    int widthPx() const { return w_; }
    int heightPx() const { return h_; }
    // The surface in DIPs (what the drawing code sees).
    D2D1_SIZE_F sizeDips() const { return {static_cast<float>(w_ / dpi_scale_), static_cast<float>(h_ / dpi_scale_)}; }
    std::uint64_t builtForGeneration() const { return generation_; }
    RenderDevice& device() const { return *device_; }
    const std::string& lastError() const { return error_; }

    // DeviceClient: RenderDevice::recover() calls this before rebuilding; the next frame rebuilds the surface.
    void releaseDeviceObjects() override;

    // nullptr: nothing to draw (zero size) or the surface could not be (re)built -- see lastError().
    ID2D1DeviceContext2* beginFrame();
    FrameStatus endFrame(const FrameOptions& options = {});

    // One whole frame with device-loss recovery: draws, and on Lost recovers the device, rebuilds, draws again
    // (at most twice). Returns the final status.
    FrameStatus render(const std::function<void(ID2D1DeviceContext2*)>& draw, const FrameOptions& options = {});

private:
    WindowSurface() = default;
    std::expected<void, std::string> build();      // swap chain + context + targets for the current device
    std::expected<void, std::string> wrapTargets();
    void releaseTargets();
    void releaseAll();
    std::expected<void, std::string> copyToImage(ID2D1Bitmap1* target, Image& out);

    std::shared_ptr<RenderDevice> device_;
    HWND hwnd_ = nullptr;
    int w_ = 0, h_ = 0;
    int built_w_ = 0, built_h_ = 0;
    double dpi_scale_ = 1.0;
    std::uint64_t generation_ = 0;
    std::string error_;

    ComPtr<IDXGISwapChain3> swap_;
    HANDLE waitable_ = nullptr;
    ComPtr<ID2D1DeviceContext2> ctx_;
    std::array<ComPtr<ID3D11Resource>, kBufferCount> wrapped_;
    std::array<ComPtr<ID2D1Bitmap1>, kBufferCount> targets_;
    ComPtr<ID2D1Bitmap1> readback_;  // CPU-readable copy target, sized with the surface
    UINT frame_index_ = 0;
    bool in_frame_ = false;
};

}  // namespace tcad::ui

#include "ui/render/window_surface.hpp"

#include <algorithm>
#include <cstring>

namespace tcad::ui {

namespace {

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr UINT kSwapFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

bool isLoss(HRESULT hr) {
    return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG;
}

}  // namespace

std::expected<std::unique_ptr<WindowSurface>, std::string> WindowSurface::create(std::shared_ptr<RenderDevice> device,
                                                                                  HWND hwnd, int w_px, int h_px,
                                                                                  double dpi_scale) {
    std::unique_ptr<WindowSurface> s(new WindowSurface());
    s->device_ = std::move(device);
    s->hwnd_ = hwnd;
    s->w_ = std::max(0, w_px);
    s->h_ = std::max(0, h_px);
    s->dpi_scale_ = dpi_scale > 0 ? dpi_scale : 1.0;
    if (auto r = s->build(); !r) return std::unexpected(r.error());
    s->device_->attach(s.get());
    return s;
}

WindowSurface::~WindowSurface() {
    releaseAll();
    if (device_) device_->detach(this);
}

void WindowSurface::releaseDeviceObjects() {
    releaseAll();
    generation_ = 0;  // stale: the next beginFrame()/render() builds for the new device
}

std::expected<void, std::string> WindowSurface::build() {
    releaseAll();
    RenderDevice& d = *device_;
    HRESULT hr = d.d2dDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                    reinterpret_cast<ID2D1DeviceContext2**>(ctx_.GetAddressOf()));
    if (FAILED(hr)) return std::unexpected(hrError("CreateDeviceContext", hr));

    // A swap chain cannot be 0x0; a surface created minimised gets 1x1 buffers until its first real size.
    const int bw = std::max(1, w_), bh = std::max(1, h_);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = static_cast<UINT>(bw);
    sd.Height = static_cast<UINT>(bh);
    sd.Format = kFormat;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBufferCount;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = kSwapFlags;
    ComPtr<IDXGISwapChain1> sc1;
    hr = d.dxgi()->CreateSwapChainForHwnd(d.queue(), hwnd_, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) return std::unexpected(hrError("CreateSwapChainForHwnd", hr));
    hr = sc1.As(&swap_);
    if (FAILED(hr)) return std::unexpected(hrError("QueryInterface(IDXGISwapChain3)", hr));
    d.dxgi()->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);  // no DXGI full-screen toggle
    swap_->SetMaximumFrameLatency(1);
    waitable_ = swap_->GetFrameLatencyWaitableObject();
    built_w_ = bw;
    built_h_ = bh;
    if (auto r = wrapTargets(); !r) return r;
    generation_ = d.generation();
    return {};
}

std::expected<void, std::string> WindowSurface::wrapTargets() {
    RenderDevice& d = *device_;
    for (UINT i = 0; i < kBufferCount; ++i) {
        ComPtr<ID3D12Resource> buf;
        HRESULT hr = swap_->GetBuffer(i, IID_PPV_ARGS(&buf));
        if (FAILED(hr)) return std::unexpected(hrError("IDXGISwapChain::GetBuffer", hr));
        D3D11_RESOURCE_FLAGS rf{};
        rf.BindFlags = D3D11_BIND_RENDER_TARGET;
        // In = the state the buffer is in when D3D11On12 acquires it, out = the state it is left in on release. Only
        // D2D draws here (no D3D12 pass first, unlike Microsoft's 11on12 sample, which uses RENDER_TARGET in), so a
        // back buffer arrives from the swap chain in PRESENT and must go back in PRESENT. Declaring RENDER_TARGET
        // made every D2D clear run on a resource in the wrong state -- found by the debug-layer test.
        hr = d.on12()->CreateWrappedResource(buf.Get(), &rf, D3D12_RESOURCE_STATE_PRESENT,
                                             D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&wrapped_[i]));
        if (FAILED(hr)) return std::unexpected(hrError("CreateWrappedResource", hr));
        ComPtr<IDXGISurface> surf;
        hr = wrapped_[i].As(&surf);
        if (FAILED(hr)) return std::unexpected(hrError("QueryInterface(IDXGISurface)", hr));
        const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                   D2D1::PixelFormat(kFormat, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr = ctx_->CreateBitmapFromDxgiSurface(surf.Get(), &props, &targets_[i]);
        if (FAILED(hr)) return std::unexpected(hrError("CreateBitmapFromDxgiSurface", hr));
    }
    const auto rprops = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                D2D1::PixelFormat(kFormat, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = ctx_->CreateBitmap(D2D1::SizeU(built_w_, built_h_), nullptr, 0, rprops, &readback_);
    if (FAILED(hr)) return std::unexpected(hrError("CreateBitmap(readback)", hr));
    return {};
}

void WindowSurface::releaseTargets() {
    if (ctx_) ctx_->SetTarget(nullptr);
    readback_.Reset();
    for (auto& t : targets_) t.Reset();
    for (auto& w : wrapped_) w.Reset();
    // D3D11On12 destroys wrapped resources lazily; flushing drops its last references to the swap-chain buffers,
    // which ResizeBuffers requires.
    if (device_ && device_->d3d11Context()) device_->d3d11Context()->Flush();
}

void WindowSurface::releaseAll() {
    if (swap_ && device_) device_->waitIdle();
    releaseTargets();
    ctx_.Reset();
    if (waitable_) {
        CloseHandle(waitable_);
        waitable_ = nullptr;
    }
    swap_.Reset();
    in_frame_ = false;
}

std::expected<void, std::string> WindowSurface::resize(int w_px, int h_px) {
    w_ = std::max(0, w_px);
    h_ = std::max(0, h_px);
    if (w_ == 0 || h_ == 0) return {};  // minimised: keep the buffers, frames are skipped
    if (!swap_ || generation_ != device_->generation()) return build();
    if (w_ == built_w_ && h_ == built_h_) return {};
    device_->waitIdle();
    releaseTargets();
    HRESULT hr = swap_->ResizeBuffers(kBufferCount, static_cast<UINT>(w_), static_cast<UINT>(h_), kFormat, kSwapFlags);
    if (isLoss(hr) || device_->lost()) return {};  // the next frame reports Lost and render() recovers
    if (FAILED(hr)) return std::unexpected(hrError("ResizeBuffers", hr));
    built_w_ = w_;
    built_h_ = h_;
    return wrapTargets();
}

ID2D1DeviceContext2* WindowSurface::beginFrame() {
    error_.clear();
    if (in_frame_) {
        error_ = "beginFrame called twice without endFrame";
        return nullptr;
    }
    if (w_ == 0 || h_ == 0) return nullptr;
    if (device_->lost()) {  // lost between frames (e.g. during a resize): recover now; the rebuild follows below
        if (auto r = device_->recover(); !r) {
            error_ = "device recovery: " + r.error();
            return nullptr;
        }
    }
    if (!swap_ || generation_ != device_->generation() || w_ != built_w_ || h_ != built_h_ || !targets_[0]) {
        auto r = (!swap_ || generation_ != device_->generation()) ? build() : resize(w_, h_);
        if (!r) {
            error_ = r.error();
            return nullptr;
        }
        if (!targets_[0]) {  // a resize that ran into a lost device
            error_ = "the device was lost while resizing";
            return nullptr;
        }
    }
    if (waitable_) WaitForSingleObjectEx(waitable_, 1000, TRUE);
    frame_index_ = swap_->GetCurrentBackBufferIndex();
    ID3D11Resource* res = wrapped_[frame_index_].Get();
    device_->on12()->AcquireWrappedResources(&res, 1);
    ctx_->SetTarget(targets_[frame_index_].Get());
    ctx_->SetDpi(static_cast<float>(96.0 * dpi_scale_), static_cast<float>(96.0 * dpi_scale_));
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Grayscale, not ClearType: independent of the monitor's subpixel layout, so WARP goldens are reproducible.
    ctx_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ctx_->BeginDraw();
    in_frame_ = true;
    return ctx_.Get();
}

std::expected<void, std::string> WindowSurface::copyToImage(ID2D1Bitmap1* target, Image& out) {
    HRESULT hr = readback_->CopyFromBitmap(nullptr, target, nullptr);
    if (FAILED(hr)) return std::unexpected(hrError("CopyFromBitmap", hr));
    D2D1_MAPPED_RECT m{};
    hr = readback_->Map(D2D1_MAP_OPTIONS_READ, &m);
    if (FAILED(hr)) return std::unexpected(hrError("ID2D1Bitmap1::Map", hr));
    out.width = built_w_;
    out.height = built_h_;
    out.bgra.resize(static_cast<std::size_t>(built_w_) * built_h_ * 4);
    for (int y = 0; y < built_h_; ++y)
        std::memcpy(out.bgra.data() + static_cast<std::size_t>(y) * built_w_ * 4, m.bits + static_cast<std::size_t>(y) * m.pitch,
                    static_cast<std::size_t>(built_w_) * 4);
    readback_->Unmap();
    return {};
}

FrameStatus WindowSurface::endFrame(const FrameOptions& options) {
    if (!in_frame_) {
        error_ = "endFrame without beginFrame";
        return FrameStatus::Failed;
    }
    in_frame_ = false;
    HRESULT hr = ctx_->EndDraw();
    std::string capture_error;
    if (SUCCEEDED(hr) && options.capture) {
        if (auto r = copyToImage(targets_[frame_index_].Get(), *options.capture); !r) capture_error = r.error();
    }
    ctx_->SetTarget(nullptr);
    ID3D11Resource* res = wrapped_[frame_index_].Get();
    device_->on12()->ReleaseWrappedResources(&res, 1);
    device_->d3d11Context()->Flush();  // submits the D2D work to the D3D12 queue, ahead of Present
    if (isLoss(hr) || device_->lost()) return FrameStatus::Lost;
    if (FAILED(hr)) {
        error_ = hrError("EndDraw", hr);
        return FrameStatus::Failed;
    }
    if (!capture_error.empty()) {
        error_ = capture_error;
        return device_->lost() ? FrameStatus::Lost : FrameStatus::Failed;
    }
    if (options.present) {
        hr = swap_->Present(options.vsync ? 1 : 0, 0);  // DXGI_STATUS_OCCLUDED is a success code: nothing to do
        if (isLoss(hr) || device_->lost()) return FrameStatus::Lost;
        if (FAILED(hr)) {
            error_ = hrError("Present", hr);
            return FrameStatus::Failed;
        }
    }
    return FrameStatus::Presented;
}

FrameStatus WindowSurface::render(const std::function<void(ID2D1DeviceContext2*)>& draw, const FrameOptions& options) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        ID2D1DeviceContext2* ctx = beginFrame();
        if (!ctx) return error_.empty() ? FrameStatus::Skipped : FrameStatus::Failed;
        draw(ctx);
        const FrameStatus st = endFrame(options);
        if (st != FrameStatus::Lost) return st;
        if (auto r = device_->recover(); !r) {
            error_ = "device recovery: " + r.error();
            return FrameStatus::Failed;
        }
        if (auto r = build(); !r) {
            error_ = "surface rebuild after device loss: " + r.error();
            return FrameStatus::Failed;
        }
    }
    error_ = "the device was lost again right after recovery";
    return FrameStatus::Lost;
}

}  // namespace tcad::ui

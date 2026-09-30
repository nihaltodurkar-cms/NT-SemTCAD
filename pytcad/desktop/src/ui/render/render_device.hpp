// The process-wide render device of the native UI framework (N2a, NATIVE-DESKTOP-PLAN.md 27.7).
//
// Decision 27.7-1: Direct2D reaches the screen through D3D11On12. One ID3D12Device and DIRECT command queue per
// process; an ID3D11On12Device on that queue; an ID2D1Device on the D3D11On12 device. Each window then owns a
// DXGI flip-model swap chain on the queue (window_surface.hpp) whose back buffers D2D draws into as wrapped
// resources. The device-independent factories (DXGI, D2D, DirectWrite, WIC) outlive a device loss; the device
// objects do not: recover() rebuilds them and bumps generation(), and every surface rebuilds itself on its next
// frame when it sees a new generation.
//
// D3D12 hands out ONE device per adapter per process: D3D12CreateDevice returns the existing object while anything
// still references it -- including a removed one. So (a) create() returns the one live RenderDevice for the same
// adapter kind (WARP / hardware) instead of building a second wrapper around the same device, and (b) recover()
// first makes every attached surface drop its device objects, then releases its own, and only then creates the
// new device. Enabling the debug layer after a device exists removes that device, so the debug-layer choice is
// fixed by the first create() in the process (a conflicting later request is an error).
//
// COM must be initialised on the calling thread (WIC is created with CoCreateInstance). Single-threaded use.
#pragma once

#include <d2d1_3.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace tcad::ui {

using Microsoft::WRL::ComPtr;

// Something that holds device objects and must let go of them before the device is rebuilt (WindowSurface).
class DeviceClient {
public:
    virtual ~DeviceClient() = default;
    virtual void releaseDeviceObjects() = 0;
};

struct RenderOptions {
    bool warp = false;         // Microsoft's software adapter: deterministic, used for goldens (decision 27.7-3)
    bool debug_layer = false;  // the D3D12 + DXGI debug layers (Graphics Tools must be installed)
};

// "<what> failed (hr=0x...)" -- every error string of this layer is built with it.
std::string hrError(const char* what, HRESULT hr);

class RenderDevice {
public:
    static std::expected<std::shared_ptr<RenderDevice>, std::string> create(const RenderOptions& options = {});
    ~RenderDevice();
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    const RenderOptions& options() const { return options_; }
    const std::wstring& adapterName() const { return adapter_name_; }
    bool isSoftware() const { return software_; }

    ID3D12Device* d3d12() const { return d3d12_.Get(); }
    ID3D12CommandQueue* queue() const { return queue_.Get(); }
    ID3D11On12Device* on12() const { return on12_.Get(); }
    ID3D11DeviceContext* d3d11Context() const { return d3d11_context_.Get(); }
    ID2D1Device2* d2dDevice() const { return d2d_device_.Get(); }
    IDXGIFactory4* dxgi() const { return dxgi_.Get(); }
    ID2D1Factory3* d2dFactory() const { return d2d_factory_.Get(); }
    IDWriteFactory2* dwrite() const { return dwrite_.Get(); }
    IWICImagingFactory* wic() const { return wic_.Get(); }

    // Bumped by every successful recover(); a surface built for an older generation rebuilds itself.
    std::uint64_t generation() const { return generation_; }
    // The device-removed reason (S_OK while the device is healthy).
    HRESULT removedReason() const;
    bool lost() const { return removedReason() != S_OK; }
    // Rebuild the device objects if the device was removed (a no-op otherwise). Surfaces must have released
    // their swap chains first only in the sense that they rebuild lazily -- the old objects die with them.
    std::expected<void, std::string> recover();
    // Block until the queue has executed everything submitted so far (resize, teardown).
    void waitIdle();
    // Test hook: remove the device now (ID3D12Device5::RemoveDevice), as a driver reset would.
    bool simulateLoss();

    void attach(DeviceClient* c);
    void detach(DeviceClient* c);

private:
    RenderDevice() = default;
    std::expected<void, std::string> buildFactories();
    std::expected<void, std::string> buildDevice();
    void releaseDevice();

    RenderOptions options_;
    std::vector<DeviceClient*> clients_;
    std::wstring adapter_name_;
    bool software_ = false;
    std::uint64_t generation_ = 1;

    ComPtr<IDXGIFactory4> dxgi_;
    ComPtr<ID2D1Factory3> d2d_factory_;
    ComPtr<IDWriteFactory2> dwrite_;
    ComPtr<IWICImagingFactory> wic_;

    ComPtr<ID3D12Device> d3d12_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    std::uint64_t fence_value_ = 0;
    HANDLE fence_event_ = nullptr;
    ComPtr<ID3D11Device> d3d11_;
    ComPtr<ID3D11DeviceContext> d3d11_context_;
    ComPtr<ID3D11On12Device> on12_;
    ComPtr<ID2D1Device2> d2d_device_;
};

}  // namespace tcad::ui

#include "ui/render/render_device.hpp"

#include <cstdio>

namespace tcad::ui {

std::string hrError(const char* what, HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(what) + " failed (hr=" + buf + ")";
}

namespace {

// The live devices, one per adapter kind (index 0 hardware, 1 WARP), and the process's debug-layer choice.
std::weak_ptr<RenderDevice> g_live[2];
int g_debug_choice = -1;  // -1: no device created yet

}  // namespace

std::expected<std::shared_ptr<RenderDevice>, std::string> RenderDevice::create(const RenderOptions& options) {
    if (g_debug_choice >= 0 && g_debug_choice != static_cast<int>(options.debug_layer))
        return std::unexpected(std::string("the D3D12 debug layer is ") + (g_debug_choice ? "on" : "off") +
                               " for this process (fixed by its first device); enabling it later would remove "
                               "the existing devices");
    auto& slot = g_live[options.warp ? 1 : 0];
    if (auto live = slot.lock()) return live;  // D3D12 would hand back the same device anyway
    std::shared_ptr<RenderDevice> d(new RenderDevice());
    d->options_ = options;
    if (auto r = d->buildFactories(); !r) return std::unexpected(r.error());
    g_debug_choice = static_cast<int>(options.debug_layer);
    if (auto r = d->buildDevice(); !r) return std::unexpected(r.error());
    slot = d;
    return d;
}

void RenderDevice::attach(DeviceClient* c) { clients_.push_back(c); }

void RenderDevice::detach(DeviceClient* c) { std::erase(clients_, c); }

RenderDevice::~RenderDevice() {
    if (queue_ && fence_) waitIdle();
    releaseDevice();
}

std::expected<void, std::string> RenderDevice::buildFactories() {
    HRESULT hr;
    if (options_.debug_layer) {
        ComPtr<ID3D12Debug> dbg;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&dbg));
        if (FAILED(hr)) return std::unexpected(hrError("D3D12GetDebugInterface (is Graphics Tools installed?)", hr));
        dbg->EnableDebugLayer();
    }
    hr = CreateDXGIFactory2(options_.debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&dxgi_));
    if (FAILED(hr)) return std::unexpected(hrError("CreateDXGIFactory2", hr));
    D2D1_FACTORY_OPTIONS fo{};
    fo.debugLevel = D2D1_DEBUG_LEVEL_NONE;  // the D2D debug layer is a separate install; D3D12/DXGI's are used
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &fo,
                           reinterpret_cast<void**>(d2d_factory_.GetAddressOf()));
    if (FAILED(hr)) return std::unexpected(hrError("D2D1CreateFactory", hr));
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (FAILED(hr)) return std::unexpected(hrError("DWriteCreateFactory", hr));
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    if (FAILED(hr)) return std::unexpected(hrError("CoCreateInstance(WICImagingFactory) (is COM initialised?)", hr));
    return {};
}

std::expected<void, std::string> RenderDevice::buildDevice() {
    HRESULT hr;
    ComPtr<IDXGIAdapter1> adapter;
    if (options_.warp) {
        hr = dxgi_->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if (FAILED(hr)) return std::unexpected(hrError("EnumWarpAdapter", hr));
    } else {
        ComPtr<IDXGIFactory6> f6;
        if (SUCCEEDED(dxgi_.As(&f6))) {
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIAdapter1> a;
                if (f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) ==
                    DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_ADAPTER_DESC1 ad{};
                a->GetDesc1(&ad);
                if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                    adapter = a;
                    break;
                }
            }
        }
        if (!adapter) {  // no hardware adapter that can do D3D12: fall back to WARP, and say so via isSoftware()
            hr = dxgi_->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
            if (FAILED(hr)) return std::unexpected(hrError("EnumWarpAdapter (no D3D12 hardware adapter)", hr));
        }
    }
    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    adapter_name_ = ad.Description;
    software_ = (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12_));
    if (FAILED(hr)) return std::unexpected(hrError("D3D12CreateDevice", hr));
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = d3d12_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_));
    if (FAILED(hr)) return std::unexpected(hrError("CreateCommandQueue", hr));
    hr = d3d12_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) return std::unexpected(hrError("CreateFence", hr));
    fence_value_ = 0;
    if (!fence_event_) fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) return std::unexpected(hrError("CreateEvent", HRESULT_FROM_WIN32(GetLastError())));

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // required by Direct2D
    if (options_.debug_layer) flags |= D3D11_CREATE_DEVICE_DEBUG;
    IUnknown* queues[] = {queue_.Get()};
    hr = D3D11On12CreateDevice(d3d12_.Get(), flags, nullptr, 0, queues, 1, 0, &d3d11_, &d3d11_context_, nullptr);
    if (FAILED(hr)) return std::unexpected(hrError("D3D11On12CreateDevice", hr));
    hr = d3d11_.As(&on12_);
    if (FAILED(hr)) return std::unexpected(hrError("QueryInterface(ID3D11On12Device)", hr));
    ComPtr<IDXGIDevice> dxgi_device;
    hr = d3d11_.As(&dxgi_device);
    if (FAILED(hr)) return std::unexpected(hrError("QueryInterface(IDXGIDevice)", hr));
    hr = d2d_factory_->CreateDevice(dxgi_device.Get(), reinterpret_cast<ID2D1Device2**>(d2d_device_.GetAddressOf()));
    if (FAILED(hr)) return std::unexpected(hrError("ID2D1Factory3::CreateDevice", hr));
    return {};
}

void RenderDevice::releaseDevice() {
    d2d_device_.Reset();
    if (d3d11_context_) {
        d3d11_context_->ClearState();
        d3d11_context_->Flush();
    }
    on12_.Reset();
    d3d11_context_.Reset();
    d3d11_.Reset();
    fence_.Reset();
    queue_.Reset();
    d3d12_.Reset();
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
}

HRESULT RenderDevice::removedReason() const {
    return d3d12_ ? d3d12_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
}

std::expected<void, std::string> RenderDevice::recover() {
    if (!lost()) return {};
    // Every reference to the removed device must be gone before D3D12CreateDevice, or it returns the same one.
    for (DeviceClient* c : std::vector<DeviceClient*>(clients_)) c->releaseDeviceObjects();
    releaseDevice();  // no waitIdle: a removed device never signals its fence
    if (auto r = buildDevice(); !r) return r;
    ++generation_;
    return {};
}

void RenderDevice::waitIdle() {
    if (!queue_ || !fence_ || lost()) return;
    const std::uint64_t v = ++fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), v))) return;
    if (fence_->GetCompletedValue() < v && SUCCEEDED(fence_->SetEventOnCompletion(v, fence_event_)))
        WaitForSingleObject(fence_event_, 5000);
}

bool RenderDevice::simulateLoss() {
    ComPtr<ID3D12Device5> d5;
    if (!d3d12_ || FAILED(d3d12_.As(&d5))) return false;
    d5->RemoveDevice();
    return lost();
}

}  // namespace tcad::ui

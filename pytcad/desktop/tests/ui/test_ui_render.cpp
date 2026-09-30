// The N2a gate (NATIVE-DESKTOP-PLAN.md 27.7): the render core -- D3D12 device, D3D11On12, Direct2D/DirectWrite into
// a flip-model swap chain, readback to PNG -- on WARP (decision 27.7-3), with goldens at 100/150/200%, device-loss
// recovery, resize/DPI churn without leaks, a clean D3D12/DXGI debug layer, and a smoke check on the real GPU.
//
//   tcad_ui_render_tests [test name] [--goldens <dir>] [--capture]
//
// Goldens are pinned to the machine's WARP/D2D/DirectWrite/Segoe UI versions (goldens/manifest.json). On another
// environment they are reported STALE with the re-capture command, not failed: pixels from another rasterizer
// build are not a regression (the CLAUDE.md machine-specific-golden rule, applied to images).
#include "mini_test.hpp"
#include "render_scene.hpp"
#include "render_test_support.hpp"

#include "ui/render/image.hpp"
#include "ui/render/render_device.hpp"
#include "ui/render/window_surface.hpp"

#include <dxgidebug.h>
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;

namespace {

// ERROR/CORRUPTION messages the D3D12 debug layer stored for this device (printed); -1 without an info queue.
int d3d12Errors(ID3D12Device* dev, const char* when) {
    ComPtr<ID3D12InfoQueue> q;
    if (!dev || FAILED(dev->QueryInterface(IID_PPV_ARGS(&q)))) {
        std::printf("  no ID3D12InfoQueue (%s)\n", when);
        return -1;
    }
    int errors = 0;
    const UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        q->GetMessage(i, nullptr, &len);
        std::vector<char> buf(len);
        auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(q->GetMessage(i, m, &len))) continue;
        if (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR || m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
            ++errors;
            std::printf("  D3D12 (%s): %s\n", when, m->pDescription);
        }
    }
    std::printf("  D3D12 info queue %s: %llu message(s), %d error(s)\n", when, static_cast<unsigned long long>(n), errors);
    return errors;
}

// Render the sample scene at `scale` into a fresh surface on `dev`; the captured frame, or an empty image.
Image renderScene(const std::shared_ptr<RenderDevice>& dev, HWND hwnd, double scale, FrameStatus* status = nullptr) {
    Image img;
    auto s = WindowSurface::create(dev, hwnd, px(kSceneWidthDip, scale), px(kSceneHeightDip, scale), scale);
    if (!s) {
        std::printf("  WindowSurface::create: %s\n", s.error().c_str());
        return img;
    }
    const FrameStatus st = (*s)->render([&](ID2D1DeviceContext2* ctx) { drawScene(ctx, *dev); },
                                       {.vsync = false, .capture = &img});
    if (st != FrameStatus::Presented) std::printf("  render: status %d, %s\n", static_cast<int>(st), (*s)->lastError().c_str());
    if (status) *status = st;
    return img;
}

}  // namespace

TEST(warp_device_is_the_software_adapter) {
    auto& d = warp();
    CHECK(d != nullptr);
    if (!d) return;
    CHECK(d->isSoftware());
    std::printf("  adapter: %s\n", narrowW(d->adapterName()).c_str());
    CHECK(d->generation() == 1);
    CHECK(!d->lost());
}

TEST(clear_color_reads_back_exactly) {
    auto& d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    auto s = WindowSurface::create(d, win->hwnd(), 64, 48);
    CHECK(s.has_value());
    if (!s) return;
    Image img;
    // 0x33/0x66/0xCC are exact in [0,1] (0.2, 0.4, 0.8): no rounding question
    const auto st = (*s)->render([](ID2D1DeviceContext2* ctx) { ctx->Clear(D2D1::ColorF(0x3366CC, 1.0f)); },
                                 {.vsync = false, .capture = &img});
    CHECK(st == FrameStatus::Presented);
    CHECK_EQ(img.width, 64);
    CHECK_EQ(img.height, 48);
    bool all = !img.empty();
    for (int y = 0; y < img.height && all; ++y)
        for (int x = 0; x < img.width && all; ++x) all = img.pixel(x, y) == 0xFF3366CCu;
    CHECK(all);
}

TEST(scene_matches_the_goldens_at_three_scales) {
    auto& d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    for (int pct : {100, 150, 200}) {
        const double scale = pct / 100.0;
        const Image img = renderScene(d, win->hwnd(), scale);
        CHECK_EQ(img.width, px(kSceneWidthDip, scale));
        CHECK_EQ(img.height, px(kSceneHeightDip, scale));
        const GoldenResult r = checkGolden(*d, "n2a_scene@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);  // decision 27.7-3: tolerance 0 on WARP
    }
}

TEST(rendering_is_deterministic_and_scales_with_dpi) {
    auto& d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    const Image a = renderScene(d, win->hwnd(), 1.0), b = renderScene(d, win->hwnd(), 1.0);
    CHECK(!a.empty() && a == b);
    const Image c = renderScene(d, win->hwnd(), 2.0);
    // The same scene in DIPs: at 200% four times the pixels are covered (antialiased edges make it inexact)
    const double ratio = static_cast<double>(nonBackgroundPixels(c)) / static_cast<double>(nonBackgroundPixels(a));
    std::printf("  covered pixels 100%%: %zu, 200%%: %zu, ratio %.3f\n", nonBackgroundPixels(a), nonBackgroundPixels(c), ratio);
    CHECK(ratio > 3.6 && ratio < 4.4);
}

TEST(resize_and_minimise_follow_the_requested_size) {
    auto& d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    auto s = WindowSurface::create(d, win->hwnd(), 200, 100);
    CHECK(s.has_value());
    if (!s) return;
    auto clear = [](ID2D1DeviceContext2* ctx) { ctx->Clear(D2D1::ColorF(0x336699, 1.0f)); };
    for (auto [w, h] : {std::pair{123, 77}, {640, 400}, {1, 1}, {333, 222}}) {
        CHECK((*s)->resize(w, h).has_value());
        Image img;
        CHECK((*s)->render(clear, {.vsync = false, .capture = &img}) == FrameStatus::Presented);
        CHECK(img.width == w && img.height == h);
        CHECK(!img.empty() && img.pixel(w - 1, h - 1) == 0xFF336699u);
    }
    CHECK((*s)->resize(0, 0).has_value());  // minimised
    CHECK((*s)->render(clear, {.vsync = false}) == FrameStatus::Skipped);
    CHECK((*s)->lastError().empty());
    CHECK((*s)->resize(50, 40).has_value());
    Image img;
    CHECK((*s)->render(clear, {.vsync = false, .capture = &img}) == FrameStatus::Presented);
    CHECK(img.width == 50 && img.height == 40);
}

TEST(device_loss_is_recovered_and_the_frame_repainted) {
    // The shared WARP device: D3D12 has one device per adapter per process, so there is no "own" device to lose
    // (RenderDevice::create returns the live one). Every surface attached to it is rebuilt by the recovery.
    auto d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    auto s = WindowSurface::create(d, win->hwnd(), 320, 200);
    CHECK(s.has_value());
    if (!s) return;
    auto draw = [&](ID2D1DeviceContext2* ctx) { drawScene(ctx, *d); };
    Image before, after;
    CHECK((*s)->render(draw, {.vsync = false, .capture = &before}) == FrameStatus::Presented);
    CHECK(d->simulateLoss());
    CHECK(d->lost());
    const FrameStatus st = (*s)->render(draw, {.vsync = false, .capture = &after});
    std::printf("  after RemoveDevice: status %d, generation %llu, %s\n", static_cast<int>(st),
                static_cast<unsigned long long>(d->generation()), (*s)->lastError().c_str());
    CHECK(st == FrameStatus::Presented);
    CHECK(!d->lost());
    const std::uint64_t gen = d->generation();
    CHECK(gen >= 2);
    CHECK_EQ((*s)->builtForGeneration(), gen);
    CHECK(!after.empty() && after == before);  // the same frame, on the rebuilt device
    // a loss in the middle of a resize is recovered on the next frame too
    CHECK(d->simulateLoss());
    CHECK((*s)->resize(200, 120).has_value());
    Image third;
    CHECK((*s)->render(draw, {.vsync = false, .capture = &third}) == FrameStatus::Presented);
    CHECK(third.width == 200 && third.height == 120 && d->generation() == gen + 1);
}

TEST(resize_and_dpi_churn_does_not_leak) {
    auto& d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    auto s = WindowSurface::create(d, win->hwnd(), 320, 200);
    CHECK(s.has_value());
    if (!s) return;
    auto draw = [&](ID2D1DeviceContext2* ctx) { drawScene(ctx, *d); };
    auto cycle = [&](int i) {
        const double scale = 1.0 + 0.25 * (i % 5);  // 100..200%
        (*s)->setDpiScale(scale);
        const bool ok = (*s)->resize(px(160 + (i * 37) % 480, scale), px(100 + (i * 53) % 300, scale)).has_value();
        return ok && (*s)->render(draw, {.vsync = false}) == FrameStatus::Presented;
    };
    for (int i = 0; i < 50; ++i) cycle(i);  // warm-up: caches, WARP's allocator
    PROCESS_MEMORY_COUNTERS_EX m0{}, m1{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m0), sizeof m0);
    DWORD h0 = 0, h1 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h0);
    const DWORD gdi0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    int failures = 0;
    for (int i = 0; i < 1000; ++i) failures += !cycle(i);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m1), sizeof m1);
    GetProcessHandleCount(GetCurrentProcess(), &h1);
    const DWORD gdi1 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const double grow_mb = (static_cast<double>(m1.PrivateUsage) - static_cast<double>(m0.PrivateUsage)) / (1024.0 * 1024.0);
    std::printf("  1000 resize+DPI frames: %d failed; private bytes %+.1f MB; handles %lu -> %lu; GDI %lu -> %lu\n",
                failures, grow_mb, h0, h1, gdi0, gdi1);
    CHECK_EQ(failures, 0);
    CHECK(grow_mb < 16.0);
    CHECK(h1 <= h0 + 32);
    CHECK(gdi1 <= gdi0 + 8);
}

TEST(debug_layer_reports_no_errors_and_nothing_outlives_the_device) {
    if (!g_debug) {
        // The layer must be on before the process's first device (enabling it later removes existing devices):
        // run this test again in a child process that enables it from the start.
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" debug_layer_reports_no_errors_and_nothing_outlives_the_device --debug-layer";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi));
        if (!pi.hProcess) return;
        WaitForSingleObject(pi.hProcess, 120000);
        DWORD code = 99;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::printf("  child process with --debug-layer exited %lu\n", code);
        CHECK_EQ(code, DWORD{0});
        return;
    }
    ComPtr<IDXGIDebug1> dxgi_debug;
    ComPtr<IDXGIInfoQueue> info;
    if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))) || FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&info)))) {
        std::printf("  SKIP: the DXGI debug layer is not installed (Settings > Optional features > Graphics Tools)\n");
        return;
    }
    info->ClearStoredMessages(DXGI_DEBUG_ALL);
    {
        // A device owned by this scope (not the static warp()), so that after the scope nothing may be alive.
        std::printf("  [step] device\n");
        auto d = makeDevice({.warp = true, .debug_layer = true});
        CHECK(d != nullptr);
        if (!d) return;
        std::printf("  [step] window\n");
        auto win = hiddenWindow();
        CHECK(win != nullptr);
        if (!win) return;
        std::printf("  [step] surface\n");
        auto s = WindowSurface::create(d, win->hwnd(), 320, 200);
        CHECK(s.has_value());
        if (!s) return;
        auto draw = [&](ID2D1DeviceContext2* ctx) { drawScene(ctx, *d); };
        Image img;
        for (int i = 0; i < 20; ++i) {
            std::printf("  [step] frame %d\n", i);
            CHECK((*s)->resize(200 + 13 * i, 120 + 7 * i).has_value());
            CHECK((*s)->render(draw, {.vsync = false, .capture = i % 5 ? nullptr : &img}) == FrameStatus::Presented);
        }
        CHECK_EQ(d3d12Errors(d->d3d12(), "before the loss"), 0);
        std::printf("  [step] loss\n");
        CHECK(d->simulateLoss());
        CHECK((*s)->render(draw, {.vsync = false}) == FrameStatus::Presented);
        std::printf("  [step] resize after recovery\n");
        CHECK((*s)->resize(300, 180).has_value());
        CHECK((*s)->render(draw, {.vsync = false, .capture = &img}) == FrameStatus::Presented);
        CHECK_EQ(d3d12Errors(d->d3d12(), "after recovery"), 0);
        std::printf("  [step] teardown\n");
    }  // surface, window and device released here
    const UINT64 n = info->GetNumStoredMessages(DXGI_DEBUG_ALL);
    int errors = 0;
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        info->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &len);
        std::vector<char> buf(len);
        auto* msg = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buf.data());
        if (FAILED(info->GetMessage(DXGI_DEBUG_ALL, i, msg, &len))) continue;
        const bool bad = msg->Severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR ||
                         msg->Severity == DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION;
        // the RemoveDevice test hook itself is reported as an error by the layer: expected, not a defect
        const std::string text(msg->pDescription, msg->DescriptionByteLength ? msg->DescriptionByteLength - 1 : 0);
        const bool expected = text.find("RemoveDevice") != std::string::npos || text.find("DEVICE_REMOVED") != std::string::npos ||
                              text.find("device removed") != std::string::npos || text.find("Device removed") != std::string::npos;
        if (bad && !expected) {
            ++errors;
            std::printf("  debug layer: %s\n", text.c_str());
        } else if (bad) {
            std::printf("  (expected, from the loss test hook) %s\n", text.substr(0, 160).c_str());
        }
    }
    CHECK_EQ(errors, 0);
    // Live objects: report and count the messages that name a live D3D12/D3D11/DXGI object (the DXGI factory
    // the debug interface itself holds is internal and ignored by the flag).
    info->ClearStoredMessages(DXGI_DEBUG_ALL);
    dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    const UINT64 live = info->GetNumStoredMessages(DXGI_DEBUG_ALL);
    int live_objects = 0;
    for (UINT64 i = 0; i < live; ++i) {
        SIZE_T len = 0;
        info->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &len);
        std::vector<char> buf(len);
        auto* msg = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buf.data());
        if (FAILED(info->GetMessage(DXGI_DEBUG_ALL, i, msg, &len))) continue;
        const std::string text(msg->pDescription, msg->DescriptionByteLength ? msg->DescriptionByteLength - 1 : 0);
        if (text.find("Live ") != std::string::npos && text.find("Refcount: 0") == std::string::npos &&
            text.find("Summary") == std::string::npos) {
            ++live_objects;
            std::printf("  live: %s\n", text.substr(0, 200).c_str());
        }
    }
    std::printf("  %llu live-object report line(s), %d naming a live object\n", static_cast<unsigned long long>(live), live_objects);
    CHECK_EQ(live_objects, 0);
}

TEST(gpu_smoke_draws_the_scene_on_the_hardware_adapter) {
    auto d = makeDevice({.debug_layer = g_debug});
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    if (d->isSoftware()) {
        std::printf("  SKIP: no D3D12 hardware adapter (%s)\n", narrowW(d->adapterName()).c_str());
        return;
    }
    std::printf("  adapter: %s\n", narrowW(d->adapterName()).c_str());
    const Image gpu = renderScene(d, win->hwnd(), 1.5);
    const Image cpu = renderScene(warp(), win->hwnd(), 1.5);
    CHECK(gpu.width == cpu.width && gpu.height == cpu.height);
    const std::size_t g = nonBackgroundPixels(gpu), c = nonBackgroundPixels(cpu);
    const ImageDiff diff = compareImages(gpu, cpu);
    // Not a pixel compare (decision 27.7-3): the same shapes in the same places, within antialiasing.
    std::printf("  covered pixels GPU %zu, WARP %zu; %zu pixels differ from WARP (max delta %d)\n", g, c,
                diff.differing_pixels, diff.max_channel_delta);
    CHECK(g > 10000 && std::abs(static_cast<double>(g) - static_cast<double>(c)) < 0.02 * static_cast<double>(c));
}

namespace {

// A crash prints where it happened -- the module and offset of every frame -- so an intermittent fault in a child
// process can be placed (in our code, or in D3D12 / the debug layer / WARP) without a debugger on the machine.
LONG WINAPI reportCrash(EXCEPTION_POINTERS* ep) {
    auto where = [](DWORD64 addr) {
        HMODULE m = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(addr), &m) && m)
            GetModuleFileNameA(m, name, MAX_PATH);
        const char* base = std::strrchr(name, '\\');
        std::printf("    %s+0x%llx\n", base ? base + 1 : name,
                    static_cast<unsigned long long>(addr - reinterpret_cast<DWORD64>(m)));
    };
    std::printf("CRASH: exception 0x%08lx at\n", ep->ExceptionRecord->ExceptionCode);
    where(reinterpret_cast<DWORD64>(ep->ExceptionRecord->ExceptionAddress));
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 f{};
    f.AddrPC.Offset = ctx.Rip;
    f.AddrPC.Mode = AddrModeFlat;
    f.AddrStack.Offset = ctx.Rsp;
    f.AddrStack.Mode = AddrModeFlat;
    f.AddrFrame.Offset = ctx.Rbp;
    f.AddrFrame.Mode = AddrModeFlat;
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    std::printf("  stack:\n");
    for (int i = 0; i < 24 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &f, &ctx, nullptr,
                                          SymFunctionTableAccess64, SymGetModuleBase64, nullptr) && f.AddrPC.Offset; ++i)
        where(f.AddrPC.Offset);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash must not swallow what was already printed
    SetUnhandledExceptionFilter(reportCrash);
    std::vector<char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--goldens" && i + 1 < argc) g_goldens = argv[++i];
        else if (a == "--capture") g_capture = true;
        else if (a == "--debug-layer") g_debug = true;
        else rest.push_back(argv[i]);
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("CoInitializeEx failed\n");
        return 2;
    }
    const int rc = minitest::runAll(static_cast<int>(rest.size()), rest.data());
    warp().reset();
    CoUninitialize();
    return rc;
}

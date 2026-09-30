#include "render_test_support.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#ifndef TCAD_UI_GOLDENS_DIR
#define TCAD_UI_GOLDENS_DIR "goldens"
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace tcad::ui::testing {

fs::path g_goldens = TCAD_UI_GOLDENS_DIR;
bool g_capture = false;
bool g_debug = false;

std::string narrowW(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

namespace {

std::string fileVersion(const wchar_t* dll) {
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    std::wstring p = std::wstring(path) + L"\\" + dll;
    DWORD h = 0;
    const DWORD n = GetFileVersionInfoSizeW(p.c_str(), &h);
    if (!n) return "?";
    std::vector<unsigned char> buf(n);
    if (!GetFileVersionInfoW(p.c_str(), 0, n, buf.data())) return "?";
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) return "?";
    char s[64];
    std::snprintf(s, sizeof s, "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
                  HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    return s;
}

std::string fontVersion(RenderDevice& d, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fc;
    if (FAILED(d.dwrite()->GetSystemFontCollection(&fc))) return "?";
    UINT32 idx = 0;
    BOOL exists = FALSE;
    if (FAILED(fc->FindFamilyName(family, &idx, &exists)) || !exists) return "absent";
    ComPtr<IDWriteFontFamily> fam;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteLocalizedStrings> strs;
    BOOL has = FALSE;
    if (FAILED(fc->GetFontFamily(idx, &fam)) ||
        FAILED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                         &font)) ||
        FAILED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_VERSION_STRINGS, &strs, &has)) || !has)
        return "?";
    UINT32 len = 0;
    strs->GetStringLength(0, &len);
    std::wstring w(len + 1, L'\0');
    strs->GetString(0, w.data(), len + 1);
    w.resize(len);
    return narrowW(w);
}

}  // namespace

// The system text rendering parameters Direct2D uses by default: gamma and contrast come from the user's ClearType
// Tuner settings (per user, per monitor), so they are part of what a text golden depends on.
std::string textRenderingParams(RenderDevice& d) {
    ComPtr<IDWriteRenderingParams> p;
    if (FAILED(d.dwrite()->CreateRenderingParams(&p))) return "?";
    char s[128];
    std::snprintf(s, sizeof s, "gamma %.3f, contrast %.3f, cleartype %.3f, mode %d", p->GetGamma(), p->GetEnhancedContrast(),
                  p->GetClearTypeLevel(), static_cast<int>(p->GetRenderingMode()));
    return s;
}

json environment(RenderDevice& d) {
    return {{"text rendering params", textRenderingParams(d)},
            {"d3d10warp.dll", fileVersion(L"d3d10warp.dll")},
            {"d2d1.dll", fileVersion(L"d2d1.dll")},
            {"DWrite.dll", fileVersion(L"DWrite.dll")},
            {"Segoe UI", fontVersion(d, L"Segoe UI")},
            // N2d: the monospace font and the system fallback fonts complex scripts and emoji come from
            {"Consolas", fontVersion(d, L"Consolas")},
            {"Nirmala UI", fontVersion(d, L"Nirmala UI")},
            {"Microsoft YaHei", fontVersion(d, L"Microsoft YaHei")},
            {"Yu Gothic UI", fontVersion(d, L"Yu Gothic UI")},
            {"Segoe UI Emoji", fontVersion(d, L"Segoe UI Emoji")}};
}

std::shared_ptr<RenderDevice> makeDevice(const RenderOptions& o) {
    auto d = RenderDevice::create(o);
    if (!d) {
        std::printf("  RenderDevice::create: %s\n", d.error().c_str());
        return nullptr;
    }
    return *d;
}

std::shared_ptr<RenderDevice>& warp() {
    static std::shared_ptr<RenderDevice> d = makeDevice({.warp = true, .debug_layer = g_debug});
    return d;
}

std::unique_ptr<tcad::platform::Window> hiddenWindow() {
    auto w = tcad::platform::Window::create({.title = L"tcad_ui_render_tests", .width = 320, .height = 200});
    if (!w) {
        std::printf("  Window::create: %s\n", w.error().c_str());
        return nullptr;
    }
    return std::move(*w);
}

int px(double dip, double scale) { return static_cast<int>(std::lround(dip * scale)); }

GoldenResult checkGolden(RenderDevice& d, const std::string& name, const Image& img) {
    const fs::path manifest_path = g_goldens / "manifest.json";
    json manifest = json::object();
    if (std::ifstream in(manifest_path); in) in >> manifest;
    const json env = environment(d);
    if (g_capture) {
        if (auto r = savePng(d.wic(), img, g_goldens / name); !r) {
            std::printf("  %s\n", r.error().c_str());
            return GoldenResult::Missing;
        }
        if (manifest.value("environment", json()) != env) manifest["images"] = json::object();  // a new baseline
        manifest["environment"] = env;
        manifest["images"][name] = {{"width", img.width}, {"height", img.height}};
        std::ofstream(manifest_path, std::ios::binary) << manifest.dump(2) << "\n";
        std::printf("  CAPTURED %s\n", (g_goldens / name).string().c_str());
        return GoldenResult::Captured;
    }
    if (!manifest.contains("images") || !manifest["images"].contains(name)) {
        std::printf("  %s: no golden -- capture with --capture\n", name.c_str());
        return GoldenResult::Missing;
    }
    if (manifest["environment"] != env) {
        std::printf("  STALE goldens: captured on %s, this machine is %s -- re-capture with tcad_ui_render_tests --capture\n",
                    manifest["environment"].dump().c_str(), env.dump().c_str());
        return GoldenResult::Stale;
    }
    auto golden = loadPng(d.wic(), g_goldens / name);
    if (!golden) {
        std::printf("  %s\n", golden.error().c_str());
        return GoldenResult::Missing;
    }
    const ImageDiff diff = compareImages(img, *golden);
    std::printf("  %s: %zu differing pixels (max channel delta %d)%s\n", name.c_str(), diff.differing_pixels,
                diff.max_channel_delta, diff.same_size ? "" : " -- DIFFERENT SIZE");
    if (diff.same_size && diff.differing_pixels == 0) return GoldenResult::Match;
    const fs::path actual = fs::temp_directory_path() / ("actual_" + name);
    if (savePng(d.wic(), img, actual)) std::printf("  actual frame saved: %s\n", actual.string().c_str());
    return GoldenResult::Mismatch;
}

}  // namespace tcad::ui::testing

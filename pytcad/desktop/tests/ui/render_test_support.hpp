// Shared by the tcad_ui_render_tests sources (N2a render core, N2b widget tree): the WARP device, hidden windows,
// and the golden-image check (decision 27.7-3: WARP, exact; STALE, not failed, on another WARP/D2D/DWrite/font build).
#pragma once

#include "platform/window.hpp"
#include "ui/render/image.hpp"
#include "ui/render/render_device.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace tcad::ui::testing {

extern std::filesystem::path g_goldens;  // --goldens <dir> (default: desktop/tests/ui/goldens)
extern bool g_capture;                   // --capture: write the goldens instead of comparing
extern bool g_debug;                     // --debug-layer: every device in this process has the debug layers

std::string narrowW(const std::wstring& w);
nlohmann::json environment(RenderDevice& d);  // what golden pixels depend on besides this repository
std::shared_ptr<RenderDevice> makeDevice(const RenderOptions& o);
std::shared_ptr<RenderDevice>& warp();        // the process's WARP device
std::unique_ptr<tcad::platform::Window> hiddenWindow();
int px(double dip, double scale);             // round to nearest

enum class GoldenResult { Match, Captured, Stale, Mismatch, Missing };
// Compare `img` with the golden `name` (or, with --capture, write it and record it in manifest.json). Prints one
// line; Mismatch and Missing are failures (the actual frame is saved to %TEMP%\actual_<name>).
GoldenResult checkGolden(RenderDevice& d, const std::string& name, const Image& img);

}  // namespace tcad::ui::testing

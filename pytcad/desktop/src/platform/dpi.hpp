// DPI and workspace layout arithmetic (NATIVE-DESKTOP-PLAN.md section 27.5, N1). Pure: no Win32, so it is
// unit-tested on any platform. Convention (section 27.4's manifest: per-monitor-v2): every pixel the OS reports
// is a DEVICE pixel; the application's own sizes are LOGICAL pixels (96 dpi) and are scaled here.
#pragma once

#include <cmath>

namespace tcad::platform {

constexpr double kBaseDpi = 96.0;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;  // device pixels
    bool operator==(const Rect&) const = default;
    bool empty() const { return w <= 0 || h <= 0; }
};

// dpi / 96; 1.0 for an unknown (0) dpi.
inline double scaleFromDpi(unsigned dpi) { return dpi ? static_cast<double>(dpi) / kBaseDpi : 1.0; }
// Round to nearest, halves away from zero (std::lround): 24 logical px at 1.25 -> 30, at 1.5 -> 36.
inline int toDevice(double logical, double scale) { return static_cast<int>(std::lround(logical * scale)); }
inline double toLogical(int device, double scale) { return scale > 0 ? device / scale : device; }

// The main window's client area: the content region (where the hosted views live) and the status strip
// along the bottom edge.
struct WorkspaceLayout {
    Rect client, content, status;
    bool operator==(const WorkspaceLayout&) const = default;
};
WorkspaceLayout computeLayout(int client_w_px, int client_h_px, double scale, double status_height_logical);

}  // namespace tcad::platform

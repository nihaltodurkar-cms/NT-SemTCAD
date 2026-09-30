// Geometry and colour value types of the native UI framework (N2b, NATIVE-DESKTOP-PLAN.md 27.7). Portable: no
// Win32. Two coordinate spaces, never mixed implicitly:
//   * DEVICE PIXELS (integers: SizeI, RectI) -- widget geometry and layout results, so edges land on pixels;
//   * DIPs (floats: SizeF, RectF, PointF)   -- size hints, style metrics and all painting, 1 DIP = 1/96 inch.
// px = DIP x scale, where scale = window DPI / 96 (1.0, 1.25, 1.5, 2.0, ...).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace tcad::ui {

struct SizeF {
    float width = 0, height = 0;
    bool operator==(const SizeF&) const = default;
};

struct PointF {
    float x = 0, y = 0;
    bool operator==(const PointF&) const = default;
};

struct RectF {
    float x = 0, y = 0, width = 0, height = 0;
    float right() const { return x + width; }
    float bottom() const { return y + height; }
    bool operator==(const RectF&) const = default;
};

struct SizeI {
    int width = 0, height = 0;
    bool operator==(const SizeI&) const = default;
};

struct RectI {
    int x = 0, y = 0, width = 0, height = 0;
    int right() const { return x + width; }
    int bottom() const { return y + height; }
    bool empty() const { return width <= 0 || height <= 0; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }
    RectI translated(int dx, int dy) const { return {x + dx, y + dy, width, height}; }
    RectI united(const RectI& o) const {
        if (empty()) return o;
        if (o.empty()) return *this;
        const int l = std::min(x, o.x), t = std::min(y, o.y);
        return {l, t, std::max(right(), o.right()) - l, std::max(bottom(), o.bottom()) - t};
    }
    RectI intersected(const RectI& o) const {
        const int l = std::max(x, o.x), t = std::max(y, o.y);
        const int r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return r > l && b > t ? RectI{l, t, r - l, b - t} : RectI{};
    }
    bool operator==(const RectI&) const = default;
};

// Margins in DIPs: a layout's contents margins, a widget's own contents margins (N3a).
struct Margins {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const Margins&) const = default;
};

// "Unbounded" for maximum sizes, in pixels (Qt's QWIDGETSIZE_MAX).
inline constexpr int kMaxPx = 16777215;
inline constexpr float kMaxDip = 16777215.0f;

// DIPs -> device pixels. Sizes round UP (a hint must never clip its content); lengths such as margins and spacing
// round to nearest.
inline int ceilPx(float dip, double scale) {
    if (dip >= kMaxDip) return kMaxPx;
    return static_cast<int>(std::ceil(static_cast<double>(dip) * scale - 1e-6));
}
inline int roundPx(float dip, double scale) { return static_cast<int>(std::lround(static_cast<double>(dip) * scale)); }

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static constexpr Color rgb(std::uint32_t hex, float alpha = 1.0f) {
        return {((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha};
    }
    bool operator==(const Color&) const = default;
};

}  // namespace tcad::ui

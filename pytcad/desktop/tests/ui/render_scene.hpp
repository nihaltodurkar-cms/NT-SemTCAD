// The N2a sample scene: one of each primitive the render core must draw -- solid and rounded rectangles, an
// antialiased line, an ellipse, a path, a pixel-snapped hairline and DirectWrite text (non-ASCII included) -- on a
// fixed 320 x 200 DIP canvas. Drawn by the render tests (goldens at 100/150/200%) and by tcad_ui_demo.
#pragma once

#include "ui/render/render_device.hpp"

namespace tcad::ui::testing {

constexpr float kSceneWidthDip = 320.0f;
constexpr float kSceneHeightDip = 200.0f;

void drawScene(ID2D1DeviceContext2* ctx, RenderDevice& device);

}  // namespace tcad::ui::testing

// The N2/N3 gallery (N2g, NATIVE-DESKTOP-PLAN.md 27.7; N3a, 27.8.2): every capability of the native UI framework
// core on one screen -- layouts (form, box with a stretch, grid, stack), text in several scripts, line edits (TSF),
// N3a's labels (a wrapped one), push buttons, a check box, a radio group in a group box, with
// hover/press/focus visuals, tooltips, keyboard focus and mnemonics, accessibility names -- with a status line that
// reports what the input system does. For screenshots (the N2 gate) and for the manual checklists (IME, Narrator,
// Accessibility Insights, high contrast).
#pragma once

#include "ui/win32/ui_window.hpp"

#include <functional>
#include <string>

namespace tcad::ui::gallery {

// Builds the gallery into `w`'s root. `status` receives a line for each interesting event.
void build(UiWindow& w, std::function<void(const std::string&)> status);

}  // namespace tcad::ui::gallery

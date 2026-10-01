// The clipboard as widgets see it (N3d, NATIVE-DESKTOP-PLAN.md 27.8.5). Portable: a window supplies the real one
// (ui/win32/clipboard.hpp behind UiHost::clipboard()), portable tests a fake. Text only (UTF-8): that is all the
// panels copy -- an edit's selection, a selectable label, a table's tab-separated cells.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tcad::ui {

class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual void setText(std::string_view utf8) = 0;
    virtual std::optional<std::string> text() = 0;  // nullopt: no text on the clipboard
};

}  // namespace tcad::ui

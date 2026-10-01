// The editor a list, table or tree opens over an item to edit its text in place (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6).
// Portable: the views are portable and the real editor is a LineEdit (TSF, the clipboard: Win32), so the window hands
// the views one through UiHost::createInlineEditor(); portable tests hand them a fake. A host without one makes every
// view read-only (nothing is edited), which is also what a view with no editable item looks like.
#pragma once

#include "ui/core/widget.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace tcad::ui {

class InlineEditor : public Widget {
public:
    virtual std::string text() const = 0;
    virtual void setText(std::string_view utf8) = 0;
    virtual void selectAll() = 0;
    // The edit ended: true = committed (Enter, or the focus left), false = cancelled (Escape). Called once.
    std::function<void(bool commit)> on_finished;
    // Tab / Shift+Tab while editing (a table moves to the next cell): called after the edit is committed.
    std::function<void(bool forward)> on_tab;
};

}  // namespace tcad::ui

// The in-place editor of the lists, tables and trees (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6): a LineEdit (TSF, the clipboard, the
// double click) behind the portable InlineEditor interface the views use (ui/core/inline_editor.hpp). UiWindow makes one
// through UiHost::createInlineEditor.
//   Enter commits; Escape cancels; the focus leaving commits (Qt's default for item editors); Tab and Shift+Tab are
//   reported as on_tab (a table commits and goes to the next cell) instead of moving the focus. The edit ends once.
#pragma once

#include "ui/core/inline_editor.hpp"
#include "ui/win32/line_edit.hpp"

namespace tcad::ui {

class LineEditEditor final : public InlineEditor {
public:
    explicit LineEditEditor(HWND window);

    std::string text() const override { return edit_->text(); }
    void setText(std::string_view utf8) override { edit_->setText(utf8); }
    void selectAll() override;
    LineEdit* lineEdit() const { return edit_; }

    bool keyEvent(const platform::KeyEvent& e) override;
    Role accessibleRole() const override { return Role::Pane; }

protected:
    void resized() override;

private:
    void finish(bool commit);

    LineEdit* edit_;
    bool finished_ = false;
};

}  // namespace tcad::ui

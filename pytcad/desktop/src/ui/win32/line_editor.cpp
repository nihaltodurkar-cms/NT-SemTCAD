#include "ui/win32/line_editor.hpp"

#include "ui/core/keys.hpp"

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;

LineEditEditor::LineEditEditor(HWND window) {
    edit_ = addChild<LineEdit>(window);
    edit_->name = "editor";
    edit_->setCaptureTab(true);
    setFocusProxy(edit_);
    edit_->on_editing_finished = [this] { finish(true); };  // Enter, or the focus leaving
}

void LineEditEditor::selectAll() {
    edit_->model().selectAll();
    edit_->update();
}

void LineEditEditor::resized() {
    const RectI g = geometry();
    edit_->setGeometry({0, 0, g.width, g.height});
}

void LineEditEditor::finish(bool commit) {
    if (finished_) return;
    finished_ = true;
    if (on_finished) on_finished(commit);
}

bool LineEditEditor::keyEvent(const KeyEvent& e) {
    if (!e.down) return false;
    if (e.vk == keys::Escape && e.mods == Mod::None) {
        finish(false);
        return true;
    }
    if (e.vk == keys::Tab && (e.mods == Mod::None || e.mods == Mod::Shift)) {
        if (finished_) return true;
        finished_ = true;  // the Tab ends the edit: the view commits it and moves on
        if (on_tab) on_tab(e.mods == Mod::None);
        return true;
    }
    return false;
}

}  // namespace tcad::ui

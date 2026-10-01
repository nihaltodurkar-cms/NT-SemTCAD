#include "ui/widgets/edit_context_menu.hpp"

namespace tcad::ui {

EditContextMenu::EditContextMenu(Kind kind, std::function<State()> state, std::function<void(Command)> act)
    : mgr_(nullptr), menu_("context"), state_(std::move(state)), act_(std::move(act)) {
    auto make = [&](const char* text, const char* shortcut, Command c) {
        Action* a = mgr_.create(text);
        a->setShortcut(shortcut);  // shown in the menu; the edit's own key handling does the work, so it is not registered anywhere
        a->on_triggered = [this, c](bool) {
            if (act_) act_(c);
        };
        return a;
    };
    if (kind == Kind::Edit) {
        undo_ = make("&Undo", "Ctrl+Z", Command::Undo);
        menu_.addAction(undo_);
        menu_.addSeparator();
        cut_ = make("Cu&t", "Ctrl+X", Command::Cut);
        menu_.addAction(cut_);
    }
    copy_ = make("&Copy", "Ctrl+C", Command::Copy);
    menu_.addAction(copy_);
    if (kind == Kind::Edit) {
        paste_ = make("&Paste", "Ctrl+V", Command::Paste);
        menu_.addAction(paste_);
        del_ = make("&Delete", "", Command::Delete);
        menu_.addAction(del_);
    }
    menu_.addSeparator();
    all_ = make("Select &All", "Ctrl+A", Command::SelectAll);
    menu_.addAction(all_);
    menu_.on_about_to_show = [this] {
        const State s = state_ ? state_() : State{};
        if (undo_) undo_->setEnabled(s.undo);
        if (cut_) cut_->setEnabled(s.cut);
        copy_->setEnabled(s.copy);
        if (paste_) paste_->setEnabled(s.paste);
        if (del_) del_->setEnabled(s.del);
        all_->setEnabled(s.select_all);
    };
}

bool EditContextMenu::show(Widget* owner, PointF local) {
    if (!owner) return false;
    const PointF origin = owner->mapFromWindow({0, 0});  // the widget's origin in the window, negated
    return menu_.showAt(owner, {local.x - origin.x, local.y - origin.y});
}

}  // namespace tcad::ui

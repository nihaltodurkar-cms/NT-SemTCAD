// The right-click menu of a text edit and of a selectable label (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7): Undo, Cut, Copy,
// Paste, Delete, Select All -- Windows' own edit menu, minus Right-to-left and the IME entries (0 uses). Built from the
// framework's own Menu and Action, so it is a real popup with the keyboard, mnemonics and UI Automation like any menu.
// The owner says what is possible NOW through `state` (asked every time the menu opens, so the enabled entries are never
// stale) and what each entry does through `act`. A selectable label has no Undo, Cut, Paste or Delete: it passes
// `Kind::Label` and gets Copy and Select All only.
#pragma once

#include "ui/core/widget.hpp"
#include "ui/widgets/action.hpp"
#include "ui/widgets/menu.hpp"

#include <functional>
#include <memory>

namespace tcad::ui {

class EditContextMenu {
public:
    enum class Kind { Edit, Label };
    enum class Command { Undo, Cut, Copy, Paste, Delete, SelectAll };
    struct State {
        bool undo = false, cut = false, copy = false, paste = false, del = false, select_all = false;
    };

    EditContextMenu(Kind kind, std::function<State()> state, std::function<void(Command)> act);
    // Opens the menu with its top-left at `local` (widget DIPs) of `owner`. False when it could not open.
    bool show(Widget* owner, PointF local);
    Menu& menu() { return menu_; }
    void close() { menu_.close(); }

private:
    ActionManager mgr_;
    Menu menu_;
    std::function<State()> state_;
    std::function<void(Command)> act_;
    Action *undo_ = nullptr, *cut_ = nullptr, *copy_ = nullptr, *paste_ = nullptr, *del_ = nullptr, *all_ = nullptr;
};

}  // namespace tcad::ui

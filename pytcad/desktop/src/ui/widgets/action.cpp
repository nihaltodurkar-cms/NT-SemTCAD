#include "ui/widgets/action.hpp"

#include "ui/core/input_router.hpp"
#include "ui/widgets/mnemonic.hpp"

namespace tcad::ui {

void Action::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    changed();
}

void Action::setToolTip(std::string t) {
    if (t == tooltip_) return;
    tooltip_ = std::move(t);
    changed();
}

void Action::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    changed();
}

void Action::setVisible(bool on) {
    if (on == visible_) return;
    visible_ = on;
    changed();
}

void Action::setCheckable(bool on) {
    if (on == checkable_) return;
    checkable_ = on;
    changed();
}

void Action::setChecked(bool on) {
    if (on == checked_) return;
    checked_ = on;
    changed();
    if (checkable_ && on_toggled) on_toggled(checked_);
}

std::unique_ptr<Action> Action::makeSeparator() {
    auto a = std::make_unique<Action>();
    a->separator_ = true;
    return a;
}

bool Action::setShortcut(std::string_view text) {
    if (!text.empty() && !platform::parseShortcut(text)) return false;
    shortcut_ = std::string(text);
    changed();
    return true;
}

void Action::trigger() {
    if (!enabled_ || separator_) return;
    if (checkable_) {
        checked_ = !checked_;
        changed();
        if (on_toggled) on_toggled(checked_);
    }
    if (on_triggered) on_triggered(checked_);
}

ActionManager::~ActionManager() {
    // the shortcuts point at the actions that die with this manager
    if (router_)
        for (auto& a : actions_)
            if (a->hasShortcut()) router_->shortcuts().remove(a->shortcutText());
}

Action* ActionManager::create(std::string text, std::string_view shortcut) {
    actions_.push_back(std::make_unique<Action>(std::move(text)));
    Action* a = actions_.back().get();
    if (!shortcut.empty()) bindShortcut(a, shortcut);
    return a;
}

Action* ActionManager::separator() {
    actions_.push_back(Action::makeSeparator());
    return actions_.back().get();
}

bool ActionManager::bindShortcut(Action* a, std::string_view shortcut) {
    if (!a) return false;
    if (a->hasShortcut() && router_) router_->shortcuts().remove(a->shortcutText());
    if (shortcut.empty()) return a->setShortcut({}), true;
    if (!platform::parseShortcut(shortcut)) return false;
    if (router_ && !router_->shortcuts().add(shortcut, [a] {
            if (a->isEnabled() && a->isVisible()) a->trigger();
        })) {
        ++conflicts_;  // taken already: the router's map refused it
        a->setShortcut({});
        return false;
    }
    return a->setShortcut(shortcut);
}

Action* ActionManager::find(std::string_view text) const {
    for (auto& a : actions_)
        if (parseMnemonic(a->text()).text == text) return a.get();
    return nullptr;
}

}  // namespace tcad::ui

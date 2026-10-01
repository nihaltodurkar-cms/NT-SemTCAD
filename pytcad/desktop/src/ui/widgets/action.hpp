// QAction and its manager (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7). QAction is 51 in the panels: the File/View/Run/Project
// menu entries and the main tool bar. What they call: text with a '&' mnemonic, setShortcut (Ctrl+O, F5, Shift+F5, the
// standard Open/Save/Quit/Undo/Redo keys), setEnabled (the Run/Stop pair, Undo/Redo), setCheckable/setChecked (the
// log-scale and view-mode entries), setToolTip, triggered and toggled. Not used, so not built: icons, actions with data,
// action groups, several shortcuts, global (application) shortcut contexts other than the window's own.
//
// An Action is a plain object -- not a widget -- that menus, tool bars and the window's shortcut table all read live, so
// changing its text, enabled state or check state changes every place it is shown. The ActionManager owns the actions of
// one window and registers their shortcuts with its InputRouter (platform::ShortcutMap, ui/core/input_router.hpp): a
// shortcut fires the action only while the action is enabled and visible. An action must outlive the menus and tool
// bars that show it (Qt's parent ownership: the manager owns them for the window's life).
//
//   * trigger() is the user's activation: nothing when disabled; a checkable action toggles first; then on_triggered(checked).
//   * setChecked reports on_toggled(checked) for the program's change too (Qt), but not on_triggered.
//   * a separator is an action with no text; a submenu entry is an action with a Menu.
#pragma once

#include "platform/input.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tcad::ui {

class InputRouter;
class Menu;

class Action {
public:
    std::function<void(bool checked)> on_triggered;
    std::function<void(bool checked)> on_toggled;

    explicit Action(std::string text = {}) : text_(std::move(text)) {}

    // Every place that shows the action listens for its changes (any property) and repaints or relays out. An id comes back to
    // remove the listener with; a listener must be removed before whatever it points to dies.
    int addListener(std::function<void()> fn) {
        listeners_.emplace_back(++next_listener_, std::move(fn));
        return next_listener_;
    }
    void removeListener(int id) {
        for (auto it = listeners_.begin(); it != listeners_.end(); ++it)
            if (it->first == id) {
                listeners_.erase(it);
                return;
            }
    }

    const std::string& text() const { return text_; }  // with its '&'
    void setText(std::string t);
    const std::string& toolTip() const { return tooltip_; }
    void setToolTip(std::string t);
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool on);
    bool isVisible() const { return visible_; }
    void setVisible(bool on);
    bool isCheckable() const { return checkable_; }
    void setCheckable(bool on);
    bool isChecked() const { return checked_; }
    void setChecked(bool on);
    bool isSeparator() const { return separator_; }
    static std::unique_ptr<Action> makeSeparator();

    // "Ctrl+O", "F5", "Shift+F5", "Alt+Left", "Ctrl+Shift+Z" (platform::parseShortcut); false and unchanged when it does not parse.
    bool setShortcut(std::string_view text);
    const std::string& shortcutText() const { return shortcut_; }  // as written, shown in a menu
    bool hasShortcut() const { return !shortcut_.empty(); }

    Menu* menu() const { return menu_; }  // a submenu entry
    void setMenu(Menu* m) { menu_ = m; }

    void trigger();

private:
    void changed() {
        const auto copy = listeners_;  // a listener may remove itself
        for (const auto& l : copy) l.second();
    }
    std::string text_, tooltip_, shortcut_;
    bool enabled_ = true, visible_ = true, checkable_ = false, checked_ = false, separator_ = false;
    Menu* menu_ = nullptr;
    std::vector<std::pair<int, std::function<void()>>> listeners_;
    int next_listener_ = 0;
};

class ActionManager {
public:
    // `router`: where shortcuts are registered; null: none (portable tests that do not need them).
    explicit ActionManager(InputRouter* router = nullptr) : router_(router) {}
    ~ActionManager();
    ActionManager(const ActionManager&) = delete;
    ActionManager& operator=(const ActionManager&) = delete;

    // A new action, owned here. `shortcut`: its key, registered now. A shortcut that is already taken is refused by the
    // router's map: the action is still made, without the shortcut (shortcutText stays empty), and conflicts() counts it.
    Action* create(std::string text, std::string_view shortcut = {});
    Action* separator();
    // Registers the shortcut of an action made elsewhere (and changes it): the old one is released.
    bool bindShortcut(Action* a, std::string_view shortcut);
    int count() const { return static_cast<int>(actions_.size()); }
    int conflicts() const { return conflicts_; }
    Action* find(std::string_view text) const;  // by text with '&' removed

private:
    InputRouter* router_;
    std::vector<std::unique_ptr<Action>> actions_;
    int conflicts_ = 0;
};

}  // namespace tcad::ui

// QMenuBar (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7): the row of menu titles under the window's title bar -- File, View, Run,
// Project in the panels. Drawn by the framework, not a native HMENU (decision 3 of 27.8). Not used, so not built: a corner
// widget, a native macOS menu, menu bar visibility toggles, titles with icons.
//
// BEHAVIOUR (Windows' menu bar, which Qt matches):
//   * a click on a title opens its menu below it; a click on the open one closes it; while a menu is open the pointer
//     moving to another title switches to that title's menu.
//   * Alt+<letter> opens the menu whose title has that mnemonic (the framework's mnemonic dispatch); a lone Alt press and
//     release ACTIVATES the bar -- the first title is highlighted, Left/Right move, Down/Enter open, Esc (or another Alt
//     tap) leaves -- and the system's own menu mode, which a lone Alt would otherwise start, is not entered. (N3a noted
//     this: "N3f's menu bar will take Alt over".)
//   * while a menu is open, Left/Right at the top level move to the neighbouring title's menu (a submenu's Left closes
//     it first).
//   * mnemonic underlines show once the bar is activated or Alt is held (the window's keyboard cues).
// UI Automation: a MenuBar with MenuItem titles (ExpandCollapse: open or closed).
#pragma once

#include "ui/widgets/menu.hpp"

#include <vector>

namespace tcad::ui {

class MenuBar;

class MenuBarItem : public Widget {
public:
    MenuBarItem(MenuBar* bar, Menu* menu) : bar_(bar), menu_(menu) {}
    Menu* menu() const { return menu_; }
    void refresh();  // the title changed
    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    void hoverChanged(bool entered) override;
    void activateMnemonic() override;
    Role accessibleRole() const override { return Role::MenuItem; }
    bool accessibleInvoke() override;
    int accessibleExpandState() const override { return menu_->isOpen() ? 1 : 0; }
    void accessibleExpand(bool open) override;

    static constexpr float kPadH = 8.0f;

private:
    MenuBar* bar_;
    Menu* menu_;
};

class MenuBar : public Widget {
public:
    MenuBar();
    ~MenuBar() override;

    // The menus are shown, not owned (see Action's note: they live as long as the window's actions do).
    void addMenu(Menu* menu);
    int count() const { return static_cast<int>(items_.size()); }
    MenuBarItem* item(int i) const { return i >= 0 && i < count() ? items_[static_cast<std::size_t>(i)] : nullptr; }
    int openIndex() const;       // the title whose menu is open, -1 none
    int activeIndex() const { return active_; }  // the highlighted title (keyboard mode), -1 none
    bool isActive() const { return active_ >= 0; }
    void activate();             // the Alt tap: the first title is highlighted
    void deactivate();
    void openMenu(int i, bool select_first = false);
    void closeMenus();

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::MenuBar; }

    static constexpr float kPadV = 4.0f;

protected:
    void resized() override {
        layoutItems();
        ensureAltHandler();
    }

private:
    friend class MenuBarItem;
    void layoutItems();
    void titleClicked(int i);
    void titleHovered(int i);
    void ensureAltHandler();
    void installFilters();
    void removeFilters();
    bool keyFilter(const platform::KeyEvent& e);
    void menuClosed(int i);
    std::vector<MenuBarItem*> items_;
    int active_ = -1;
    int switching_to_ = -1;
    bool filters_ = false;
    bool alt_registered_ = false;
};

}  // namespace tcad::ui

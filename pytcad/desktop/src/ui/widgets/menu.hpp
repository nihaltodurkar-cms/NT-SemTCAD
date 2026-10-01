// QMenu (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7; decision 3 of 27.8: menus are drawn, in top-level POPUP WINDOWS, never a native
// HMENU). 9 in the panels: the File/View/Run/Project menus, their submenus (recent files, view mode), and menus whose
// contents are rebuilt each time they open (aboutToShow). What they call: addAction, addSeparator, addMenu, clear,
// setTitle with a '&', aboutToShow. Not used, so not built: icons, tear-off, widget actions, context-menu policies on
// the views (the edits' own context menu is built here), menu-wide font changes.
//
// A Menu is a model, not a widget: a title and a list of entries -- an Action, a separator, or a submenu -- shown by
// show*(), which makes a MenuPopup (the popup's content) and asks the host's PopupService for a window. The popups never
// take the keyboard focus (ui/core/popup.hpp); while a menu is open the window's InputRouter hands it every key first
// (the key filter), and a press anywhere in the owner window closes the menus and is swallowed, as Windows' own menus do.
//
// BEHAVIOUR (Windows'; Qt's where it agrees):
//   * entries show their text with the '&' mnemonic underlined, the shortcut right-aligned and dim, a check mark for a
//     checked checkable action, a triangle for a submenu; disabled ones are faint and cannot be highlighted or used; an
//     action that is not visible is not shown (and two separators never touch, nor start or end the list).
//   * the pointer highlights the entry under it; a release on it activates it (the menu closes FIRST, then the action
//     is triggered, as Qt does); a submenu opens after 250 ms of hovering, or at once on a click, beside its entry.
//   * keys: Up/Down move the highlight (wrapping, skipping separators and disabled entries), Home/End, Right opens a
//     submenu and moves into it, Left (or Esc) closes the innermost, Esc on the outermost closes the menu, Enter/Space
//     activate, a letter activates the entry with that mnemonic (several: highlights the next one first).
//   * on_about_to_show runs before each show -- a recent-files menu rebuilds itself there; on_closed after it closes.
// UI Automation: the popup is a Menu with MenuItem children (Invoke; Toggle for checkable; ExpandCollapse for submenus).
#pragma once

#include "ui/core/popup.hpp"
#include "ui/core/widget.hpp"
#include "ui/widgets/action.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tcad::ui {

class MenuPopup;

class Menu {
public:
    std::function<void()> on_about_to_show;
    std::function<void()> on_closed;
    // A menu bar sets this: Left/Right at the top level (no submenu to open or close) go to the neighbouring title.
    // dir is -1 or +1; true: handled.
    std::function<bool(int dir)> on_navigate;

    explicit Menu(std::string title = {}) : title_(std::move(title)) {}
    ~Menu();
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    const std::string& title() const { return title_; }
    void setTitle(std::string t) { title_ = std::move(t); }

    // The entries, in order. An action is shown, not owned; a separator is made here; a submenu is shown, not owned.
    struct Entry {
        Action* action = nullptr;  // null for a submenu entry
        Menu* submenu = nullptr;
    };
    void addAction(Action* a);
    void addSeparator();
    void addMenu(Menu* sub);
    void clear();
    const std::vector<Entry>& entries() const { return entries_; }
    // The entries that would be shown: invisible actions dropped, separators neither first, last nor doubled.
    std::vector<Entry> visibleEntries() const;
    bool isEmpty() const { return visibleEntries().empty(); }

    // -- showing. `owner`: a widget of the window the menu belongs to (its host's popups and router are used).
    bool showBelow(Widget* owner, Widget* anchor);                    // under `anchor` (a menu bar title, a button)
    bool showAt(Widget* owner, PointF window_dips);                   // top-left at a point (a context menu)
    void close();                                                     // this menu and any submenu of it
    bool isOpen() const { return popup_ != nullptr; }
    MenuPopup* popup() const { return popup_; }
    int shownCount() const { return shown_; }

    static constexpr int kSubmenuDelayMs = 250;

private:
    friend class MenuPopup;
    bool open(Widget* owner, const PopupRequest& request, MenuPopup* parent);
    void popupClosed();
    std::string title_;
    std::vector<Entry> entries_;
    std::vector<std::unique_ptr<Action>> owned_;  // the separators
    MenuPopup* popup_ = nullptr;
    PopupHandle* handle_ = nullptr;
    Widget* owner_ = nullptr;
    int shown_ = 0;
    bool root_ = false;  // the one that installed the router's filters
};

// A menu entry as a widget: it paints itself and takes the mouse; the popup drives the keyboard.
class MenuItemWidget : public Widget {
public:
    MenuItemWidget(MenuPopup* popup, int index) : popup_(popup), index_(index) {}
    int index() const { return index_; }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    void hoverChanged(bool entered) override;
    bool accessibleEnabled() const override;
    Role accessibleRole() const override;  // a separator entry is a Separator, not an item
    bool accessibleInvoke() override;
    int accessibleToggleState() const override;
    void accessibleToggle() override { accessibleInvoke(); }
    int accessibleExpandState() const override;
    void accessibleExpand(bool open) override;

private:
    MenuPopup* popup_;
    int index_;
};

// The popup's content: the entries stacked, with the highlight and the keyboard.
class MenuPopup : public Widget {
public:
    MenuPopup(Menu* menu, MenuPopup* parent, std::vector<Menu::Entry> entries);
    ~MenuPopup() override;

    Menu* menu() const { return menu_; }
    MenuPopup* parentPopup() const { return parent_; }
    MenuPopup* childPopup() const { return child_; }
    MenuPopup* innermost();
    const std::vector<Menu::Entry>& entries() const { return entries_; }
    int highlighted() const { return highlight_; }
    void setHighlighted(int i);                    // -1 none; a separator or a disabled entry is refused
    bool move(int dir);                            // Up/Down: the next usable entry, wrapping
    void first() { highlight_ = -1, move(+1); }
    void last() { highlight_ = static_cast<int>(entries_.size()), move(-1); }
    bool activate(int i = -2);                     // -2: the highlighted entry
    bool openSubmenu(int i, bool from_keyboard);
    void closeSubmenu();
    bool typeMnemonic(char32_t c);
    float rowTop(int i) const;
    float rowHeight(int i) const;
    bool entryUsable(int i) const;
    bool isCheckedEntry(int i) const;
    std::string entryText(int i) const;            // the shown text, '&' marker removed
    std::string entryShortcut(int i) const;

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::Menu; }

    static constexpr float kSeparatorHeight = 7.0f;
    static constexpr float kPadV = 3.0f;     // above the first and below the last entry
    static constexpr float kCheckColumn = 24.0f;
    static constexpr float kPadH = 10.0f;
    static constexpr float kGap = 24.0f;     // between the text and the shortcut
    static constexpr float kArrowColumn = 20.0f;

protected:
    void resized() override;

private:
    friend class Menu;
    friend class MenuItemWidget;
    void layoutItems();
    void hoverItem(int i, bool entered);
    void armSubmenu(int i);
    void closeAll();                               // every menu of the chain
    void detachActions();                          // stop listening to the entries' actions (when closed: the popup may live on until the window frees it)
    Menu* menu_;
    MenuPopup* parent_;
    MenuPopup* child_ = nullptr;
    std::vector<Menu::Entry> entries_;
    std::vector<MenuItemWidget*> items_;
    std::vector<int> listener_ids_;  // one per entry (-1: a submenu entry)
    int highlight_ = -1;
    int pressed_ = -1;
    TimerId submenu_timer_ = 0;
    int pending_submenu_ = -1;
    float row_h_ = 22;
};

}  // namespace tcad::ui

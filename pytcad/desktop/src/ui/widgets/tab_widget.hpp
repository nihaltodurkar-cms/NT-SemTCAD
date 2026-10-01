// QTabWidget (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7): 4 in the panels -- the build panel's tabs (Structure, Process, Catalog...),
// its inner tabs, and the study pages. What they call: addTab(page, text), currentIndex/setCurrentIndex, currentChanged,
// setTabText, count, widget(i). Not used, so not built: closable or movable tabs, tab icons, tabs on the sides or bottom,
// scroll buttons for an overflowing bar (the tabs share the width instead, down to 48 DIPs each), corner widgets,
// document mode.
//
// A tab bar above a stack of pages; only the current page is visible. The first tab added is current (Qt's); removing the
// current one makes its neighbour current.
//   * the bar is one Tab stop (the TabWidget has the focus; the current tab shows the focus frame): Left/Right move to the
//     neighbouring ENABLED tab, Home/End the first/last; Ctrl+Tab / Ctrl+Shift+Tab (and Ctrl+PageDown / Ctrl+PageUp)
//     cycle through the tabs from anywhere inside the widget; a click on a tab selects it; Alt+<the tab's mnemonic> too.
//   * a disabled tab is faint and cannot be selected; setTabToolTip gives its tool tip.
//   * currentChanged(index) fires for the program's change too; setCurrentIndexSilent is QSignalBlocker.
// UI Automation: a Tab control with TabItem children (SelectionItem; the selected one is the current) and the current
// page; Selection pattern on the control.
#pragma once

#include "ui/core/widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tcad::ui {

class TabWidget;

class TabButton : public Widget {
public:
    TabButton(TabWidget* t, int index) : tabs_(t), index_(index) {}
    int index() const { return index_; }
    void setIndex(int i) { index_ = i; }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    void activateMnemonic() override;
    Role accessibleRole() const override { return Role::TabItem; }
    int accessibleSelectionState() const override;
    void accessibleSelect() override;
    Widget* accessibleSelectionContainer() const override;
    bool accessibleFocused() const override;
    bool accessibleFocusable() const override { return true; }

private:
    TabWidget* tabs_;
    int index_;
};

class TabWidget : public Widget {
public:
    std::function<void(int)> on_current_changed;

    TabWidget();

    int addTab(std::unique_ptr<Widget> page, std::string text);
    int insertTab(int index, std::unique_ptr<Widget> page, std::string text);
    std::unique_ptr<Widget> removeTab(int index);  // the page, no longer owned here; null out of range
    int count() const { return static_cast<int>(tabs_.size()); }
    Widget* widget(int i) const { return i >= 0 && i < count() ? tabs_[static_cast<std::size_t>(i)].page : nullptr; }
    int indexOf(const Widget* page) const;
    Widget* currentWidget() const { return widget(current_); }
    int currentIndex() const { return current_; }
    void setCurrentIndex(int i);
    void setCurrentIndexSilent(int i);
    std::string tabText(int i) const;  // as set, with its '&'
    void setTabText(int i, std::string text);
    void setTabEnabled(int i, bool on);
    bool isTabEnabled(int i) const;
    void setTabToolTip(int i, std::string tip);

    // geometry (DIPs, widget coordinates)
    float tabBarHeight() const;
    RectF tabRect(int i) const;
    RectF pageRect() const;
    TabButton* tabButton(int i) const { return i >= 0 && i < count() ? tabs_[static_cast<std::size_t>(i)].button : nullptr; }

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override;
    void paint(Painter& p) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    void focusChanged(bool, FocusReason) override { update(); }

    Role accessibleRole() const override { return Role::Tab; }
    bool accessibleIsSelectionContainer() const override { return true; }
    std::vector<Widget*> accessibleSelection() const override;
    Widget* accessibleFocusChild() const override { return tabButton(current_); }
    bool accessibleFocused() const override { return false; }

    static constexpr float kMinTab = 48.0f;
    static constexpr float kPadH = 12.0f;
    static constexpr float kPadV = 5.0f;

protected:
    void resized() override { layoutParts(); }

private:
    friend class TabButton;
    struct Tab {
        Widget* page = nullptr;
        TabButton* button = nullptr;
        std::string text;
        bool enabled = true;
    };
    void layoutParts();
    void select(int i, bool notify);
    int stepEnabled(int from, int dir) const;
    std::vector<Tab> tabs_;
    int current_ = -1;
};

}  // namespace tcad::ui

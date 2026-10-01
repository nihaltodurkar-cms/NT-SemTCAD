// QToolBar and the tool tip (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7). QToolBar is 2 in the panels (the main tool bar and the
// one findChild looks for); setToolTip is 28 (lists, buttons, fields saying why something is disabled). Not used, so not
// built: icons (the tool bar's buttons are text), movable or floating bars, an overflow menu, vertical bars, tool button
// menus, rich-text tool tips.
//
// TOOL BAR: a row of buttons, one per Action, with separators and, if a panel needs one, an embedded widget (a combo box
// of view modes). A button shows the action's text (its '&' removed: a tool bar has no mnemonics), is raised on hover,
// pressed while the mouse is down and while a checkable action is checked, faint when the action is disabled, and
// triggers the action on a click (press and release on it) or on Space/Enter when it has the keyboard focus. Its tool
// tip is the action's tool tip, else its text, followed by the shortcut in parentheses. The buttons are Tab stops, as
// Windows' tool bars' items are not: a tool bar is ONE Tab stop and Left/Right move between its buttons (roving focus).
// UI Automation: a ToolBar with Button children (Invoke; Toggle for checkable actions).
//
// TOOL TIP: a small popup window near the pointer, shown by the window after the pointer rests on a widget with tool
// tip text (InputRouter's on_tooltip, 700 ms) and hidden by any press, key, hover change or the owner deactivating. It
// never takes the focus or dismisses a menu. The text wraps at 320 DIPs.
#pragma once

#include "ui/core/popup.hpp"
#include "ui/core/widget.hpp"
#include "ui/widgets/action.hpp"

#include <vector>

namespace tcad::ui {

class ToolBar;

class ToolButton : public Widget {
public:
    ToolButton(ToolBar* bar, Action* action);
    ~ToolButton() override;
    Action* action() const { return action_; }
    void refresh();  // the action changed
    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    void hoverChanged(bool) override { update(); }
    void focusChanged(bool, FocusReason) override { update(); }
    bool isTabStop() const override;
    Role accessibleRole() const override { return Role::Button; }
    bool accessibleInvoke() override;
    int accessibleToggleState() const override { return action_->isCheckable() ? (action_->isChecked() ? 1 : 0) : -1; }
    void accessibleToggle() override { accessibleInvoke(); }

    static constexpr float kPadH = 8.0f;
    static constexpr float kPadV = 4.0f;

private:
    friend class ToolBar;
    ToolBar* bar_;
    Action* action_;
    int listener_ = -1;
    bool down_ = false;
};

class ToolBar : public Widget {
public:
    ToolBar();

    ToolButton* addAction(Action* a);
    void addSeparator();
    Widget* addWidget(std::unique_ptr<Widget> w);  // an embedded widget, laid out in the row
    int buttonCount() const;
    ToolButton* button(int i) const;
    ToolButton* currentButton() const { return roving_; }  // the one that takes the Tab stop

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::ToolBar; }

    static constexpr float kSpacing = 2.0f;
    static constexpr float kMargin = 2.0f;
    static constexpr float kSeparator = 7.0f;

protected:
    void resized() override { layoutItems(); }

private:
    friend class ToolButton;
    class Separator;
    void layoutItems();
    void moveRoving(int dir);
    std::vector<Widget*> items_;  // buttons, separators and embedded widgets, in order
    ToolButton* roving_ = nullptr;
};

// The content of a tool tip popup.
class ToolTipLabel : public Widget {
public:
    explicit ToolTipLabel(std::string text) : text_(std::move(text)) {}
    const std::string& text() const { return text_; }
    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    bool hasHeightForWidth() const override { return false; }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::Text; }

    static constexpr float kMaxWidth = 320.0f;
    static constexpr float kPad = 6.0f;

private:
    std::string text_;
};

}  // namespace tcad::ui

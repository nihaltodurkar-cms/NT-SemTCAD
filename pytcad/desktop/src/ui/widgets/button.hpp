// Buttons (N3a, NATIVE-DESKTOP-PLAN.md 27.8.1): QAbstractButton's behaviour and the three concrete buttons the
// panels use -- PushButton (128), CheckBox (49), RadioButton (8) -- plus ButtonGroup (1).
//
// Behaviour (Qt's, pinned by tests/ui/test_ui_widgets.cpp):
//   * a left press makes the button DOWN; it stays down only while the pointer is over it; a release while down
//     CLICKS. Space press/release does the same from the keyboard (a key-repeat does nothing); losing focus while
//     down cancels. A PushButton also clicks on Enter.
//   * click(): a checkable button toggles first (on_toggled), then on_clicked runs. A disabled button never clicks.
//   * Alt+<'&' letter> focuses and clicks it.
//   * exclusive buttons -- RadioButtons (auto-exclusive with their siblings, as in Qt) or the members of an
//     exclusive ButtonGroup: checking one unchecks the rest; the checked one cannot be unchecked by a click or by
//     setChecked(false). Arrow keys move focus to the previous/next enabled, visible member (wrapping) and check it.
//     The group is one Tab stop: its checked member, or its first member while none is checked (Windows' rule).
// DEFAULT button (N3f): a PushButton can be the dialog's default (setDefault): it is drawn with the accent ring; pressing Enter
// with the focus elsewhere in the dialog is the dialog's business (MessageBoxContent), and a focused button's own Enter clicks it.
// Not built (0 uses, measured): tri-state check boxes, checkable push buttons, auto-default push buttons, icons, flat buttons.
#pragma once

#include "ui/core/widget.hpp"
#include "ui/widgets/mnemonic.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tcad::ui {

class ButtonGroup;

class AbstractButton : public Widget {
public:
    std::function<void()> on_clicked;
    std::function<void(bool)> on_toggled;

    explicit AbstractButton(std::string text = {});
    ~AbstractButton() override;

    void setText(std::string text);  // '&' marks the mnemonic
    const std::string& text() const { return shown_.text; }
    void setCheckable(bool on) { checkable_ = on; }
    bool isCheckable() const { return checkable_; }
    void setChecked(bool on);
    bool isChecked() const { return checked_; }
    void setAutoExclusive(bool on) { auto_exclusive_ = on; }
    bool autoExclusive() const { return auto_exclusive_; }
    ButtonGroup* group() const { return group_; }
    bool isDown() const { return down_; }
    void click();  // as the user's click

    // Widget
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    void hoverChanged(bool) override { update(); }
    void focusChanged(bool in, FocusReason) override;
    void activateMnemonic() override;
    bool isTabStop() const override;
    bool accessibleInvoke() override;

protected:
    // The members this button is exclusive with, itself included, in tree order; empty when it is not exclusive.
    std::vector<AbstractButton*> exclusiveMembers() const;
    SizeF textSize() const;
    const MnemonicText& label() const { return shown_; }

private:
    friend class ButtonGroup;
    void setCheckedInternal(bool on);  // no exclusivity: sets the state, notifies
    bool moveInGroup(int step);

    MnemonicText shown_;
    bool checkable_ = false;
    bool checked_ = false;
    bool auto_exclusive_ = false;
    bool down_ = false;
    bool key_down_ = false;  // down because Space is held
    ButtonGroup* group_ = nullptr;
};

class PushButton : public AbstractButton {
public:
    explicit PushButton(std::string text = {});
    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    bool keyEvent(const platform::KeyEvent& e) override;
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::Button; }
    void setDefault(bool on) {
        default_ = on;
        update();
    }
    bool isDefault() const { return default_; }

private:
    bool default_ = false;
};

class CheckBox : public AbstractButton {
public:
    explicit CheckBox(std::string text = {});
    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::CheckBox; }
    bool accessibleInvoke() override { return false; }  // Toggle, not Invoke (as UIA's check boxes)
    int accessibleToggleState() const override { return isChecked() ? 1 : 0; }
    void accessibleToggle() override { click(); }
};

class RadioButton : public AbstractButton {
public:
    explicit RadioButton(std::string text = {});
    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::RadioButton; }
    bool accessibleInvoke() override { return false; }  // SelectionItem
    int accessibleSelectionState() const override { return isChecked() ? 1 : 0; }
    void accessibleSelect() override {
        if (!isChecked()) click();
    }
};

// QButtonGroup: a non-widget set of buttons, exclusive by default. The buttons may have different parents. Neither
// owns the other: a button that dies leaves its group, a group that dies releases its buttons.
class ButtonGroup {
public:
    std::function<void(int)> on_id_clicked;
    std::function<void(int, bool)> on_id_toggled;

    ButtonGroup() = default;
    ~ButtonGroup();
    ButtonGroup(const ButtonGroup&) = delete;
    ButtonGroup& operator=(const ButtonGroup&) = delete;

    void addButton(AbstractButton* b, int id = -1);  // id -1: the next free negative id (-2, -3, ...), as Qt
    void removeButton(AbstractButton* b);
    const std::vector<AbstractButton*>& buttons() const { return buttons_; }
    void setExclusive(bool on) { exclusive_ = on; }
    bool exclusive() const { return exclusive_; }
    AbstractButton* checkedButton() const;
    int checkedId() const;  // -1 when none
    int id(const AbstractButton* b) const;
    AbstractButton* button(int id) const;

private:
    friend class AbstractButton;
    std::vector<AbstractButton*> buttons_;
    std::vector<int> ids_;
    bool exclusive_ = true;
    int next_auto_id_ = -2;
};

}  // namespace tcad::ui

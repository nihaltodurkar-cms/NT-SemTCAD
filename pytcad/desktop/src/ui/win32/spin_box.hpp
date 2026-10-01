// QSpinBox and QDoubleSpinBox (N3b, NATIVE-DESKTOP-PLAN.md 27.8.3): 40 and 144 uses in the panels -- setRange 31,
// setDecimals 27, setValue 41, setSingleStep 7, setSuffix 1, setSpecialValueText 1, editingFinished 17 -- on a
// LineEdit, so it lives beside it (Win32: TSF and the clipboard), with every number rule in ui/widgets/numeric.hpp
// (portable, tested without a window).
//
// The deliberate differences from Qt (decision 1 and 4 of 27.8), each a rule the tests pin:
//   * SCIENTIFIC ENTRY WITH UNITS. "1e17", "5k", "2.5 nm" (in a "m" field: 2.5 nano-metres), "5 V" are all accepted;
//     see numeric.hpp. Values are SI underneath. The display is Qt's (`decimals` places) while that shows the value
//     exactly and is under 1e9, else scientific ("1e17").
//   * NEVER SILENT. Text that is not a number, is outside the range, or (SpinBox) is not whole is REJECTED: the text
//     goes back to the value, the field's frame shows the error while the text is wrong, and on_rejected says why.
//     Qt clamps it without a word. A value set by the program is clamped to the range (setValue returns false when
//     it was), as in Qt, and is never rounded to `decimals`.
//   * editingFinished means a CHANGE. on_editing_finished fires on Enter or focus loss only when the value now
//     differs from the value at the last such event and the text was valid -- not on every focus loss (Qt's 17
//     connections all mean "apply the edit"; a pointless apply is an undo step for nothing).
//   * DECADE STEPPING (DoubleSpinBox::setDecadeStep): Up multiplies by 10 and Down divides, for doping and time
//     fields that span decades; linear fields add the single step.
// Same as Qt: Up/Down step, PageUp/PageDown ten steps (one step on a decade field), the wheel steps a notch -- but only
// while the field has the focus -- and the buttons auto-repeat after 500 ms at 75 ms. A step reads the text first, so
// a typed value steps from itself. Escape takes back a pending edit. Focus by Tab or mnemonic selects the text.
// Keyboard-tracking off, prefix, wrapping, accelerated stepping and the read-only flag have 0 uses, so are not built.
//
// UI Automation: Spinner with RangeValue (the number) and Value (the text), and the edit as its child.
#pragma once

#include "ui/core/widget.hpp"
#include "ui/win32/line_edit.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace tcad::ui {

enum class Reject { NotANumber, OutOfRange, NotWhole };

class NumberField : public Widget {
public:
    // Enter or focus loss, when the value changed and the text is valid.
    std::function<void()> on_editing_finished;
    // The text was refused (and put back): what was typed and why.
    std::function<void(const std::string&, Reject)> on_rejected;

    ~NumberField() override = default;

    LineEdit* edit() const { return edit_; }
    const std::string& text() const { return edit_->text(); }
    bool isInputValid() const { return !edit_->isInvalid(); }  // the text as typed, right now
    void setSuffix(std::string unit);
    const std::string& suffix() const { return unit_; }
    void setSingleStep(double s);
    double singleStepValue() const { return step_; }

    // Steps `n` times (the buttons, arrows, wheel; public for tests). Reads a pending typed value first.
    void stepBy(int n);
    // Commits the text as Enter does: parse, range-check, apply or reject; then on_editing_finished if it changed.
    // False when the text was rejected.
    bool commit();

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    void hoverChanged(bool entered) override;
    void activateMnemonic() override { edit_->setFocus(FocusReason::Mnemonic); }

    Role accessibleRole() const override { return Role::Spinner; }
    bool accessibleHasValue() const override { return true; }
    std::string accessibleValue() const override { return edit_->text(); }
    bool accessibleSetValue(std::string_view v) override;
    bool accessibleReadOnly() const override { return false; }
    AccessibleRange accessibleRange() const override;
    bool accessibleSetRangeValue(double v) override;

    static constexpr float kButtonWidth = 16.0f;
    static constexpr int kRepeatDelayMs = 500;
    static constexpr int kRepeatRateMs = 75;

protected:
    explicit NumberField(HWND window, bool integral);
    void resized() override;

    double value_ = 0, min_ = 0, max_ = 99, step_ = 1;
    int decimals_ = 2;
    bool decade_ = false;
    std::string special_;  // SpinBox: the text shown at the minimum
    // Sets the value (clamped; true when it was not clamped). notify: on_value_changed and the UIA event.
    // `user`: the value came from the person (typing, a step): it does not become the editing-finished baseline, so
    // the next Enter or focus loss reports it. A program's setValue is the new baseline (loading a document is no edit).
    bool setValueImpl(double v, bool notify, bool user = false);
    void setRangeImpl(double lo, double hi);
    void refreshText();
    virtual void emitValueChanged() = 0;

private:
    struct Outcome {
        bool ok = false;
        bool neutral = false;  // empty or a start of a number: no complaint while it is still being typed
        Reject why = Reject::NotANumber;
        double value = 0;
    };
    Outcome evaluate(const std::string& text) const;
    void textTyped();
    void fireIfChanged();
    std::string display(double v) const;
    void stopRepeat();
    void startRepeat(int direction);
    int buttonAt(PointF p) const;  // 1 up, 2 down, 0 none
    RectF buttonRect(int which) const;

    LineEdit* edit_ = nullptr;
    bool integral_;
    std::string unit_;
    std::string shown_;    // the text last put in the edit
    double committed_ = 0; // the value at the last editing-finished decision
    int hot_ = 0, down_ = 0;
    bool down_inside_ = false;
    TimerId repeat_ = 0;
    int repeat_dir_ = 0;
    bool refreshing_ = false;
};

class SpinBox : public NumberField {
public:
    std::function<void(int)> on_value_changed;

    explicit SpinBox(HWND window);

    void setRange(int lo, int hi) { setRangeImpl(lo, hi); }
    int minimum() const { return static_cast<int>(min_); }
    int maximum() const { return static_cast<int>(max_); }
    bool setValue(int v) { return setValueImpl(v, true); }
    void setValueSilent(int v) { setValueImpl(v, false); }
    int value() const { return static_cast<int>(value_); }
    void setSingleStep(int s) { NumberField::setSingleStep(s); }
    void setSpecialValueText(std::string t);  // shown at the minimum; typing it gives the minimum

protected:
    void emitValueChanged() override {
        if (on_value_changed) on_value_changed(static_cast<int>(value_));
    }
};

class DoubleSpinBox : public NumberField {
public:
    std::function<void(double)> on_value_changed;

    explicit DoubleSpinBox(HWND window);

    void setRange(double lo, double hi) { setRangeImpl(lo, hi); }
    double minimum() const { return min_; }
    double maximum() const { return max_; }
    void setDecimals(int d);
    int decimals() const { return decimals_; }
    bool setValue(double v) { return setValueImpl(v, true); }
    void setValueSilent(double v) { setValueImpl(v, false); }
    double value() const { return value_; }
    void setSingleStep(double s) { NumberField::setSingleStep(s); }
    void setDecadeStep(bool on) { decade_ = on; }
    bool decadeStep() const { return decade_; }

protected:
    void emitValueChanged() override {
        if (on_value_changed) on_value_changed(value_);
    }
};

}  // namespace tcad::ui

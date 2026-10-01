// QSlider, horizontal (N3b, NATIVE-DESKTOP-PLAN.md 27.8.3): 3 in the panels (playback frame, plot cut position, the
// 3D slice), each with setRange/setValue and valueChanged. Not built (0 uses): vertical sliders, ticks, inverted
// appearance, sliderMoved/pressed/released signals, tracking off.
//
// Behaviour (Qt's where Qt has one, else Windows'):
//   * keys: Left/Down one single step down, Right/Up one up, PageDown/PageUp one page, Home/End the ends.
//   * mouse: a left press on the handle grabs it (the value follows the pointer, keeping the grab offset); a press on
//     the groove jumps the handle to the pointer and grabs it (Windows 11's behaviour; Qt's Fusion steps by a page).
//   * wheel, when the slider has the focus: three single steps per notch, at most a page (Qt's rule); never while it
//     does not have the focus, so a wheel over a form does not change a value on its way past.
//   * setValue clamps to the range; on_value_changed fires for every change, including setValue (Qt). The panels that
//     block signals while loading use setValueSilent.
//   * UI Automation: Slider with RangeValue.
#pragma once

#include "ui/core/widget.hpp"

#include <functional>

namespace tcad::ui {

class Slider : public Widget {
public:
    std::function<void(int)> on_value_changed;

    Slider();

    void setRange(int minimum, int maximum);  // maximum < minimum becomes minimum (Qt); the value is clamped
    int minimum() const { return min_; }
    int maximum() const { return max_; }
    void setValue(int v);
    void setValueSilent(int v);  // clamped; no on_value_changed (QSignalBlocker)
    int value() const { return value_; }
    void setSingleStep(int s) { single_ = s < 1 ? 1 : s; }
    void setPageStep(int s) { page_ = s < 1 ? 1 : s; }
    int singleStep() const { return single_; }
    int pageStep() const { return page_; }
    bool isSliderDown() const { return dragging_; }

    // The handle, in widget DIPs (tests and painting).
    RectF handleRect() const;
    int valueAtX(float x) const;  // the value whose handle is centred at x (DIPs), clamped and rounded

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override;
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    void focusChanged(bool, FocusReason) override { update(); }
    void hoverChanged(bool) override { update(); }

    Role accessibleRole() const override { return Role::Slider; }
    AccessibleRange accessibleRange() const override;
    bool accessibleSetRangeValue(double v) override;

    static constexpr float kHandle = 16.0f;  // the handle's width and height (Fusion's 16-DIP slider handle)
    static constexpr float kGroove = 4.0f;
    static constexpr float kLength = 84.0f;  // QSlider's preferred length
    static constexpr float kThickness = 20.0f;

private:
    bool setClamped(int v, bool notify);

    int min_ = 0, max_ = 99, value_ = 0, single_ = 1, page_ = 10;
    bool dragging_ = false;
    float grab_ = 0;  // pointer x minus the handle's centre, while dragging
};

}  // namespace tcad::ui

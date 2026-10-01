// ScrollBar (N3d, NATIVE-DESKTOP-PLAN.md 27.8.5): the bar every scrolling widget shares -- the multi-line edit's two,
// and later the lists, tables, trees and scroll areas (N3e, N3f). Qt's QScrollBar in the measured subset: the panels
// read `value`/`maximum` of the console's bar (follow the end) and nothing else (1 use).
//
// The model is Qt's: an integer range [minimum, maximum] of the SCROLL POSITION (not of the content), a page step
// (what one page shows, also the thumb's share of the track) and a single step (one arrow key, one wheel notch is
// three). The thumb is `pageStep / (maximum - minimum + pageStep)` of the track, at least kMinThumb long.
// There are no arrow buttons: a Windows 11 scroll bar is a thin track and thumb; keys are the owner's.
//
// Behaviour: a press on the thumb grabs it (the value follows the pointer, keeping the grab offset); a press on the
// track pages toward the pointer, again every 75 ms after 500 ms while the button is held and the pointer has not
// reached the thumb; the wheel over the bar scrolls three single steps a notch. setValue clamps and reports (as
// QScrollBar, including for the program); setValueSilent does not. A bar with nothing to scroll (maximum == minimum)
// is drawn as an empty track, and `isNeeded()` is false so an owner can hide it.
// UI Automation: ScrollBar with RangeValue (value, range, small and large change).
#pragma once

#include "ui/core/layout.hpp"
#include "ui/core/widget.hpp"

#include <functional>

namespace tcad::ui {

class ScrollBar : public Widget {
public:
    std::function<void(int)> on_value_changed;

    explicit ScrollBar(Orientation o = Orientation::Vertical);

    Orientation orientation() const { return orientation_; }
    void setRange(int minimum, int maximum);  // maximum < minimum becomes minimum; the value is clamped
    int minimum() const { return min_; }
    int maximum() const { return max_; }
    void setPageStep(int s) { page_ = s < 1 ? 1 : s, update(); }
    void setSingleStep(int s) { single_ = s < 1 ? 1 : s; }
    int pageStep() const { return page_; }
    int singleStep() const { return single_; }
    void setValue(int v);
    void setValueSilent(int v);
    int value() const { return value_; }
    bool isNeeded() const { return max_ > min_; }
    bool atEnd() const { return value_ >= max_; }
    bool isSliderDown() const { return dragging_; }

    // Geometry in widget DIPs (painting and tests): the whole track and the thumb inside it.
    RectF trackRect() const;
    RectF thumbRect() const;
    int valueAtThumbStart(float pos) const;  // the value for a thumb whose start is at `pos` (DIPs along the bar), clamped

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    void hoverChanged(bool) override { update(); }

    Role accessibleRole() const override { return Role::ScrollBar; }
    AccessibleRange accessibleRange() const override;
    bool accessibleSetRangeValue(double v) override;

    static constexpr float kThickness = 14.0f;
    static constexpr float kMinThumb = 24.0f;
    static constexpr int kRepeatDelayMs = 500;
    static constexpr int kRepeatMs = 75;

private:
    bool setClamped(int v, bool notify);
    float along(PointF p) const { return orientation_ == Orientation::Vertical ? p.y : p.x; }
    float length() const;  // the bar's length along its axis, DIPs
    void pageToward(float pos);
    void stopRepeat();

    Orientation orientation_;
    int min_ = 0, max_ = 0, value_ = 0, single_ = 1, page_ = 10;
    bool dragging_ = false;
    float grab_ = 0;       // pointer minus the thumb's start, while dragging
    float held_pos_ = 0;   // the pointer along the bar while a track press repeats
    TimerId repeat_ = 0;
};

}  // namespace tcad::ui

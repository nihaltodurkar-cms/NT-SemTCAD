// QScrollArea (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7): 2 in the panels -- the 3D view's display controls (setWidgetResizable(true),
// no frame, one content widget). Not used, so not built: alignment of a small widget, a viewport widget replaced,
// scroll-bar policies other than "as needed", kinetic scrolling.
//
// A clipped viewport showing one content widget, with a vertical bar and a horizontal one as needed.
//   * resizable (setWidgetResizable(true), as the panel uses): the content is as wide as the viewport and as tall as its
//     height-for-width needs (at least the viewport's height), so a form reflows to the width and only scrolls
//     vertically; a bar's width is taken from the viewport only when the content then needs it. Not resizable: the
//     content keeps its size hint and the bars appear on either axis.
//   * the wheel scrolls three steps of 20 DIPs a notch (QScrollArea's single step); the bars' page is the viewport.
//   * focus follows: moving the keyboard focus (Tab) to a widget inside the content that is cut by or outside the viewport
//     scrolls it into view -- a form too tall for its window must stay usable by keyboard (QScrollArea does the same).
//   * ensureVisible(x, y, margin) and ensureWidgetVisible(w) scroll the least that shows the point or the widget.
//   * setFrame(false) draws no border (the panel's NoFrame).
// UI Automation: a Pane with the Scroll pattern (percentages, view size; Scroll and SetScrollPercent), and its bars.
#pragma once

#include "ui/core/widget.hpp"
#include "ui/widgets/scroll_bar.hpp"

#include <memory>

namespace tcad::ui {

class ScrollArea : public Widget {
public:
    ScrollArea();
    ~ScrollArea() override;

    Widget* setWidget(std::unique_ptr<Widget> w);  // replaces any content; returns the new one
    Widget* widget() const { return content_; }
    void setWidgetResizable(bool on);
    bool widgetResizable() const { return resizable_; }
    void setFrame(bool on);
    ScrollBar* verticalScrollBar() const { return vbar_; }
    ScrollBar* horizontalScrollBar() const { return hbar_; }
    int scrollY() const { return vbar_->value(); }
    int scrollX() const { return hbar_->value(); }
    RectF viewportRect() const;  // widget DIPs
    void ensureVisible(int x, int y, int margin = 0);  // a point in the content's coordinates
    void ensureWidgetVisible(const Widget* w, int margin = 0);

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return {48, 48}; }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;

    AccessibleScroll accessibleScroll() const override;
    void accessibleScrollBy(int h_amount, int v_amount) override;
    void accessibleSetScrollPercent(double h, double v) override;

    static constexpr int kStep = 20;

protected:
    void resized() override { relayout(); }

private:
    class Viewport;
    void relayout();
    void ensureFocusObserver();
    void focusMoved(Widget* now);
    Viewport* viewport_ = nullptr;
    Widget* content_ = nullptr;
    ScrollBar* vbar_ = nullptr;
    ScrollBar* hbar_ = nullptr;
    bool resizable_ = false;
    bool frame_ = true;
    bool observing_ = false;
    bool in_layout_ = false;
    float view_w_ = 0, view_h_ = 0;
    float content_w_ = 0, content_h_ = 0;
};

}  // namespace tcad::ui

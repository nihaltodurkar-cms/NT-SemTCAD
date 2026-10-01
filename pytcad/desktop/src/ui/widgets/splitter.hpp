// QSplitter (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7): 14 in the panels -- the build panel's horizontal and vertical splits, the
// catalog's, the main window's central one. What they call: addWidget, setSizes (the left column's width), sizes, the
// orientation, and (in code that walks up the parents) the count. Not used, so not built: nested-splitter state save and
// restore, setRubberBand, hidden children (a child is shown or removed), more than the default handle look.
//
// Children are laid out in a row (Horizontal) or a column (Vertical) with a HANDLE between each pair; the handles are
// widgets (UI Automation: a Splitter with a RangeValue -- the size of the child before it -- that a screen reader can set).
//   * sizes: setSizes gives the children's sizes in DIPs along the axis (the handles' own width is not part of them);
//     before it is called they are in proportion to the children's size hints. A resize of the splitter changes the
//     children in proportion to their stretch factors (setStretchFactor), or, when all are zero, to their sizes. A child
//     is never smaller than its minimum size hint, except a COLLAPSIBLE one (the default, Qt's) which snaps to nothing
//     when a drag takes it below half of that minimum, and comes back when dragged out.
//   * a drag moves the boundary between the two children beside the handle; with opaque resize (the default) they follow
//     the pointer; without it a line follows it and the children change when the button is released -- for a child that
//     is expensive to resize (the 3D view): on_drag_started/on_drag_finished bracket the drag either way.
//   * a handle is a Tab stop: the arrow keys along the axis move it 10 DIPs, PageUp/PageDown 50, Home/End to the ends.
//   * the cursor over a handle is the resize cursor.
#pragma once

#include "ui/core/layout.hpp"
#include "ui/core/widget.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace tcad::ui {

class Splitter;

class SplitterHandle : public Widget {
public:
    SplitterHandle(Splitter* s, int index) : splitter_(s), index_(index) {}
    int index() const { return index_; }
    void setIndex(int i) { index_ = i; }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    void hoverChanged(bool) override { update(); }
    void focusChanged(bool, FocusReason) override { update(); }
    Role accessibleRole() const override { return Role::Splitter; }
    AccessibleRange accessibleRange() const override;
    bool accessibleSetRangeValue(double v) override;
    bool isDragging() const { return dragging_; }

private:
    Splitter* splitter_;
    int index_;
    bool dragging_ = false;
    float start_pos_ = 0;
};

class Splitter : public Widget {
public:
    std::function<void(int handle)> on_splitter_moved;  // after the sizes changed by a drag or key
    std::function<void()> on_drag_started;
    std::function<void()> on_drag_finished;

    explicit Splitter(Orientation o = Orientation::Horizontal);

    Orientation orientation() const { return orientation_; }
    int addWidget(std::unique_ptr<Widget> w);
    int count() const { return static_cast<int>(children_w_.size()); }
    Widget* widget(int i) const { return i >= 0 && i < count() ? children_w_[static_cast<std::size_t>(i)] : nullptr; }
    SplitterHandle* handle(int i) const { return i >= 0 && i < static_cast<int>(handles_.size()) ? handles_[static_cast<std::size_t>(i)] : nullptr; }
    int indexOf(const Widget* w) const;

    std::vector<int> sizes() const;  // DIPs along the axis, rounded
    void setSizes(const std::vector<int>& dips);
    void setStretchFactor(int i, int factor);
    void setCollapsible(int i, bool on);
    bool isCollapsible(int i) const { return i >= 0 && i < count() && collapsible_[static_cast<std::size_t>(i)]; }
    void setChildrenCollapsible(bool on);
    bool isCollapsed(int i) const { return i >= 0 && i < count() && sizes_[static_cast<std::size_t>(i)] < 0.5f; }
    void setOpaqueResize(bool on) { opaque_ = on; }
    bool opaqueResize() const { return opaque_; }
    void setHandleWidth(float dips);
    float handleWidth() const { return handle_w_; }

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override;
    void paint(Painter& p) override;

    // used by the handles
    void beginDrag(int handle);
    void dragTo(int handle, float delta_dips, bool commit_now);  // from the sizes at beginDrag
    void endDrag(int handle);
    void moveHandleBy(int handle, float dips);                   // keys
    float sizeBefore(int handle) const { return sizes_[static_cast<std::size_t>(handle)]; }
    float minBefore(int handle) const;
    float maxBefore(int handle) const;
    float ghostPosition() const { return ghost_; }  // DIPs along the axis of the non-opaque drag's line, -1 none

    static constexpr float kDefaultHandle = 5.0f;

protected:
    void resized() override { layoutParts(); }

private:
    friend class SplitterHandle;
    float axisOf(const SizeF& s) const { return orientation_ == Orientation::Horizontal ? s.width : s.height; }
    float totalAvailable() const;  // DIPs for the children: the axis length less the handles
    float minSize(int i) const;
    void layoutParts();
    void distribute(float new_total);
    void applySizes(const std::vector<float>& s, int moved_handle);
    Orientation orientation_;
    std::vector<Widget*> children_w_;
    std::vector<SplitterHandle*> handles_;
    std::vector<float> sizes_;
    std::vector<int> stretch_;
    std::vector<bool> collapsible_;
    std::vector<float> drag_start_;  // the sizes when the drag began
    std::vector<float> pending_;     // what the drag would make them (applied at once when opaque, on release otherwise)
    Widget* ghost_w_ = nullptr;
    float handle_w_ = kDefaultHandle;
    float laid_total_ = -1;
    float ghost_ = -1;
    bool sizes_set_ = false;
    bool opaque_ = true;
    bool dragging_ = false;
    bool children_collapsible_ = true;
};

}  // namespace tcad::ui

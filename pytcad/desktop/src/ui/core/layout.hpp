// Layouts (N2b, NATIVE-DESKTOP-PLAN.md 27.7): Box (horizontal/vertical), Form, Grid, Stack.
//
// SCOPE -- the subset the Qt panels actually use, MEASURED in desktop/src on 2026-09-30, not all of Qt:
//   Box:   addWidget(w, stretch) / addLayout / addStretch(n), contents margins, spacing
//          (QVBoxLayout 39, QHBoxLayout 21 uses; stretch factors on 6 addWidget calls; 7 addStretch; 6 margins)
//   Form:  addRow(label, field), addRow(spanning widget), insertRow (QFormLayout 55; 87 addRow, 1 insertRow)
//   Grid:  cells with row/column spans (QGridLayout 6; one 1x3 span)
//   Stack: one current page (QStackedWidget 13)
//   Sizes: explicit minimum/maximum sizes, hidden widgets taking no space, and per-axis size policies limited to
//          Fixed / Minimum / Preferred / Expanding -- the defaults of the Qt widgets used. NOT built, because nothing
//          uses them: setSizePolicy calls, Maximum/MinimumExpanding/Ignored policies, alignment flags,
//          row/column stretch, form growth/wrap policies. Add one only when a port needs it.
//   Height-for-width (N3a, NATIVE-DESKTOP-PLAN.md 27.8.1): the 9 word-wrapped labels sit in vertical boxes, spanning
//          form rows and a stacked page's form, so Box, Form and Stack honour it; Grid does not (no wrapped label is
//          in a grid). Qt's rule: where a layout knows an item's width, a height-for-width item's minimum AND hint
//          height become heightForWidth(that width) -- a vertical box, a form row and a stack page all know it. A
//          nested layout or a widget holding one answers heightForWidthPx for the width its parent gives it.
//
// All arithmetic is in integer DEVICE PIXELS at the host's scale (hints are rounded up, margins/spacing to nearest),
// so every edge lands on a pixel and results are exact and testable.
//
// Distribution along a line (distribute(); Box main axis, Grid rows/columns), documented because it is the
// behaviour the tests pin:
//   * space <= sum of minimums: every item at its minimum (the rest is clipped);
//   * space <  sum of hints:    each item gives up part of (hint - minimum), in proportion to it;
//   * space >= sum of hints:    the extra goes to the items with stretch > 0 in proportion to their stretch; if none
//                               has a stretch, equally to the Expanding items; if none expands, equally to every
//                               item that can grow; items stop at their maximum and the rest is shared again;
//                               whole pixels left by rounding go one each to the first eligible items;
//   * an addStretch(n) spacer is an Expanding item with minimum/hint 0 and stretch n.
// Across the line an item gets the full breadth clamped to its maximum; a narrower item is centred vertically /
// kept at the left horizontally (the panels set no alignment, so this is the one rule).
#pragma once

#include "ui/core/widget.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace tcad::ui {

// One item's needs along one axis, in px.
struct AxisItem {
    int min = 0, hint = 0, max = kMaxPx;
    int stretch = 0;
    bool expanding = false;
};

// The sizes (px) of `items` in `space` px -- the rule above. Sizes sum to `space` unless every item is at its
// minimum (overflow) or at its maximum (underflow).
std::vector<int> distribute(const std::vector<AxisItem>& items, int space);

enum class Orientation { Horizontal, Vertical };

class Layout {
public:
    // A layout installed on `host` (Widget::setLayout) has the style's margins; one nested in another has none.
    explicit Layout(Widget* host);
    virtual ~Layout();
    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;

    Widget* host() const { return host_; }
    void setContentsMargins(float l, float t, float r, float b);
    Margins contentsMargins() const { return margins_; }
    void setSpacing(float dips);
    float spacing() const { return spacing_; }

    virtual SizeI minimumSizePx() const = 0;
    virtual SizeI sizeHintPx() const = 0;
    virtual SizeI maximumSizePx() const { return {kMaxPx, kMaxPx}; }
    virtual bool expanding(Orientation o) const = 0;
    virtual bool isEmpty() const = 0;         // nothing visible in it
    virtual void setGeometryPx(const RectI& r) = 0;  // host coordinates
    // Height-for-width (see the scope note): true when some visible item's height depends on its width; then the
    // height (px, margins included) the layout needs at `width` px (margins included).
    virtual bool hasHeightForWidth() const { return false; }
    virtual int heightForWidthPx(int width) const {
        (void)width;
        return sizeHintPx().height;
    }

protected:
    friend class BoxLayout;
    friend class GridLayout;
    double scale() const { return host_->scale(); }
    int spacingPx() const { return roundPx(spacing_, scale()); }
    int marginsH() const { return roundPx(margins_.left, scale()) + roundPx(margins_.right, scale()); }
    int marginsV() const { return roundPx(margins_.top, scale()) + roundPx(margins_.bottom, scale()); }
    RectI contentRect(const RectI& r) const;
    void changed();  // the host's geometry must be recomputed
    void markNested() { margins_ = {}; }

    // An item a box or grid places: a widget, a nested layout, or a stretch spacer.
    struct Item {
        Widget* widget = nullptr;
        std::unique_ptr<Layout> layout;
        int stretch = 0;
        bool spacer = false;
        static Item ofWidget(Widget* w, int stretch = 0) {
            Item i;
            i.widget = w;
            i.stretch = stretch;
            return i;
        }
        static Item ofSpacer(int stretch) {
            Item i;
            i.stretch = stretch;
            i.spacer = true;
            return i;
        }
        bool visible() const;
        // `main`: the box's direction (a spacer only expands along it)
        AxisItem axis(Orientation o, Orientation main, double scale) const;
        // Give the item `cell` (host coordinates), each axis clamped to its maximum per the rule above.
        void place(const RectI& cell, double scale) const;
        bool hasHeightForWidth() const;
        // The item's width inside a cell `cell_width` px wide (clamped to its maximum; the rule above).
        int widthIn(int cell_width, double scale) const;
        // Its vertical needs at `width` px: axis(Vertical) with min = hint = heightForWidth when it has one.
        AxisItem verticalAt(int width, Orientation main, double scale) const;
    };

    Widget* host_;
    Margins margins_;
    float spacing_;
};

class BoxLayout final : public Layout {
public:
    BoxLayout(Widget* host, Orientation o);

    // The widget must be a child of the host (Widget::addChild), or null to create one: see add<T>.
    void addWidget(Widget* w, int stretch = 0);
    template <class T, class... A>
    T* add(A&&... a) {
        T* w = host_->addChild<T>(std::forward<A>(a)...);
        addWidget(w);
        return w;
    }
    BoxLayout* addBox(Orientation o, int stretch = 0);  // a nested box (no margins)
    class FormLayout* addForm(int stretch = 0);
    class GridLayout* addGrid(int stretch = 0);
    void addStretch(int stretch = 0);
    Orientation orientation() const { return orientation_; }

    SizeI minimumSizePx() const override;
    SizeI sizeHintPx() const override;
    SizeI maximumSizePx() const override;
    bool expanding(Orientation o) const override;
    bool isEmpty() const override;
    void setGeometryPx(const RectI& r) override;
    bool hasHeightForWidth() const override;
    int heightForWidthPx(int width) const override;

private:
    template <class L, class... A>
    L* nest(int stretch, A&&... a);
    SizeI total(bool minimum) const;
    // The visible items and their main-axis needs for a content area `content_width` px wide (a vertical box's
    // height-for-width items depend on it).
    std::vector<AxisItem> mainAxes(int content_width, std::vector<const Item*>& vis, int& solid) const;

    Orientation orientation_;
    std::vector<Item> items_;
};

class FormLayout final : public Layout {
public:
    explicit FormLayout(Widget* host);

    void addRow(Widget* label, Widget* field);  // either may be null
    void addRow(Widget* spanning);              // one widget across both columns
    // QFormLayout::addRow(QString, field) (N3a): a Label child of the host with `field` as its buddy, so an '&' in
    // the text is Alt+<letter> to the field. Returns the label.
    class Label* addRow(std::string_view label, Widget* field);
    void insertRow(int index, Widget* label, Widget* field);
    int rowCount() const { return static_cast<int>(rows_.size()); }

    SizeI minimumSizePx() const override;
    SizeI sizeHintPx() const override;
    bool expanding(Orientation o) const override;
    bool isEmpty() const override;
    void setGeometryPx(const RectI& r) override;
    bool hasHeightForWidth() const override;
    int heightForWidthPx(int width) const override;

private:
    struct Row {
        Widget* label = nullptr;
        Widget* field = nullptr;
        bool spanning = false;
    };
    // One visible row placed in a content rectangle `c`: the label's and field's widths and the row's height, with
    // height-for-width applied.
    struct RowFit {
        int label_w = 0, field_x = 0, field_w = 0, height = 0;
    };
    RowFit fitRow(const Row& row, const RectI& c) const;
    bool rowVisible(const Row& r) const;
    int labelColumnPx(bool minimum) const;
    SizeI total(bool minimum) const;
    std::vector<Row> rows_;
};

class GridLayout final : public Layout {
public:
    explicit GridLayout(Widget* host);

    void addWidget(Widget* w, int row, int col, int row_span = 1, int col_span = 1);

    SizeI minimumSizePx() const override;
    SizeI sizeHintPx() const override;
    bool expanding(Orientation o) const override;
    bool isEmpty() const override;
    void setGeometryPx(const RectI& r) override;

private:
    struct Cell {
        Item item;
        int row, col, row_span, col_span;
    };
    // Per-row or per-column needs (index = row/column), with empty lines marked.
    std::vector<AxisItem> lines(Orientation o, std::vector<bool>& occupied) const;
    int extent(const std::vector<AxisItem>& lines, const std::vector<bool>& occupied, bool minimum) const;
    std::vector<Cell> cells_;
    int rows_ = 0, cols_ = 0;
};

class StackLayout final : public Layout {
public:
    explicit StackLayout(Widget* host);

    int addWidget(Widget* w);  // returns its index; the first becomes current
    void setCurrentIndex(int i);
    int currentIndex() const { return current_; }
    Widget* currentWidget() const;
    int count() const { return static_cast<int>(pages_.size()); }

    SizeI minimumSizePx() const override;
    SizeI sizeHintPx() const override;
    bool expanding(Orientation o) const override;
    bool isEmpty() const override;
    void setGeometryPx(const RectI& r) override;
    bool hasHeightForWidth() const override;
    int heightForWidthPx(int width) const override;  // the tallest page at that width (every page: no resize on switch)

private:
    std::vector<Widget*> pages_;
    int current_ = -1;
};

}  // namespace tcad::ui

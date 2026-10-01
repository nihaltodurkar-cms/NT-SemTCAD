// The column header band of the table and the tree (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6): a row of labels above the rows
// that follows the horizontal scroll, whose column edges can be dragged (and double-clicked to fit a column to its
// contents). Portable. The band asks its ColumnModel -- the view -- for everything: how many columns, their labels,
// where they are and how wide.
//
// Each label is a HeaderCell child widget (UI Automation's HeaderItem, named by its label), so the table's Table pattern
// can list them. Dragging an edge sets the column's width through the model, which clamps it.
#pragma once

#include "ui/widgets/item_view.hpp"

#include <string>

namespace tcad::ui {

class ColumnModel {
public:
    virtual ~ColumnModel() = default;
    virtual ItemView* headerView() = 0;  // the view whose horizontal scroll the band follows (and whose scale it uses)
    virtual int columnCount() const = 0;
    virtual std::string headerLabel(int col) const = 0;
    virtual float gutterWidth() const { return 0; }  // left of the first column (a table's row numbers)
    virtual float columnX(int col) const = 0;        // from the content's left edge, after the gutter
    virtual float columnWidth(int col) const = 0;
    virtual void setColumnWidth(int col, float dips) = 0;
    virtual void resizeColumnToContents(int col) = 0;
};

class HeaderCell : public Widget {
public:
    HeaderCell(ColumnModel*, int col) : col_(col) {}
    int column() const { return col_; }
    void setColumn(int c) { col_ = c; }
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::HeaderItem; }

private:
    int col_;
};

class ColumnHeaderBand : public Widget {
public:
    explicit ColumnHeaderBand(ColumnModel* m) : model_(m) {}
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    Role accessibleRole() const override { return Role::Header; }
    void sync();  // the cells: count, positions, labels

    static constexpr float kCellPad = 6.0f;

private:
    int separatorAt(float x) const;  // the column whose right edge is within 3 DIPs of band x, -1 none
    ColumnModel* model_;
    int drag_col_ = -1;
    float drag_start_x_ = 0, drag_start_w_ = 0;
};

}  // namespace tcad::ui

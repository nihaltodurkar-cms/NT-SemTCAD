// QTreeWidget (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6): ONE in the panels -- the info panel: two columns (Property, Value),
// group rows that span both columns and start expanded, leaf rows under them with a tool tip on the value, single
// selection, the first column fitted to its contents and the last stretched, a header. What it calls: setHeaderLabels,
// addTopLevelItem / child items from text lists, setExpanded, setFirstColumnSpanned, setToolTip(column, text),
// topLevelItem(i), clear. Not used, so not built: icons, check boxes, editing, sorting, drag and drop, item widgets,
// several selected items.
//
// Built on ItemView (item_view.hpp): the tree is flattened to the VISIBLE items, one row each, indented 16 DIPs a level
// with a disclosure triangle before the text of a parent. Items are owned by the view and addressed by TreeItem*,
// which stays valid until the item is removed or clear() is called.
//   * a click on a triangle, a double click on a parent, Right on a collapsed parent and + expand; Left on an expanded
//     one and - collapse; Right on an expanded parent moves to its first child; Left on a child moves to its parent;
//     * expands everything under the item. Collapsing a parent that holds the current item makes the parent current.
//   * the current item's change and every expand and collapse are reported (itemExpanded/itemCollapsed).
//   * the first column is wide enough for its widest visible text (Qt's ResizeToContents) unless a width is set; the last
//     column fills what is left (Qt's stretchLastSection); a header edge drags and double-clicks as the table's does.
// UI Automation: Tree with TreeItem rows (name = the texts, ExpandCollapse on parents, SelectionItem), Selection, Scroll.
// Stated limit: the rows are a FLAT list of the visible items, not nested elements, so a screen reader walks them in
// order and is told which are expanded or collapsed, but not their depth.
#pragma once

#include "ui/widgets/column_header.hpp"
#include "ui/widgets/item_view.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tcad::ui {

class TreeItem {
public:
    const std::string& text(int col = 0) const;
    const std::string& toolTip(int col = 0) const;
    TreeItem* parent() const { return parent_; }
    int childCount() const { return static_cast<int>(children_.size()); }
    TreeItem* child(int i) const { return i >= 0 && i < childCount() ? children_[static_cast<std::size_t>(i)].get() : nullptr; }
    bool isExpanded() const { return expanded_; }
    bool firstColumnSpanned() const { return span_first_; }
    int depth() const;

private:
    friend class TreeView;
    std::vector<std::string> texts_, tips_;
    TreeItem* parent_ = nullptr;
    std::vector<std::unique_ptr<TreeItem>> children_;
    bool expanded_ = false;
    bool span_first_ = false;
};

class TreeView : public ItemView, public ColumnModel {
public:
    std::function<void(TreeItem*)> on_current_item_changed;  // null: none
    std::function<void(TreeItem*)> on_item_expanded;
    std::function<void(TreeItem*)> on_item_collapsed;
    std::function<void(TreeItem*)> on_item_activated;        // Enter, or a double click on a leaf

    TreeView();

    // -- items
    TreeItem* addTopLevelItem(std::vector<std::string> texts);
    TreeItem* addItem(TreeItem* parent, std::vector<std::string> texts);
    void removeItem(TreeItem* item);  // and everything under it
    void clear();
    int topLevelItemCount() const { return static_cast<int>(roots_.size()); }
    TreeItem* topLevelItem(int i) const { return i >= 0 && i < topLevelItemCount() ? roots_[static_cast<std::size_t>(i)].get() : nullptr; }
    void setItemText(TreeItem* item, int col, std::string text);
    void setItemToolTip(TreeItem* item, int col, std::string tip);
    void setFirstColumnSpanned(TreeItem* item, bool on);
    void setExpanded(TreeItem* item, bool on);
    void expandAll();
    void collapseAll();
    TreeItem* currentItem() const { return itemAtRow(currentRow()); }
    void setCurrentItem(TreeItem* item);  // expands its ancestors; null clears
    int rowOfItem(const TreeItem* item) const;  // its visible row, -1 when hidden under a collapsed parent
    TreeItem* itemAtRow(int row) const { return row >= 0 && row < rowCount() ? visible_[static_cast<std::size_t>(row)] : nullptr; }

    // -- columns and header
    void setColumnCount(int n);
    void setHeaderLabels(std::vector<std::string> labels);
    void setHeaderVisible(bool on);
    void setFirstColumnFitsContents(bool on);
    void setStretchLastColumn(bool on);
    ColumnHeaderBand* headerBand() const { return band_; }
    ItemView* headerView() override { return this; }
    int columnCount() const override { return cols_; }
    std::string headerLabel(int col) const override;
    float columnX(int col) const override;
    float columnWidth(int col) const override;
    void setColumnWidth(int col, float dips) override;
    void resizeColumnToContents(int col) override;
    int columnAt(float view_x) const;

    int rowCount() const override { return static_cast<int>(visible_.size()); }
    Role accessibleRole() const override { return Role::Tree; }

    static constexpr float kIndent = 16.0f;
    static constexpr float kArrow = 16.0f;
    static constexpr float kPad = 4.0f;

protected:
    std::string rowText(int row) const override;
    std::string rowAccessibleName(int row) const override;
    Role rowRole(int) const override { return Role::TreeItem; }
    std::string rowToolTip(int row) const override;
    void paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) override;
    float contentWidth() const override;
    float headerHeight() const override { return header_visible_ ? rowHeight() : 0.0f; }
    void layoutHeader(const RectI& px) override;
    void horizontalScrolled() override { band_->sync(); }
    bool rowPressed(int row, const UiMouseEvent& e, float x) override;
    void rowDoubleClicked(int row, float x) override;
    bool rowKey(int row, const platform::KeyEvent& e) override;
    int rowExpandState(int row) const override;
    void rowExpand(int row, bool open) override;
    void currentChanged(int row) override;
    std::string copyTextFor(const std::vector<int>& rows) const override;

private:
    void rebuild(bool announce_to_view);  // visible_ from the items; the view told what changed
    void collectVisible(TreeItem* item, std::vector<TreeItem*>& out) const;
    void changeVisible(const std::vector<TreeItem*>& before, const std::vector<TreeItem*>& after);
    float firstColumnFit() const;
    float baseWidth(int col) const;
    void shapeChanged();
    void toggleRow(int row);
    TreeItem* make(std::vector<std::string> texts, TreeItem* parent);

    std::vector<std::unique_ptr<TreeItem>> roots_;
    std::vector<TreeItem*> visible_;
    ColumnHeaderBand* band_ = nullptr;
    int cols_ = 1;
    std::vector<std::string> headers_;
    std::vector<float> widths_;       // 0: not set (the first column fits, the others get kDefaultColumn)
    bool header_visible_ = true;
    bool fit_first_ = true;
    bool stretch_last_ = true;
    mutable float fit_cache_ = -1;

    static constexpr float kDefaultColumn = 100.0f;
    static constexpr float kMinColumn = 20.0f;
};

}  // namespace tcad::ui

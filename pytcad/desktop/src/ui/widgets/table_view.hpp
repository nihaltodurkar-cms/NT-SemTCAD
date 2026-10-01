// QTableWidget (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6): 4 in the panels -- the study panel's base-parameter table (a name
// column that is not editable, a value column that is), its split table (two editable columns, rows added and removed
// by buttons), its read-only run table (SelectRows; as many columns as the sweep has axes) and its read-only matrix.
// What they call: setRowCount/setColumnCount, insertRow/removeRow, setItem(row, col, text), item(row, col) text, header
// labels, stretch last section, resizeColumnsToContents, setEditTriggers(NoEditTriggers), setSelectionBehavior(
// SelectRows), per-cell flags (not editable) and tool tips, currentRow. Not used, so not built: sorting, header clicks,
// spans, cell widgets, icons, item delegates, editable headers, hidden rows or columns, drag and drop.
//
// Built on ItemView (item_view.hpp): a row is a realized row widget, so only the rows in view cost anything; each row
// holds one accessible cell widget per column (UI Automation's DataItem cells) and paints its cells itself.
//   * HEADERS: a column header band above the rows (the labels; drag a column's right edge to resize it, double-click
//     it to fit its contents) and a row-number gutter on the left (Qt's vertical header), both optional; the band follows
//     the horizontal scroll. Columns are 100 DIPs wide by default (Qt's); setStretchLastColumn widens the last one to fill.
//   * SELECTION BEHAVIOR: Cells (Qt's default): one current cell, a block selected by Shift+click, Shift+arrows or a drag,
//     Ctrl+A for all; Rows: whole rows, as the list (Ctrl/Shift extend). Ctrl+C copies the selection as tab-separated
//     text, one row a line (the clipboard's own line breaks are CR LF), through the host's clipboard.
//   * KEYS (cells): arrows, Home/End (Ctrl: the first/last cell), PageUp/PageDown, Tab and Shift+Tab walk the cells and
//     leave the table from the last/first one; F2, a double click, or typing a character edits the current cell in place
//     (the typed character replaces its text); Enter commits, Escape cancels, Tab commits and edits the next cell.
//   * EDITING needs the host's InlineEditor; a table with setEditTriggers(false), or a cell with setCellEditable(false),
//     is read-only. on_cell_changed(row, col) fires for a commit and for setItem (Qt's cellChanged), unless blocked.
// UI Automation: Table (Grid + Table patterns: sizes, GetItem, the column headers), DataItem rows with Text cells
// (GridItem + TableItem), Selection, Scroll. GetItem scrolls the row into view first, so any cell can be reached.
#pragma once

#include "ui/widgets/column_header.hpp"
#include "ui/widgets/item_view.hpp"

#include <string>
#include <vector>

namespace tcad::ui {

class TableView;

// One cell of one row: no paint (the row paints), an accessible element and a tool tip.
class CellWidget : public Widget {
public:
    CellWidget(TableView* t, int row, int col) : table_(t), row_(row), col_(col) {}
    void set(int row, int col) { row_ = row, col_ = col; }
    Role accessibleRole() const override { return Role::Text; }
    int accessibleSelectionState() const override;
    void accessibleSelect() override;
    Widget* accessibleSelectionContainer() const override;
    AccessibleCell accessibleCell() const override;
    void accessibleScrollIntoView() override;

private:
    TableView* table_;
    int row_, col_;
};

class TableView : public ItemView, public ColumnModel {
public:
    enum class SelectionBehavior { Cells, Rows };

    std::function<void(int, int)> on_cell_changed;          // (row, column): a commit or setItem
    std::function<void(int, int)> on_current_cell_changed;  // the current cell moved
    std::function<void(int, int)> on_cell_double_clicked;   // a double click on a cell that is not edited

    TableView();

    // -- shape and contents
    int rowCount() const override { return rows_; }
    int columnCount() const override { return cols_; }
    void setRowCount(int n);
    void setColumnCount(int n);
    void insertRow(int row);
    void removeRow(int row);
    void clearContents();                                       // the texts, not the shape
    void setItem(int row, int col, std::string text);
    const std::string& cellText(int row, int col) const;        // "" out of range
    void setCellToolTip(int row, int col, std::string tip);
    void setCellEditable(int row, int col, bool on);
    bool cellEditable(int row, int col) const;

    // -- headers and columns
    void setHeaderLabels(std::vector<std::string> labels);
    std::string headerLabel(int col) const override;
    void setHeaderVisible(bool on);
    void setRowHeadersVisible(bool on);
    void setColumnWidth(int col, float dips) override;
    float columnWidth(int col) const override;                           // as laid out (the stretched last one included)
    float columnX(int col) const override;                               // from the content's left edge, after the row gutter
    float gutterWidth() const override;
    void setStretchLastColumn(bool on);
    void resizeColumnsToContents();
    void resizeColumnToContents(int col) override;
    int columnAt(float view_x) const;                           // a view-local x -> a column, -1 outside
    RectF cellRectInRow(int col) const;                         // row-local DIPs (x from the row's left edge, gutter included)

    // -- editing and selection
    void setEditTriggers(bool on) { edit_triggers_ = on; }
    bool editTriggers() const { return edit_triggers_; }
    void setSelectionBehavior(SelectionBehavior b);
    SelectionBehavior selectionBehavior() const { return behavior_; }
    int currentColumn() const { return cur_col_; }
    void setCurrentCell(int row, int col);
    bool isCellSelected(int row, int col) const;
    struct Block {
        int row0 = -1, col0 = -1, row1 = -1, col1 = -1;  // inclusive, ordered
        bool valid() const { return row0 >= 0; }
    };
    Block selectedBlock() const;                                // Cells behavior; invalid when none
    void editCell(int row, int col);

    ColumnHeaderBand* headerBand() const { return band_; }
    ItemView* headerView() override { return this; }
    int columnCountForAccessibility() const { return cols_; }

    // -- UI Automation: the table
    Role accessibleRole() const override { return Role::Table; }
    bool accessibleIsGrid() const override { return true; }
    int accessibleRowCount() const override { return rows_; }
    int accessibleColumnCount() const override { return cols_; }
    Widget* accessibleGridCell(int row, int col) const override;
    std::vector<Widget*> accessibleColumnHeaders() const override;
    std::vector<Widget*> accessibleSelection() const override;
    bool accessibleCanSelectMultiple() const override { return true; }
    // used by the cell widgets
    int cellSelectionState(int row, int col) const;
    void cellSelect(int row, int col);
    std::string rowName(int row) const;

    bool wantsTabKey(bool forward) const override;
    bool charEvent(char32_t c) override;
    void focusChanged(bool in, FocusReason why) override;

    static constexpr float kDefaultColumn = 100.0f;
    static constexpr float kCellPad = 6.0f;
    static constexpr float kMinColumn = 20.0f;

protected:
    std::string rowText(int row) const override { return cellText(row, 0); }
    std::string rowAccessibleName(int row) const override { return rowName(row); }
    Role rowRole(int) const override { return Role::DataItem; }
    void paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) override;
    float contentWidth() const override;
    float headerHeight() const override { return header_visible_ ? rowHeight() : 0.0f; }
    void layoutHeader(const RectI& px) override;
    void horizontalScrolled() override { band_->sync(); }
    bool rowFillsSelection() const override { return behavior_ == SelectionBehavior::Rows; }
    bool rowFocusFrame() const override { return behavior_ == SelectionBehavior::Rows; }
    int rowSelectionState(int row) const override;
    bool rowPressed(int row, const UiMouseEvent& e, float x) override;
    bool rowDragged(int row, float x) override;
    void rowDoubleClicked(int row, float x) override;
    bool rowKey(int row, const platform::KeyEvent& e) override;
    void createRowChildren(ItemRow* r) override { updateRowChildren(r); }
    void updateRowChildren(ItemRow* r) override;
    AccessibleCell rowCell(int row) const override;
    bool canEdit(int row) const override;
    void startEditing(int row) override;
    void commitEdit(int row, int col, const std::string& text) override;
    void editTab(bool forward) override;
    bool hasSelection() const override;
    void currentChanged(int row) override;
    std::string copyTextFor(const std::vector<int>& rows) const override;

private:
    struct Cell {
        std::string text, tooltip;
        bool editable = true;
    };
    bool validCell(int r, int c) const { return r >= 0 && r < rows_ && c >= 0 && c < cols_; }
    void moveCurrentCell(int row, int col, bool extend);
    void setCurrentCellInternal(int row, int col, bool select);
    void shapeChanged();
    float baseWidth(int col) const;
    std::string cellsText(int r0, int c0, int r1, int c1) const;
    void beginCellEdit(int row, int col, const std::string& text, bool select_all);

    int rows_ = 0, cols_ = 0;
    std::vector<std::vector<Cell>> cells_;
    std::vector<std::string> headers_;
    std::vector<float> widths_;
    ColumnHeaderBand* band_ = nullptr;
    bool header_visible_ = true;
    bool row_headers_ = true;
    bool stretch_last_ = false;
    bool edit_triggers_ = true;
    SelectionBehavior behavior_ = SelectionBehavior::Cells;
    int cur_col_ = -1;
    int anchor_row_ = -1, anchor_col_ = -1;  // the block's fixed corner (Cells)
};

}  // namespace tcad::ui

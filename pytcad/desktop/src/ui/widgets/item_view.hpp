// The virtualized row engine under ListView, TableView and TreeView (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6). Portable.
//
// WHAT THE PANELS NEED, measured in desktop/src on 2026-10-01: 11 QListWidget, 4 QTableWidget, 1 QTreeWidget -- all with
// UNIFORM row heights, a few dozen to a few hundred rows (the console is not a list), single current row, selection by
// click and key. Not used, so not built: variable row heights, icons, item delegates, drag and drop (the 3 QDrag
// uses are file drops on the main window), sorting, filtering, models/views (QAbstractItemModel).
//
// THE ENGINE. Rows are uniform: row `i` is at y = i * rowHeight. Only the rows in (or touching) the viewport are
// REALIZED -- each is a real child widget (ItemRow) of an internal viewport widget, created and released as the view
// scrolls, so a thousand-row list costs a screenful of widgets. Each realized row is also its UI Automation element
// (a ListItem, TreeItem or DataItem with its name, selection, toggle and expand state), so UI Automation sees the
// realized rows -- the visible ones -- and scrolls to others through the Scroll pattern; that is a stated limit.
// The viewport clips what scrolls: a row cut by the edge is cut, and never paints over the header or the bars.
//
// BEHAVIOUR (Qt's unless said):
//   * current row and selection: SelectionMode None, Single (the current row is the selection) or Extended (Ctrl+click
//     toggles, Shift+click and Shift+arrows extend a range, Ctrl+A selects all, a drag selects a range). A row the
//     subclass calls not enabled can be neither current nor selected: keys skip it, a click on it does nothing.
//   * keys: Up/Down, PageUp/PageDown (a view of rows), Home/End move the current row (Shift extends; Ctrl moves it
//     without selecting); Enter reports row activated; F2 edits (when the subclass allows); Ctrl+A; Ctrl+C or Ctrl+Insert
//     copy the selection as text through the host's clipboard; typing selects the next row whose text starts with what
//     was typed (the same letter cycles; forgotten after a second).
//   * the wheel scrolls three rows a notch, over the view whether or not it has the focus (it changes no value).
//   * the current row has a focus frame while the view has the focus; selected rows are filled with the Selection
//     colour while it has the focus and with the alternate base while it does not (a selection stays visible), and in
//     high contrast the system highlight with highlight-text, or an outline when inactive (ui/core/selection.hpp).
//   * signals fire for the program's changes too (Qt); setSignalsBlocked(true) is QSignalBlocker.
//   * in-place editing: beginEdit opens the host's InlineEditor over a cell; Enter or leaving commits (the subclass's
//     commitEdit), Escape cancels. A host without an editor makes every view read-only.
#pragma once

#include "ui/core/inline_editor.hpp"
#include "ui/core/style.hpp"
#include "ui/core/widget.hpp"
#include "ui/widgets/scroll_bar.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace tcad::ui {

class ItemView;

// Everything a row's painting needs to know.
struct RowState {
    bool selected = false;   // in the selection
    bool current = false;    // the current row
    bool enabled = true;
    bool hovered = false;
    bool focused = false;    // the view has the focus
    Color text{};            // the row's text colour: Text, TextFaint when disabled, highlight-text on a high-contrast selection
};

// One realized row: a child of the view's viewport, and the row's UI Automation element.
class ItemRow : public Widget {
public:
    ItemRow(ItemView* view, int row) : view_(view), row_(row) {}
    int row() const { return row_; }
    void setRow(int r) { row_ = r; }
    ItemView* view() const { return view_; }

    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    void hoverChanged(bool) override { update(); }

    Role accessibleRole() const override;
    int accessibleSelectionState() const override;
    bool accessibleFocused() const override;
    bool accessibleFocusable() const override { return true; }
    void accessibleSelect() override;
    bool accessibleAddToSelection() override;
    bool accessibleRemoveFromSelection() override;
    Widget* accessibleSelectionContainer() const override;
    int accessibleToggleState() const override;
    void accessibleToggle() override;
    int accessibleExpandState() const override;
    void accessibleExpand(bool open) override;
    void accessibleScrollIntoView() override;
    bool accessibleReadOnly() const override { return true; }
    AccessibleCell accessibleCell() const override;

private:
    ItemView* view_;
    int row_;
};

// The clipped area the rows live in. Invisible to UI Automation (its rows appear as the view's children).
class ItemViewport : public Widget {
public:
    bool accessibleIsStructural() const override { return true; }
};

class ItemView : public Widget {
public:
    enum class SelectionMode { None, Single, Extended };

    std::function<void(int)> on_current_row_changed;   // the current row (-1: none)
    std::function<void()> on_selection_changed;
    std::function<void(int)> on_row_activated;         // double click, or Enter

    ItemView();
    ~ItemView() override;

    // -- current row and selection
    int currentRow() const { return current_; }
    void setCurrentRow(int row);          // -1 clears; a disabled or out-of-range row is ignored. Selects it in Single/Extended mode.
    void setCurrentRowSilent(int row);
    SelectionMode selectionMode() const { return mode_; }
    void setSelectionMode(SelectionMode m);
    bool isRowSelected(int row) const { return mode_ == SelectionMode::Single ? row == current_ : selected_.count(row) > 0; }
    std::vector<int> selectedRows() const;
    void selectRow(int row, bool on);     // Extended mode
    void selectRange(int from, int to);   // Extended: replaces the selection with [from, to]
    void selectAll();
    void clearSelection();
    void setSignalsBlocked(bool b) { blocked_ = b; }
    bool signalsBlocked() const { return blocked_; }

    // -- scrolling and geometry (DIPs)
    ScrollBar* verticalScrollBar() const { return vbar_; }
    ScrollBar* horizontalScrollBar() const { return hbar_; }
    int scrollY() const { return vbar_->value(); }
    int scrollX() const { return hbar_->value(); }
    void scrollToRow(int row);            // the least scrolling that shows it whole
    void scrollToTop() { vbar_->setValue(0); }
    float rowHeight() const;
    RectF viewportRect() const;           // widget DIPs: inside the frame, below the header, left of the bars
    RectF rowRect(int row) const;         // widget DIPs, scrolled (may lie outside the viewport)
    int rowAt(PointF local) const;        // a widget-local point -> a row, -1 outside the rows
    int firstVisibleRow() const;
    int lastVisibleRow() const;           // -1 when there are none
    int realizedRows() const { return static_cast<int>(rows_.size()); }
    ItemViewport* viewport() const { return viewport_; }
    ItemRow* realizedRow(int row) const;
    float contentHeight() const { return static_cast<float>(rowCount()) * rowHeight(); }
    const std::string& copyText();        // what Ctrl+C would copy (also stored in the clipboard by copy())
    bool copy();

    // -- in-place editing
    bool isEditing() const { return editor_ != nullptr; }
    void cancelEdit();

    SizeF sizeHint() const override { return {256, 192}; }  // QAbstractScrollArea's
    SizeF minimumSizeHint() const override { return {70, 70}; }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool charEvent(char32_t c) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    void focusChanged(bool, FocusReason) override;

    Role accessibleRole() const override { return Role::List; }  // a table and a tree say their own
    bool accessibleIsSelectionContainer() const override { return true; }
    bool accessibleFocused() const override { return hasFocus() && currentRow() < 0; }  // with a current item, IT has the focus
    Widget* accessibleFocusChild() const override { return realizedRow(current_); }
    std::vector<Widget*> accessibleSelection() const override;
    bool accessibleCanSelectMultiple() const override { return mode_ == SelectionMode::Extended; }
    bool accessibleSelectionRequired() const override { return false; }
    AccessibleScroll accessibleScroll() const override;
    void accessibleScrollBy(int h_amount, int v_amount) override;
    void accessibleSetScrollPercent(double h, double v) override;
    bool accessibleReadOnly() const override { return true; }

    static constexpr int kTypeaheadMs = 1000;
    static constexpr float kBorder = 1.0f;
    static constexpr float kRowPad = 6.0f;  // a row is its text's line plus this

    // -- used by ItemRow (public so the row can reach them; not a panel's API)
    void paintRowFor(Painter& p, int row, const SizeF& size, bool hovered);
    bool rowMouse(int row, const UiMouseEvent& e);
    Role rowRoleFor(int row) const { return rowRole(row); }
    int rowSelectionStateFor(int row) const { return rowSelectionState(row); }
    int rowToggleStateFor(int row) const { return rowToggleState(row); }
    void rowToggleFor(int row) { rowToggle(row); }
    int rowExpandStateFor(int row) const { return rowExpandState(row); }
    void rowExpandFor(int row, bool open) { rowExpand(row, open); }
    AccessibleCell rowCellFor(int row) const { return rowCell(row); }
    bool rowSelectable(int row) const { return row >= 0 && row < rowCount() && rowEnabled(row) && mode_ != SelectionMode::None; }
    void accessibleSelectRow(int row);
    bool accessibleAddRow(int row);
    bool accessibleRemoveRow(int row);

protected:
    // -- the subclass's model
    virtual int rowCount() const = 0;
    virtual std::string rowText(int row) const = 0;       // typeahead, copy and the screen reader's name by default
    virtual bool rowEnabled(int) const { return true; }
    virtual Role rowRole(int) const { return Role::ListItem; }
    virtual std::string rowAccessibleName(int row) const { return rowText(row); }
    virtual std::string rowToolTip(int) const { return {}; }
    // Paints the row's CONTENT in `size` (the selection fill and focus frame are the engine's, under and over it).
    virtual void paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) = 0;
    virtual float contentWidth() const { return 0; }       // wider than the viewport: a horizontal bar
    virtual float headerHeight() const { return 0; }
    virtual void layoutHeader(const RectI& /*px*/) {}      // place the header band (px, widget coordinates; scroll applied by the caller)
    virtual bool rowFillsSelection() const { return true; }
    virtual void currentChanged(int /*row*/) {}            // the current row changed (any path, signals blocked or not)
    virtual bool hasSelection() const { return !selectedRows().empty(); }  // something Ctrl+C would copy
    void repaintRows();
    virtual bool rowFocusFrame() const { return true; }    // the current row's frame (a table in cell mode frames the cell)
    virtual int rowSelectionState(int row) const { return rowSelectable(row) ? (isRowSelected(row) ? 1 : 0) : -1; }
    // A drag over `row` with the left button held; `x` view-local. True: handled (a table selecting cells).
    virtual bool rowDragged(int /*row*/, float /*x*/) { return false; }
    // A press on a row before selection logic: true = consumed (a check box, a disclosure arrow). `x`: view-local DIPs.
    virtual bool rowPressed(int /*row*/, const UiMouseEvent& /*e*/, float /*x*/) { return false; }
    virtual void rowDoubleClicked(int row, float /*x*/) { if (on_row_activated && !blocked_) on_row_activated(row); }
    virtual bool rowKey(int /*row*/, const platform::KeyEvent& /*e*/) { return false; }  // before the engine's keys
    virtual int rowToggleState(int) const { return -1; }
    virtual void rowToggle(int) {}
    virtual int rowExpandState(int) const { return -1; }
    virtual void rowExpand(int, bool) {}
    virtual AccessibleCell rowCell(int) const { return {}; }
    virtual void createRowChildren(ItemRow*) {}            // a table adds its cells
    virtual void updateRowChildren(ItemRow*) {}
    void refreshRealizedRows();                            // every realized row's name, tool tip and children
    virtual void horizontalScrolled() {}                   // the header follows
    virtual void editTab(bool /*forward*/) {}              // Tab in the editor, after its commit
    virtual bool canEdit(int /*row*/) const { return false; }
    virtual void startEditing(int /*row*/) {}
    virtual void commitEdit(int /*row*/, int /*col*/, const std::string& /*text*/) {}
    virtual std::string copyTextFor(const std::vector<int>& rows) const;  // default: the rows' texts, one a line

    // -- the subclass tells the engine what changed
    void rowsInserted(int first, int count);
    void rowsRemoved(int first, int count);
    void modelReset();
    void rowChanged(int row);        // its text or look: repaint, refresh the name
    void contentChangedWidth();      // contentWidth() changed
    void relayout();                 // geometry of the parts and the rows
    void beginEdit(int row, int col, const RectF& cell_in_row, const std::string& text, bool select_all = true);  // `cell_in_row`: row-local DIPs
    int editingColumn() const { return edit_col_; }
    float scrollOffsetX() const { return static_cast<float>(hbar_->value()); }
    void resized() override;
    void setCurrentInternal(int row, bool notify, bool select);
    void setCurrentAndSelect(int row, bool extend, bool toggle);
    int step(int from, int dir) const;     // the next enabled row in the direction, or -1

private:
    void layoutParts();
    void realize();
    void refreshRowLooks(ItemRow* r);
    void typeahead(const std::string& typed);
    void finishEdit(bool commit);
    void emitSelection();
    float viewH() const { return viewport_h_; }

    ItemViewport* viewport_ = nullptr;
    ScrollBar* vbar_ = nullptr;
    ScrollBar* hbar_ = nullptr;
    std::map<int, ItemRow*> rows_;
    int current_ = -1;
    int anchor_ = -1;
    std::set<int> selected_;
    SelectionMode mode_ = SelectionMode::Single;
    bool blocked_ = false;
    float viewport_w_ = 0, viewport_h_ = 0;
    std::string typed_;
    TimerId typed_timer_ = 0;
    std::string copy_text_;
    InlineEditor* editor_ = nullptr;  // owned by the viewport while it is open, then by graveyard_
    std::vector<std::unique_ptr<Widget>> graveyard_;
    int edit_row_ = -1, edit_col_ = 0;
    bool in_layout_ = false;
};

// A check box as the lists and trees draw it: `state` 0 off, 1 on, 2 partly; DIPs, in `box`.
void paintCheckMark(Painter& p, const RectF& box, int state, bool enabled);

}  // namespace tcad::ui

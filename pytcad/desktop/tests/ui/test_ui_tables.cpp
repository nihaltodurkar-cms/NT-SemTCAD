// Portable tests of N3e's table (NATIVE-DESKTOP-PLAN.md 27.8.6): shape and contents, headers and column widths, cell and row
// selection, keys, in-place editing, tab-separated copy, scrolling, painting and UI Automation state. Part of
// tcad_ui_core_tests: no Win32. The fake text engine gives 6 DIPs a byte and a 15-DIP line, so a row and the header are
// 21 DIPs; the row-number gutter is 30; columns are 100. A table at (10, 10) of 300 x 130 has its header band at window y
// 11..32 and row i centred at window y 42.5 + 21 i; column c at window x 91 + 100 c.
#include "mini_test.hpp"

#include "fake_ui.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/table_view.hpp"

using namespace tcad::ui;
using namespace tcad::ui::fake;
using tcad::desktop::theme::T;
namespace keys = tcad::ui::keys;

namespace {

struct Rig {
    Host h;
    TableView* t;
    Slider* after;  // a focusable widget after the table, for Tab to land on
    std::vector<std::string> log;
    Rig(int rows = 5, int cols = 2, float width = 300, float height = 130) {
        t = h.root.addChild<TableView>();
        after = h.root.addChild<Slider>();
        after->setGeometry({10, 200, 100, 20});
        t->setGeometry({10, 10, static_cast<int>(width), static_cast<int>(height)});
        t->setColumnCount(cols);
        t->setRowCount(rows);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) t->setItem(r, c, "r" + std::to_string(r) + "c" + std::to_string(c));
        t->on_cell_changed = [this](int r, int c) { log.push_back("changed " + std::to_string(r) + "," + std::to_string(c)); };
        t->on_current_cell_changed = [this](int r, int c) { log.push_back("cell " + std::to_string(r) + "," + std::to_string(c)); };
        t->on_cell_double_clicked = [this](int r, int c) { log.push_back("dbl " + std::to_string(r) + "," + std::to_string(c)); };
        t->on_row_activated = [this](int r) { log.push_back("activated " + std::to_string(r)); };
    }
    static float X(int col, float dx = 0) { return 91 + 100.0f * static_cast<float>(col) + dx; }
    static float Y(int row) { return 42.5f + 21.0f * static_cast<float>(row); }
    void click(int row, int col, Mod m = Mod::None, float dx = 0) {
        h.router.mouse(mouse(MouseType::Down, X(col, dx), Y(row), kLeftBit, m));
        h.router.mouse(mouse(MouseType::Up, X(col, dx), Y(row), 0, m));
    }
    void dblclick(int row, int col) {
        click(row, col);
        h.router.mouse(mouse(MouseType::DoubleClick, X(col), Y(row), kLeftBit));
        h.router.mouse(mouse(MouseType::Up, X(col), Y(row), 0));
    }
    void key(int vk, Mod m = Mod::None) { h.router.key(::tcad::ui::fake::key(vk, m)); }
    void type(const std::string& s) {
        for (char c : s) h.router.character(static_cast<char32_t>(c));
    }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
    std::string block() const {
        const auto b = t->selectedBlock();
        if (!b.valid()) return "none";
        return std::to_string(b.row0) + "," + std::to_string(b.col0) + "-" + std::to_string(b.row1) + "," + std::to_string(b.col1);
    }
    std::string cell() const { return std::to_string(t->currentRow()) + "," + std::to_string(t->currentColumn()); }
};

}  // namespace

// -- shape, contents, headers --------------------------------------------------------------------------------------

TEST(a_table_has_the_shape_and_texts_it_is_given) {
    Rig r(3, 2);
    CHECK(r.t->rowCount() == 3 && r.t->columnCount() == 2);
    CHECK_STR(r.t->cellText(1, 1), "r1c1");
    CHECK_STR(r.t->cellText(9, 9), "");
    CHECK(r.t->currentRow() == -1 && r.t->currentColumn() == -1);
    CHECK(r.t->cellEditable(0, 0) && !r.t->cellEditable(5, 5));
    r.t->setItem(9, 9, "ignored");
    r.t->setColumnCount(4);
    CHECK(r.t->columnCount() == 4 && r.t->cellText(1, 1) == "r1c1" && r.t->cellText(1, 3).empty());
    r.t->setColumnCount(1);
    CHECK(r.t->columnCount() == 1 && r.t->cellText(2, 0) == "r2c0");
    r.t->setRowCount(1);
    CHECK(r.t->rowCount() == 1);
    r.t->clearContents();
    CHECK(r.t->rowCount() == 1 && r.t->cellText(0, 0).empty());
    CHECK(r.t->accessibleRole() == Role::Table && r.t->sizeHint() == (SizeF{256, 192}));
}

TEST(headers_columns_and_the_row_number_gutter_have_qts_defaults) {
    Rig r(3, 3);
    r.t->setHeaderLabels({"Parameter", "Value"});  // fewer than the columns: the rest are empty
    CHECK_STR(r.t->headerLabel(0), "Parameter");
    CHECK_STR(r.t->headerLabel(2), "");
    CHECK(r.t->columnWidth(0) == 100 && r.t->columnWidth(2) == 100 && r.t->gutterWidth() == 30);
    CHECK(r.t->columnX(2) == 200);
    CHECK((r.t->headerBand()->geometry() == RectI{1, 1, 298, 21}));  // inside the frame, one row high
    CHECK(r.t->headerBand()->children().size() == 3);
    const auto& cells = r.t->headerBand()->children();
    CHECK((cells[0]->geometry() == RectI{30, 0, 100, 21}) && (cells[1]->geometry() == RectI{130, 0, 100, 21}));
    CHECK_STR(cells[0]->accessibleName, "Parameter");
    CHECK(cells[0]->accessibleRole() == Role::HeaderItem && r.t->headerBand()->accessibleRole() == Role::Header);
    CHECK((r.t->viewportRect() == RectF{1, 22, 298, 93}));  // below the band, above the sideways bar (30 + 300 wide columns do not fit)
    r.t->setHeaderVisible(false);
    CHECK((r.t->viewportRect() == RectF{1, 1, 298, 114}) && !r.t->headerBand()->isVisibleSelf());
    r.t->setHeaderVisible(true);
    r.t->setRowHeadersVisible(false);
    CHECK(r.t->gutterWidth() == 0 && r.t->columnX(0) == 0);
    CHECK((r.t->headerBand()->children()[0]->geometry() == RectI{0, 0, 100, 21}));
    r.t->setRowHeadersVisible(true);
    Rig big(120, 2);
    CHECK(big.t->gutterWidth() == 14 + 18 && big.t->gutterWidth() > 30);  // three digits: 18 + 14
}

TEST(a_row_is_a_widget_with_a_cell_widget_for_each_column) {
    Rig r(3, 2);
    ItemRow* row = r.t->realizedRow(1);
    CHECK(row != nullptr && row->children().size() == 2);
    if (!row) return;
    CHECK(row->accessibleRole() == Role::DataItem);
    CHECK_STR(row->accessibleName, "r1c0, r1c1");
    CHECK((row->geometry() == RectI{0, 21, 298, 21}));
    auto* c1 = static_cast<CellWidget*>(row->children()[1].get());
    CHECK((c1->geometry() == RectI{130, 0, 100, 21}));
    CHECK(c1->accessibleRole() == Role::Text);
    CHECK_STR(c1->accessibleName, "r1c1");
    const AccessibleCell ac = c1->accessibleCell();
    CHECK(ac.valid && ac.row == 1 && ac.column == 1 && ac.row_span == 1 && ac.grid == r.t);
    r.t->setCellToolTip(1, 1, "a tip");
    CHECK_STR(c1->toolTip, "a tip");
    r.t->setItem(1, 1, "changed");  // the accessible name follows the text
    CHECK_STR(c1->accessibleName, "changed");
    CHECK_STR(row->accessibleName, "r1c0, changed");
}

TEST(set_item_reports_a_cell_change_unless_blocked_or_the_same) {
    Rig r(2, 2);
    r.take();
    r.t->setItem(0, 0, "x");
    CHECK_STR(r.take(), "changed 0,0");
    r.t->setItem(0, 0, "x");
    CHECK_STR(r.take(), "");
    r.t->setSignalsBlocked(true);
    r.t->setItem(1, 1, "y");
    r.t->setSignalsBlocked(false);
    CHECK_STR(r.take(), "");
    CHECK_STR(r.t->cellText(1, 1), "y");
}

TEST(rows_inserted_and_removed_keep_the_current_cell_with_its_row) {
    Rig r(4, 2);
    r.click(2, 1);
    r.take();
    r.t->insertRow(0);
    CHECK(r.t->rowCount() == 5 && r.t->currentRow() == 3 && r.t->currentColumn() == 1 && r.t->cellText(3, 1) == "r2c1" && r.t->cellText(0, 0).empty());
    r.t->removeRow(0);
    CHECK(r.t->currentRow() == 2 && r.t->cellText(2, 1) == "r2c1");
    r.t->removeRow(2);  // the current row: its neighbour
    CHECK(r.t->currentRow() == 2 && r.t->rowCount() == 3);
    r.t->setRowCount(0);
    CHECK(r.t->currentRow() == -1 && r.t->realizedRows() == 0);
    r.t->setRowCount(2);
    CHECK(r.t->realizedRows() == 2 && r.t->cellText(0, 0).empty());
}

// -- columns -------------------------------------------------------------------------------------------------------

TEST(column_widths_are_set_clamped_stretched_and_fitted_to_contents) {
    Rig r(3, 2);
    r.t->setColumnWidth(0, 50);
    CHECK(r.t->columnWidth(0) == 50 && r.t->columnX(1) == 50);
    r.t->setColumnWidth(0, 5);  // never narrower than 20
    CHECK(r.t->columnWidth(0) == 20);
    r.t->setColumnWidth(9, 50);  // no such column
    r.t->setStretchLastColumn(true);  // 298 - 30 - 20 = 248 wide now
    CHECK(r.t->columnWidth(1) == 248 && r.t->columnX(1) == 20);
    CHECK((r.t->headerBand()->children()[1]->geometry() == RectI{50, 0, 248, 21}));
    CHECK(r.t->realizedRow(0)->children()[1]->geometry().width == 248);
    CHECK(!r.t->horizontalScrollBar()->isVisibleSelf());
    r.t->setStretchLastColumn(false);
    CHECK(r.t->columnWidth(1) == 100);
    r.t->setHeaderLabels({"Parameter name", "V"});
    r.t->setItem(1, 1, "a longer value");
    r.t->resizeColumnsToContents();  // header "Parameter name" = 14 bytes = 84 DIPs, plus 6 on each side
    CHECK(r.t->columnWidth(0) == 96);
    CHECK(r.t->columnWidth(1) == 14 * 6 + 12);  // "a longer value" is 14 bytes too
    r.t->resizeColumnToContents(1);
    CHECK(r.t->columnWidth(1) == 96);
}

TEST(a_wide_table_scrolls_sideways_and_its_header_follows) {
    Rig r(3, 5);  // 30 + 500 DIPs of columns in 298
    ScrollBar* hb = r.t->horizontalScrollBar();
    CHECK(hb->isNeeded() && !r.t->verticalScrollBar()->isVisibleSelf());
    const int max = hb->maximum();
    CHECK(max == 530 - 298);  // the viewport is 298 wide: no vertical bar takes any
    hb->setValue(100);
    CHECK((r.t->headerBand()->children()[0]->geometry() == RectI{-70, 0, 100, 21}));  // 30 - 100
    CHECK((r.t->realizedRow(0)->geometry() == RectI{-100, 0, 530, 21}));
    CHECK(r.t->realizedRow(0)->geometry().width == 530);
    hb->setValue(0);
    CHECK((r.t->headerBand()->children()[0]->geometry() == RectI{30, 0, 100, 21}));
}

TEST(dragging_a_header_edge_resizes_the_column_and_a_double_click_fits_it) {
    Rig r(3, 2);
    // the first column's right edge is at band x 130, window x 11 + 130 = 141; the band is at window y 11..32
    r.h.router.mouse(mouse(MouseType::Move, 141, 20, 0));
    CHECK(r.h.router.cursor() == Cursor::SizeWE);
    r.h.router.mouse(mouse(MouseType::Move, 160, 20, 0));
    CHECK(r.h.router.cursor() == Cursor::Arrow);
    r.h.router.mouse(mouse(MouseType::Down, 141, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, 166, 20, kLeftBit));
    CHECK(r.t->columnWidth(0) == 125);
    r.h.router.mouse(mouse(MouseType::Move, 40, 20, kLeftBit));  // 101 DIPs to the left of where it started: past the minimum
    CHECK(r.t->columnWidth(0) == 20);
    r.h.router.mouse(mouse(MouseType::Up, 40, 20, 0));
    r.h.router.mouse(mouse(MouseType::Move, 200, 20, kLeftBit));  // released: nothing
    const float w = r.t->columnWidth(0);
    CHECK(r.t->columnWidth(0) == w);
    // a press elsewhere on the band does nothing; a double click on an edge fits that column
    r.t->setItem(0, 0, "twelve chars");
    r.h.router.mouse(mouse(MouseType::Down, 100, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 100, 20, 0));
    CHECK(r.t->columnWidth(0) == w);
    const float edge = 11 + 30 + r.t->columnWidth(0);
    r.h.router.mouse(mouse(MouseType::DoubleClick, edge, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, edge, 20, 0));
    CHECK(r.t->columnWidth(0) == 12 * 6 + 12);
}

// -- cell selection --------------------------------------------------------------------------------------------------

TEST(a_click_makes_a_cell_current_and_selects_just_it) {
    Rig r(5, 2);
    r.click(2, 1);
    CHECK_STR(r.cell(), "2,1");
    CHECK_STR(r.block(), "2,1-2,1");
    CHECK_STR(r.take(), "cell 2,1");
    r.click(2, 1);
    CHECK_STR(r.take(), "");
    r.click(0, 0);
    CHECK_STR(r.take(), "cell 0,0");
    CHECK(r.t->isCellSelected(0, 0) && !r.t->isCellSelected(2, 1));
    CHECK(r.h.router.focusWidget() == r.t);
    // a click that only changes the column reports too
    r.click(0, 1);
    CHECK_STR(r.take(), "cell 0,1");
}

TEST(shift_click_and_drag_select_a_block_and_ctrl_click_does_not_toggle) {
    Rig r(5, 2);
    r.click(1, 0);
    r.click(3, 1, Mod::Shift);
    CHECK_STR(r.block(), "1,0-3,1");
    CHECK(r.t->isCellSelected(2, 1) && !r.t->isCellSelected(0, 0));
    r.click(0, 0, Mod::Shift);  // the corner stays at (1, 0)
    CHECK_STR(r.block(), "0,0-1,0");
    r.click(2, 1);
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(0), Rig::Y(1), kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(1), Rig::Y(3), kLeftBit));
    CHECK_STR(r.block(), "1,0-3,1");
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(0), Rig::Y(2), kLeftBit));
    CHECK_STR(r.block(), "1,0-2,0");
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(0), Rig::Y(2), 0));
}

TEST(clicking_a_row_number_selects_the_whole_row) {
    Rig r(5, 3);
    r.h.router.mouse(mouse(MouseType::Down, 10 + 1 + 15, Rig::Y(2), kLeftBit));  // inside the 30-DIP gutter
    r.h.router.mouse(mouse(MouseType::Up, 10 + 1 + 15, Rig::Y(2), 0));
    CHECK_STR(r.block(), "2,0-2,2");
    CHECK(r.t->currentRow() == 2);
}

TEST(arrow_keys_move_the_current_cell_and_shift_extends_the_block) {
    Rig r(5, 3);
    r.t->setFocus(FocusReason::Mouse);
    r.click(1, 1);
    r.take();
    r.key(keys::Right);
    CHECK_STR(r.cell(), "1,2");
    r.key(keys::Right);
    CHECK_STR(r.cell(), "1,2");  // the last column: stays
    r.key(keys::Down);
    r.key(keys::Left);
    CHECK_STR(r.cell(), "2,1");
    CHECK_STR(r.block(), "2,1-2,1");
    r.key(keys::Up);
    r.key(keys::Up);
    r.key(keys::Up);
    CHECK_STR(r.cell(), "0,1");
    r.key(keys::Home);
    CHECK_STR(r.cell(), "0,0");
    r.key(keys::End);
    CHECK_STR(r.cell(), "0,2");
    r.key(keys::End, Mod::Ctrl);
    CHECK_STR(r.cell(), "4,2");
    r.key(keys::Home, Mod::Ctrl);
    CHECK_STR(r.cell(), "0,0");
    r.key(keys::Down, Mod::Shift);
    r.key(keys::Right, Mod::Shift);
    CHECK_STR(r.block(), "0,0-1,1");
    r.key(keys::Up, Mod::Shift);
    CHECK_STR(r.block(), "0,0-0,1");
    r.key(keys::Down);  // a plain move collapses it
    CHECK_STR(r.block(), "1,1-1,1");
    r.key('A', Mod::Ctrl);
    CHECK_STR(r.block(), "0,0-4,2");
    CHECK(r.t->accessibleCanSelectMultiple());
}

TEST(page_keys_move_by_a_view_of_rows) {
    Rig r(40, 2);
    r.t->setFocus(FocusReason::Mouse);
    r.click(0, 0);
    r.key(keys::PageDown);  // viewport 107 -> 5 rows, less one
    CHECK(r.t->currentRow() == 4);
    r.key(keys::PageDown);
    CHECK(r.t->currentRow() == 8);
    r.key(keys::PageUp);
    CHECK(r.t->currentRow() == 4);
    r.key(keys::End, Mod::Ctrl);
    CHECK(r.t->currentRow() == 39 && r.t->scrollY() == r.t->verticalScrollBar()->maximum());
}

TEST(entering_by_tab_selects_the_first_cell_and_tab_walks_the_cells_then_leaves) {
    Rig r(2, 2);
    r.t->setFocus(FocusReason::Tab);
    CHECK_STR(r.cell(), "0,0");  // Qt: a table entered by Tab has a current cell
    CHECK(r.t->wantsTabKey(true) && !r.t->wantsTabKey(false));  // at the very first cell, Shift+Tab leaves
    r.key(keys::Tab);
    CHECK_STR(r.cell(), "0,1");
    r.key(keys::Tab);
    CHECK_STR(r.cell(), "1,0");  // the next row
    r.key(keys::Tab);
    CHECK_STR(r.cell(), "1,1");
    CHECK(!r.t->wantsTabKey(true) && r.t->wantsTabKey(false));  // at the last: Tab leaves, Shift+Tab walks back
    r.key(keys::Tab);
    CHECK(r.after->hasFocus());
    r.after->setFocus(FocusReason::Backtab);
    r.t->setFocus(FocusReason::Other);
    r.key(keys::Tab, Mod::Shift);
    CHECK_STR(r.cell(), "1,0");
    CHECK(!r.after->hasFocus() && r.t->hasFocus());
}

// -- row behavior ---------------------------------------------------------------------------------------------------

TEST(in_row_mode_rows_are_selected_like_a_list) {
    Rig r(5, 3);
    r.t->setSelectionBehavior(TableView::SelectionBehavior::Rows);
    CHECK(r.t->selectionBehavior() == TableView::SelectionBehavior::Rows);
    r.click(1, 2);
    CHECK(r.t->currentRow() == 1 && r.t->selectedRows() == std::vector<int>{1});
    r.click(3, 0, Mod::Shift);
    CHECK((r.t->selectedRows() == std::vector<int>{1, 2, 3}));
    r.click(0, 1, Mod::Ctrl);
    CHECK((r.t->selectedRows() == std::vector<int>{0, 1, 2, 3}));
    CHECK(r.t->isCellSelected(2, 1) && !r.t->isCellSelected(4, 1));
    CHECK(!r.t->selectedBlock().valid());
    CHECK(r.t->realizedRow(2)->accessibleSelectionState() == 1);
    CHECK(static_cast<CellWidget*>(r.t->realizedRow(2)->children()[0].get())->accessibleSelectionState() == -1);  // rows, not cells
}

TEST(in_row_mode_left_and_right_scroll_a_wide_table) {
    Rig r(3, 5);
    r.t->setSelectionBehavior(TableView::SelectionBehavior::Rows);
    r.click(1, 0);
    r.key(keys::Right);
    CHECK(r.t->scrollX() == 20);
    r.key(keys::Left);
    r.key(keys::Left);
    CHECK(r.t->scrollX() == 0);
}

// -- editing --------------------------------------------------------------------------------------------------------

TEST(f2_a_double_click_and_typing_edit_the_current_cell_in_place) {
    Rig r(3, 2);
    r.click(1, 1);
    r.take();
    r.key(keys::F2);
    CHECK(r.t->isEditing() && r.h.last_editor != nullptr);
    Editor* ed = r.h.last_editor;
    if (!ed) return;
    CHECK_STR(ed->value, "r1c1");
    CHECK(ed->selected_all);
    CHECK((ed->geometry() == RectI{130, 21, 100, 21}));  // the cell, in the viewport: gutter 30 + column 100 across, row 1 down
    ed->setText("edited");
    r.h.router.key(key(keys::Return));
    CHECK(!r.t->isEditing() && r.t->cellText(1, 1) == "edited");
    CHECK_STR(r.take(), "changed 1,1");
    CHECK(r.h.router.focusWidget() == r.t);
    r.dblclick(0, 0);  // a double click edits too
    CHECK(r.t->isEditing());
    r.h.last_editor->setText("nope");
    r.h.router.key(key(keys::Escape));
    CHECK(!r.t->isEditing() && r.t->cellText(0, 0) == "r0c0");
    r.take();
    // typing a character starts editing with that character in place of the text, not selected
    r.click(2, 0);
    r.take();
    r.type("q");
    CHECK(r.t->isEditing() && r.h.last_editor->value == "q" && !r.h.last_editor->selected_all);
    r.h.last_editor->setText("qwerty");
    r.h.router.key(key(keys::Return));
    CHECK_STR(r.t->cellText(2, 0), "qwerty");
    CHECK_STR(r.take(), "changed 2,0");
    // the same text again is no change
    r.key(keys::F2);
    r.h.router.key(key(keys::Return));
    CHECK_STR(r.take(), "");
}

TEST(a_cell_that_is_not_editable_or_a_table_with_edit_triggers_off_is_read_only) {
    Rig r(3, 2);
    r.t->setCellEditable(1, 0, false);
    r.click(1, 0);
    r.take();
    r.key(keys::F2);
    r.type("z");
    CHECK(!r.t->isEditing() && r.h.editors_made == 0);
    r.dblclick(1, 0);
    CHECK(!r.t->isEditing());
    CHECK_STR(r.take(), "dbl 1,0, activated 1");  // an ordinary double click
    r.click(1, 1);
    r.take();
    r.t->setEditTriggers(false);
    r.key(keys::F2);
    r.type("z");
    CHECK(!r.t->isEditing() && r.h.editors_made == 0 && r.t->cellText(1, 1) == "r1c1");
    r.t->setEditTriggers(true);
    r.h.editing_allowed = false;
    r.key(keys::F2);
    CHECK(!r.t->isEditing());
}

TEST(tab_in_the_editor_commits_and_edits_the_next_editable_cell) {
    Rig r(2, 3);
    r.t->setCellEditable(0, 1, false);
    r.click(0, 0);
    r.t->editCell(0, 0);
    CHECK(r.t->isEditing());
    r.h.last_editor->setText("one");
    r.h.last_editor->on_tab(true);  // (the editor reports Tab instead of finishing)
    CHECK_STR(r.t->cellText(0, 0), "one");
    CHECK(r.t->isEditing() && r.t->currentRow() == 0 && r.t->currentColumn() == 2);  // (0, 1) is read-only: skipped
    r.h.last_editor->setText("two");
    r.h.last_editor->on_tab(true);
    CHECK(r.t->cellText(0, 2) == "two" && r.t->isEditing() && r.t->currentRow() == 1 && r.t->currentColumn() == 0);  // the next row
    r.h.last_editor->on_tab(false);  // back, uncommitted text the same
    CHECK(r.t->currentRow() == 0 && r.t->currentColumn() == 2 && r.t->isEditing());
    r.h.router.key(key(keys::Escape));
    r.t->editCell(1, 2);
    r.h.last_editor->on_tab(true);  // past the last cell: editing stops
    CHECK(!r.t->isEditing());
}

// -- copy -----------------------------------------------------------------------------------------------------------

TEST(copy_puts_the_selected_block_on_the_clipboard_tab_separated) {
    Rig r(4, 3);
    r.t->setItem(1, 1, "has\ttab\nand line");
    r.take();
    r.click(1, 0);
    r.click(2, 1, Mod::Shift);
    r.key('C', Mod::Ctrl);
    CHECK(r.h.board.value == std::optional<std::string>("r1c0\thas tab and line\nr2c0\tr2c1"));  // a cell stays one line
    r.h.board.value.reset();
    r.click(3, 2);
    r.key(keys::Insert, Mod::Ctrl);
    CHECK(r.h.board.value == std::optional<std::string>("r3c2"));
    r.t->setSelectionBehavior(TableView::SelectionBehavior::Rows);
    r.click(0, 0);
    r.click(1, 0, Mod::Shift);
    r.h.board.value.reset();
    r.key('C', Mod::Ctrl);
    CHECK(r.h.board.value == std::optional<std::string>("r0c0\tr0c1\tr0c2\nr1c0\thas tab and line\tr1c2"));
    r.t->setCurrentCell(-1, -1);
    r.h.board.value.reset();
    CHECK(!r.t->copy() && !r.h.board.value);
}

// -- scrolling, virtualization --------------------------------------------------------------------------------------

TEST(a_long_table_realizes_only_the_visible_rows_and_the_header_stays) {
    Rig r(500, 3);
    CHECK(r.t->verticalScrollBar()->isVisibleSelf());
    CHECK(r.t->realizedRows() <= 6 && r.t->realizedRows() >= 5);
    ItemRow* row = r.t->realizedRow(0);
    CHECK(row != nullptr && row->children().size() == 3);
    CHECK((r.t->headerBand()->geometry() == RectI{1, 1, 284, 21}));  // the bar takes 14 of the width, not the band's place
    r.t->verticalScrollBar()->setValue(2100);
    CHECK(r.t->realizedRow(100) != nullptr && r.t->realizedRow(0) == nullptr);
    CHECK_STR(r.t->realizedRow(100)->children()[1]->accessibleName, "r100c1");
    CHECK((r.t->headerBand()->geometry() == RectI{1, 1, 284, 21}));  // the header does not scroll
    r.click(0, 1);  // row 100 is at the top now
    CHECK_STR(r.cell(), "100,1");
}

TEST(moving_the_current_cell_to_a_column_off_screen_scrolls_it_into_view) {
    Rig r(3, 6);  // 30 + 600
    r.click(0, 0);
    r.t->setFocus(FocusReason::Mouse);
    for (int i = 0; i < 5; ++i) r.key(keys::Right);
    CHECK_STR(r.cell(), "0,5");
    CHECK(r.t->scrollX() == 630 - 298);  // the right edge of the last column at the viewport's
    r.key(keys::Home);
    CHECK(r.t->scrollX() == 0);
}

// -- painting -------------------------------------------------------------------------------------------------------

TEST(a_row_paints_its_number_its_cells_a_grid_and_a_filled_selection) {
    Rig r(3, 2);
    r.click(1, 1);
    r.click(2, 0, Mod::Shift);
    RecordingPainter p;
    r.t->realizedRow(1)->paint(p);
    int sel = 0;
    bool number = false, cell_text = false;
    for (const auto& op : p.ops()) {
        if (op.kind == "fillRect" && op.color.b == token(T::Selection).b) {
            ++sel;
            CHECK(op.rect.x >= 30);  // never in the gutter
        }
        if (op.kind == "text" && op.text == "2") number = true;
        if (op.kind == "text" && op.text == "r1c1") cell_text = op.rect.x == 130 + 6;
    }
    CHECK(sel == 2 && number && cell_text);  // block rows 1..2, columns 0..1: both cells of row 1
    // the current cell has a frame while the table has the focus
    bool frame = false;
    for (const auto& op : p.ops()) frame = frame || (op.kind == "strokeRect" && op.color.g == token(T::Focus).g);
    CHECK(!frame || r.t->hasFocus());
    RecordingPainter q;
    r.t->realizedRow(0)->paint(q);
    int fills = 0;
    for (const auto& op : q.ops()) fills += op.kind == "fillRect" && op.color.b == token(T::Selection).b;
    CHECK(fills == 0);  // row 0 is outside the block
}

TEST(a_selected_cell_is_inactive_coloured_without_the_focus_and_in_high_contrast_outlined) {
    Rig r(2, 2);
    r.click(0, 0);
    r.t->clearFocus();
    RecordingPainter p;
    r.t->realizedRow(0)->paint(p);
    bool inactive = false;
    for (const auto& op : p.ops()) inactive = inactive || (op.kind == "fillRect" && op.color.b == token(T::AlternateBase).b && op.rect.width == 100);
    CHECK(inactive);
    const HighContrast saved = highContrast();
    highContrast() = {true, Color::rgb(0x000000), Color::rgb(0xFFFFFF), Color::rgb(0x1AEBFF), Color::rgb(0x000000), Color::rgb(0x3FF23F)};
    RecordingPainter q;
    r.t->realizedRow(0)->paint(q);
    int outlines = 0;
    for (const auto& op : q.ops()) outlines += op.kind == "strokeRect" && op.rect.width > 90 && op.rect.width < 101;
    CHECK(outlines == 1);
    r.t->setFocus(FocusReason::Mouse);
    RecordingPainter a;
    r.t->realizedRow(0)->paint(a);
    bool black = false;
    for (const auto& op : a.ops()) black = black || (op.kind == "text" && op.text == "r0c0" && op.style.color.r == 0.0f);
    CHECK(black);
    highContrast() = saved;
}

// -- UI Automation --------------------------------------------------------------------------------------------------

TEST(the_table_is_a_grid_with_cells_by_position_and_column_headers) {
    Rig r(60, 3);
    r.t->setHeaderLabels({"A", "B", "C"});
    CHECK(r.t->accessibleIsGrid() && r.t->accessibleRowCount() == 60 && r.t->accessibleColumnCount() == 3);
    Widget* c = r.t->accessibleGridCell(2, 1);
    CHECK(c != nullptr);
    if (c) {
        CHECK_STR(c->accessibleName, "r2c1");
        const AccessibleCell ac = c->accessibleCell();
        CHECK(ac.valid && ac.row == 2 && ac.column == 1 && ac.grid == r.t);
    }
    // a row that is not realized is brought into view first
    CHECK(r.t->realizedRow(50) == nullptr);
    Widget* far = r.t->accessibleGridCell(50, 2);
    CHECK(far != nullptr && r.t->realizedRow(50) != nullptr);
    if (far) CHECK_STR(far->accessibleName, "r50c2");
    CHECK(r.t->accessibleGridCell(60, 0) == nullptr && r.t->accessibleGridCell(0, 3) == nullptr && r.t->accessibleGridCell(-1, 0) == nullptr);
    const auto headers = r.t->accessibleColumnHeaders();
    CHECK(headers.size() == 3 && headers[1]->accessibleName == "B");
}

TEST(cells_report_and_take_selection_through_ui_automation) {
    Rig r(5, 3);
    r.click(1, 1);
    r.click(2, 2, Mod::Shift);
    Widget* a = r.t->accessibleGridCell(1, 1);
    Widget* b = r.t->accessibleGridCell(0, 0);
    CHECK(a && b && a->accessibleSelectionState() == 1 && b->accessibleSelectionState() == 0);
    CHECK(a->accessibleSelectionContainer() == r.t);
    CHECK(r.t->accessibleSelection().size() == 4);  // 2 x 2
    b->accessibleSelect();
    CHECK_STR(r.cell(), "0,0");
    CHECK_STR(r.block(), "0,0-0,0");
    CHECK(r.t->accessibleSelection().size() == 1);
}

TEST(a_table_destroyed_with_an_editor_open_and_a_header_drag_is_safe) {
    auto host = std::make_unique<Host>();
    auto* t = host->root.addChild<TableView>();
    t->setGeometry({10, 10, 300, 130});
    t->setColumnCount(2);
    t->setRowCount(3);
    t->editCell(1, 1);
    CHECK(t->isEditing());
    host->router.mouse(mouse(MouseType::Down, 141, 20, kLeftBit));  // holding a header separator
    host->root.release(t);
    host->router.mouse(mouse(MouseType::Move, 150, 20, kLeftBit));
    host->router.mouse(mouse(MouseType::Up, 150, 20, 0));
    CHECK(host->root.children().empty());
}

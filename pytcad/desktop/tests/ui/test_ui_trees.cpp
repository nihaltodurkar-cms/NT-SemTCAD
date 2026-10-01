// Portable tests of N3e's tree (NATIVE-DESKTOP-PLAN.md 27.8.6): items and the flattened visible rows, expanding and
// collapsing (mouse, keys, program) with the current item's rules, columns and the header, painting, copy, and UI
// Automation state. Part of tcad_ui_core_tests: no Win32. Fake text engine: 6 DIPs a byte, a 15-DIP line, so a row and the
// header are 21 DIPs. A tree at (10, 10) of 300 x 250 has its header at window y 11..32 and row i centred at
// y 42.5 + 21 i; indentation is 16 DIPs a level, the disclosure box 16 wide starting 4 in.
#include "mini_test.hpp"

#include "fake_ui.hpp"
#include "ui/widgets/tree_view.hpp"

using namespace tcad::ui;
using namespace tcad::ui::fake;
using tcad::desktop::theme::T;
namespace keys = tcad::ui::keys;

namespace {

// The info panel's shape: groups that span both columns, leaves with a value.
struct Rig {
    Host h;
    TreeView* t;
    TreeItem *file, *mesh, *name, *size, *dims;
    std::vector<std::string> log;
    Rig() {
        t = h.root.addChild<TreeView>();
        t->setGeometry({10, 10, 300, 250});
        t->setColumnCount(2);
        t->setHeaderLabels({"Property", "Value"});
        file = t->addTopLevelItem({"File"});
        t->setFirstColumnSpanned(file, true);
        name = t->addItem(file, {"Name", "mosfet_2d.npz"});
        size = t->addItem(file, {"Size", "1.2 MB"});
        mesh = t->addTopLevelItem({"Mesh"});
        t->setFirstColumnSpanned(mesh, true);
        dims = t->addItem(mesh, {"Dimensionality", "2D"});
        t->addItem(mesh, {"Nodes", "64 x 32 = 2048"});
        t->setExpanded(file, true);
        t->setExpanded(mesh, true);
        t->on_current_item_changed = [this](TreeItem* i) { log.push_back("current " + (i ? i->text(0) : std::string("none"))); };
        t->on_item_expanded = [this](TreeItem* i) { log.push_back("expanded " + i->text(0)); };
        t->on_item_collapsed = [this](TreeItem* i) { log.push_back("collapsed " + i->text(0)); };
        t->on_item_activated = [this](TreeItem* i) { log.push_back("activated " + i->text(0)); };
        take();
    }
    static float Y(int row) { return 42.5f + 21.0f * static_cast<float>(row); }
    // x inside the text of a row at `depth`
    static float X(int depth, float dx = 30) { return 11 + 4 + 16.0f * static_cast<float>(depth) + dx; }
    void click(int row, float x, Mod m = Mod::None) {
        h.router.mouse(mouse(MouseType::Down, x, Y(row), kLeftBit, m));
        h.router.mouse(mouse(MouseType::Up, x, Y(row), 0, m));
    }
    void dblclick(int row, float x) {
        click(row, x);
        h.router.mouse(mouse(MouseType::DoubleClick, x, Y(row), kLeftBit));
        h.router.mouse(mouse(MouseType::Up, x, Y(row), 0));
    }
    void key(int vk, Mod m = Mod::None) { h.router.key(::tcad::ui::fake::key(vk, m)); }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
    std::string rows() const {
        std::string s;
        for (int i = 0; i < t->rowCount(); ++i) s += (i ? "|" : "") + t->itemAtRow(i)->text(0);
        return s;
    }
};

}  // namespace

// -- items and the visible rows ----------------------------------------------------------------------------------

TEST(an_empty_tree_has_no_rows_and_one_default_column) {
    Host h;
    auto* t = h.root.addChild<TreeView>();
    t->setGeometry({10, 10, 300, 250});
    CHECK(t->rowCount() == 0 && t->columnCount() == 1 && t->topLevelItemCount() == 0 && t->currentItem() == nullptr);
    CHECK(t->accessibleRole() == Role::Tree && t->sizeHint() == (SizeF{256, 192}));
    CHECK(t->itemAtRow(0) == nullptr && t->topLevelItem(0) == nullptr);
    CHECK_STR(t->headerLabel(0), "");
}

TEST(only_the_visible_items_are_rows_and_expanding_and_collapsing_changes_them) {
    Rig r;
    CHECK_STR(r.rows(), "File|Name|Size|Mesh|Dimensionality|Nodes");
    CHECK(r.t->rowOfItem(r.dims) == 4 && r.dims->depth() == 1 && r.file->depth() == 0 && r.dims->parent() == r.mesh);
    r.t->setExpanded(r.file, false);
    CHECK_STR(r.rows(), "File|Mesh|Dimensionality|Nodes");
    CHECK(r.t->rowOfItem(r.name) == -1 && !r.file->isExpanded());
    CHECK_STR(r.take(), "collapsed File");
    r.t->setExpanded(r.file, true);
    CHECK_STR(r.rows(), "File|Name|Size|Mesh|Dimensionality|Nodes");
    CHECK_STR(r.take(), "expanded File");
    r.t->setExpanded(r.file, true);  // already: nothing
    CHECK_STR(r.take(), "");
    r.t->collapseAll();
    CHECK_STR(r.rows(), "File|Mesh");
    CHECK_STR(r.take(), "collapsed File, collapsed Mesh");
    r.t->expandAll();
    CHECK(r.t->rowCount() == 6);
    CHECK_STR(r.take(), "expanded File, expanded Mesh");
    CHECK(r.t->realizedRows() == 6 && r.t->realizedRow(4)->accessibleName == "Dimensionality, 2D");
}

TEST(a_leaf_has_no_expand_state_and_setting_it_reports_nothing) {
    Rig r;
    r.t->setExpanded(r.name, true);
    CHECK_STR(r.take(), "");
    CHECK(r.t->rowCount() == 6 && r.t->realizedRow(1)->accessibleExpandState() == -1);
    CHECK(r.t->realizedRow(0)->accessibleExpandState() == 1 && r.t->realizedRow(3)->accessibleExpandState() == 1);
}

TEST(a_child_added_to_a_collapsed_parent_is_hidden_and_to_an_expanded_one_is_inserted_in_order) {
    Rig r;
    r.t->setExpanded(r.mesh, false);
    r.take();
    TreeItem* late = r.t->addItem(r.mesh, {"Late", "x"});
    CHECK_STR(r.rows(), "File|Name|Size|Mesh");
    CHECK(r.t->rowOfItem(late) == -1);
    r.t->setExpanded(r.mesh, true);
    CHECK_STR(r.rows(), "File|Name|Size|Mesh|Dimensionality|Nodes|Late");
    r.t->addItem(r.file, {"Schema", "5"});
    CHECK_STR(r.rows(), "File|Name|Size|Schema|Mesh|Dimensionality|Nodes|Late");
    TreeItem* top = r.t->addTopLevelItem({"Fields"});
    CHECK(r.t->rowOfItem(top) == 8 && r.t->topLevelItemCount() == 3 && r.t->topLevelItem(2) == top);
    CHECK(r.file->childCount() == 3 && r.file->child(2)->text(0) == "Schema" && r.file->child(5) == nullptr);
}

TEST(item_texts_tool_tips_and_spans_are_set_and_read) {
    Rig r;
    CHECK_STR(r.name->text(1), "mosfet_2d.npz");
    CHECK_STR(r.name->text(7), "");
    r.t->setItemToolTip(r.name, 1, "C:/runs/mosfet_2d.npz");
    CHECK_STR(r.name->toolTip(1), "C:/runs/mosfet_2d.npz");
    CHECK_STR(r.t->realizedRow(1)->toolTip, "C:/runs/mosfet_2d.npz");  // the value's tip is the row's
    CHECK_STR(r.t->realizedRow(2)->toolTip, "");
    r.t->setItemText(r.size, 1, "2.0 MB");
    CHECK_STR(r.t->realizedRow(2)->accessibleName, "Size, 2.0 MB");
    CHECK(r.file->firstColumnSpanned() && !r.name->firstColumnSpanned());
    r.t->setItemText(r.name, 3, "far");  // a column past the count grows the item, nothing breaks
    CHECK_STR(r.name->text(3), "far");
}

// -- current item and selection ----------------------------------------------------------------------------------

TEST(a_click_on_the_text_selects_and_reports_the_item) {
    Rig r;
    r.click(1, Rig::X(1, 60));
    CHECK(r.t->currentItem() == r.name && r.t->currentRow() == 1);
    CHECK_STR(r.take(), "current Name");
    r.click(1, Rig::X(1, 60));
    CHECK_STR(r.take(), "");
    r.click(3, Rig::X(0, 30));
    CHECK_STR(r.take(), "current Mesh");
    CHECK(r.h.router.focusWidget() == r.t);
}

TEST(a_click_on_a_triangle_opens_or_closes_without_selecting) {
    Rig r;
    r.click(0, Rig::X(0, 8));  // inside the 16-DIP box that starts 4 in
    CHECK(!r.file->isExpanded() && r.t->currentRow() == -1);
    CHECK_STR(r.take(), "collapsed File");
    r.click(0, Rig::X(0, 8));
    CHECK(r.file->isExpanded());
    CHECK_STR(r.take(), "expanded File");
    r.click(1, Rig::X(1, 8));  // a leaf has no triangle: it is a click on the row
    CHECK(r.t->currentItem() == r.name);
}

TEST(a_double_click_toggles_a_parent_and_activates_a_leaf) {
    Rig r;
    r.dblclick(3, Rig::X(0, 40));
    CHECK(!r.mesh->isExpanded());
    CHECK_STR(r.take(), "current Mesh, collapsed Mesh");
    r.dblclick(3, Rig::X(0, 40));
    CHECK(r.mesh->isExpanded());
    r.take();
    r.dblclick(4, Rig::X(1, 40));  // Dimensionality, a leaf
    CHECK_STR(r.take(), "current Dimensionality, activated Dimensionality");
}

TEST(collapsing_a_parent_that_holds_the_current_item_makes_the_parent_current) {
    Rig r;
    r.t->setCurrentItem(r.dims);
    CHECK_STR(r.take(), "current Dimensionality");
    r.t->setExpanded(r.mesh, false);
    CHECK(r.t->currentItem() == r.mesh);
    CHECK_STR(r.take(), "current Mesh, collapsed Mesh");  // one current change, not a second one for the neighbour
    CHECK(r.t->selectedRows() == std::vector<int>{3});
    r.t->setExpanded(r.mesh, true);
    CHECK(r.t->currentItem() == r.mesh && r.take() == "expanded Mesh");
    // collapsing an unrelated parent: the current item keeps its row index only if it did not move
    r.t->setCurrentItem(r.mesh);
    r.take();
    r.t->setExpanded(r.file, false);
    CHECK(r.t->currentItem() == r.mesh && r.t->currentRow() == 1);  // same item, new row
    CHECK_STR(r.take(), "collapsed File");
}

TEST(set_current_item_opens_the_ancestors) {
    Rig r;
    r.t->collapseAll();
    r.take();
    r.t->setCurrentItem(r.dims);
    CHECK(r.mesh->isExpanded() && !r.file->isExpanded());
    CHECK(r.t->currentItem() == r.dims && r.t->currentRow() == r.t->rowOfItem(r.dims));
    CHECK_STR(r.take(), "expanded Mesh, current Dimensionality");
    r.t->setCurrentItem(nullptr);
    CHECK(r.t->currentRow() == -1);
    CHECK_STR(r.take(), "current none");
}

TEST(a_tree_with_signals_blocked_reports_nothing_and_still_changes) {
    Rig r;
    r.t->setCurrentItem(r.name);
    r.take();
    r.t->setSignalsBlocked(true);
    r.t->setExpanded(r.file, false);
    r.t->setExpanded(r.file, true);
    r.t->setCurrentItem(r.mesh);
    r.t->collapseAll();
    r.t->setSignalsBlocked(false);
    CHECK_STR(r.take(), "");
    CHECK(r.t->rowCount() == 2 && r.t->currentItem() == r.mesh);
}

// -- keys ---------------------------------------------------------------------------------------------------------------

TEST(right_and_left_open_close_and_walk_the_tree) {
    Rig r;
    r.t->setFocus(FocusReason::Mouse);
    r.t->setCurrentItem(r.file);
    r.take();
    r.key(keys::Left);  // expanded: collapses
    CHECK(!r.file->isExpanded());
    r.key(keys::Left);  // collapsed top-level: nothing
    CHECK(r.t->currentItem() == r.file);
    r.key(keys::Right);  // opens
    CHECK(r.file->isExpanded() && r.t->currentItem() == r.file);
    r.key(keys::Right);  // expanded: the first child
    CHECK(r.t->currentItem() == r.name);
    r.key(keys::Right);  // a leaf: nothing
    CHECK(r.t->currentItem() == r.name);
    r.key(keys::Left);  // a child: its parent
    CHECK(r.t->currentItem() == r.file);
    r.take();
    r.key(keys::Subtract);
    CHECK(!r.file->isExpanded());
    r.key(keys::Add);
    CHECK(r.file->isExpanded());
    r.key(keys::Down);
    r.key(keys::Down);
    r.key(keys::Down);
    CHECK(r.t->currentItem() == r.mesh);
}

TEST(star_opens_everything_under_the_item_and_enter_activates) {
    Rig r;
    TreeItem* deep = r.t->addItem(r.dims, {"Deeper", "y"});
    r.t->collapseAll();
    r.t->setCurrentItem(r.mesh);  // opens Mesh
    r.t->setExpanded(r.mesh, false);
    r.t->setFocus(FocusReason::Mouse);
    r.take();
    r.key(keys::Multiply);
    CHECK(r.mesh->isExpanded() && r.dims->isExpanded() && r.t->rowOfItem(deep) >= 0);
    CHECK(!r.file->isExpanded());  // only under the current item
    r.take();
    r.key(keys::Return);
    CHECK_STR(r.take(), "activated Mesh");
}

// -- removing items ------------------------------------------------------------------------------------------------

TEST(removing_an_item_removes_its_subtree_and_the_current_item_moves_on) {
    Rig r;
    r.t->setCurrentItem(r.dims);
    r.take();
    r.t->removeItem(r.mesh);
    CHECK_STR(r.rows(), "File|Name|Size");
    CHECK(r.t->topLevelItemCount() == 1);
    CHECK_STR(r.take(), "current Size");  // its neighbour: the row that took the removed row's place, clamped
    r.t->removeItem(r.name);
    CHECK_STR(r.rows(), "File|Size");
    CHECK(r.file->childCount() == 1);
    CHECK_STR(r.take(), "");  // Size stayed current
    CHECK(r.t->currentItem() == r.size);
    r.t->removeItem(nullptr);  // nothing
    r.t->clear();
    CHECK(r.t->rowCount() == 0 && r.t->currentItem() == nullptr && r.t->realizedRows() == 0);
    CHECK_STR(r.take(), "current none");
    r.t->clear();
    CHECK_STR(r.take(), "");
}

// -- columns and the header --------------------------------------------------------------------------------------

TEST(the_first_column_fits_its_widest_text_and_the_last_fills_the_rest) {
    Rig r;
    // "Dimensionality" is 14 bytes = 84 DIPs, a level deep: 16 + 4 + 16 + 84 + 4 = 124; the header and the groups are less
    CHECK(r.t->columnWidth(0) == 124 && r.t->columnX(1) == 124);
    CHECK(r.t->columnWidth(1) == 298 - 124);  // stretched
    CHECK((r.t->headerBand()->geometry() == RectI{1, 1, 298, 21}));
    const auto& cells = r.t->headerBand()->children();
    CHECK(cells.size() == 2 && cells[0]->accessibleName == "Property" && cells[1]->accessibleName == "Value");
    CHECK((cells[0]->geometry() == RectI{0, 0, 124, 21}) && (cells[1]->geometry() == RectI{124, 0, 174, 21}));
    r.t->setItemText(r.dims, 0, "A much longer property name");  // 27 bytes
    CHECK(r.t->columnWidth(0) == 16 + 4 + 16 + 27 * 6 + 4);
    r.t->setExpanded(r.mesh, false);  // hidden items do not count
    CHECK(r.t->columnWidth(0) == 16 + 4 + 16 + 4 * 6 + 4);  // "Name" and "Size" a level deep now set it
    r.t->setExpanded(r.mesh, true);
    r.t->setFirstColumnFitsContents(false);
    CHECK(r.t->columnWidth(0) == 100);
    r.t->setStretchLastColumn(false);
    CHECK(r.t->columnWidth(1) == 100);
}

TEST(a_header_edge_drags_to_a_set_width_and_a_double_click_fits_again) {
    Rig r;
    // column 0's right edge: band x 124, window x 11 + 124 = 135
    r.h.router.mouse(mouse(MouseType::Move, 135, 20, 0));
    CHECK(r.h.router.cursor() == Cursor::SizeWE);
    r.h.router.mouse(mouse(MouseType::Down, 135, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, 175, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 175, 20, 0));
    CHECK(r.t->columnWidth(0) == 164 && r.t->columnX(1) == 164);
    r.t->setItemText(r.dims, 0, "short");
    CHECK(r.t->columnWidth(0) == 164);  // a width that was set stays
    const float edge = 11 + 164;
    r.h.router.mouse(mouse(MouseType::DoubleClick, edge, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, edge, 20, 0));
    CHECK(r.t->columnWidth(0) == 16 + 4 + 16 + 5 * 6 + 4);  // fitted again: "short" and "Nodes", five bytes a level deep
}

TEST(a_wide_tree_scrolls_sideways_and_the_header_follows) {
    Rig r;
    r.t->setStretchLastColumn(false);
    r.t->setColumnWidth(0, 200);
    r.t->setColumnWidth(1, 300);  // 500 in 298
    CHECK(r.t->horizontalScrollBar()->isNeeded() && r.t->horizontalScrollBar()->maximum() == 202);
    r.t->horizontalScrollBar()->setValue(50);
    CHECK((r.t->headerBand()->children()[1]->geometry() == RectI{150, 0, 300, 21}));
    CHECK(r.t->realizedRow(1)->geometry().x == -50);
}

TEST(the_header_can_be_hidden) {
    Rig r;
    const float before = r.t->viewportRect().y;
    r.t->setHeaderVisible(false);
    CHECK(r.t->viewportRect().y == before - 21 && !r.t->headerBand()->isVisibleSelf());
}

// -- painting and copy -------------------------------------------------------------------------------------------

TEST(rows_paint_a_triangle_for_a_parent_indented_text_and_a_spanned_group_across) {
    Rig r;
    RecordingPainter g;
    r.t->realizedRow(0)->paint(g);  // File: an expanded, spanned group
    bool tri = false, text = false;
    for (const auto& op : g.ops()) {
        if (op.kind == "polygon") tri = true;
        if (op.kind == "text" && op.text == "File") text = op.rect.x == 4 + 16 && op.rect.width > 200;  // spans past the first column
    }
    CHECK(tri && text);
    RecordingPainter l;
    r.t->realizedRow(4)->paint(l);  // Dimensionality: a leaf a level deep, two columns
    int polygons = 0;
    bool key_text = false, value_text = false;
    for (const auto& op : l.ops()) {
        polygons += op.kind == "polygon";
        if (op.kind == "text" && op.text == "Dimensionality") key_text = op.rect.x == 4 + 16 + 16;
        if (op.kind == "text" && op.text == "2D") value_text = op.rect.x == 124 + 6 - 2;
    }
    CHECK(polygons == 0 && key_text && value_text);
    RecordingPainter c;
    r.t->collapseAll();
    r.t->realizedRow(0)->paint(c);  // collapsed: a different triangle than the expanded one
    int p = 0;
    for (const auto& op : c.ops()) p += op.kind == "polygon";
    CHECK(p == 1);
}

TEST(copy_puts_the_selected_row_on_the_clipboard_with_its_columns_tab_separated) {
    Rig r;
    r.click(1, Rig::X(1, 60));
    r.key('C', Mod::Ctrl);
    CHECK(r.h.board.value == std::optional<std::string>("Name\tmosfet_2d.npz"));
    r.t->setCurrentItem(nullptr);
    r.h.board.value.reset();
    CHECK(!r.t->copy() && !r.h.board.value);
}

// -- UI Automation --------------------------------------------------------------------------------------------------

TEST(rows_are_tree_items_with_names_expand_state_and_selection) {
    Rig r;
    ItemRow* a = r.t->realizedRow(0);
    ItemRow* b = r.t->realizedRow(1);
    CHECK(a && b && a->accessibleRole() == Role::TreeItem);
    CHECK_STR(a->accessibleName, "File");
    CHECK_STR(b->accessibleName, "Name, mosfet_2d.npz");
    CHECK(a->accessibleExpandState() == 1 && b->accessibleExpandState() == -1);
    a->accessibleExpand(false);  // through UI Automation
    CHECK(!r.file->isExpanded());
    CHECK(r.t->realizedRow(0)->accessibleExpandState() == 0 && r.t->realizedRow(1)->accessibleName == "Mesh");
    r.t->realizedRow(0)->accessibleExpand(true);
    CHECK(r.file->isExpanded());
    r.t->realizedRow(1)->accessibleSelect();
    CHECK(r.t->currentItem() == r.name && r.t->realizedRow(1)->accessibleSelectionState() == 1);
    CHECK(r.t->realizedRow(1)->accessibleSelectionContainer() == r.t && r.t->accessibleIsSelectionContainer());
    CHECK(r.t->accessibleSelection() == std::vector<Widget*>{r.t->realizedRow(1)});
}

TEST(a_tree_destroyed_with_a_row_pressed_is_safe) {
    auto host = std::make_unique<Host>();
    auto* t = host->root.addChild<TreeView>();
    t->setGeometry({10, 10, 300, 250});
    TreeItem* a = t->addTopLevelItem({"a"});
    t->addItem(a, {"b"});
    t->setExpanded(a, true);
    host->router.mouse(mouse(MouseType::Down, 60, 42, kLeftBit));
    host->root.release(t);
    host->router.mouse(mouse(MouseType::Up, 60, 42, 0));
    CHECK(host->root.children().empty());
}

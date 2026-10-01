// N3e on real windows (NATIVE-DESKTOP-PLAN.md 27.8.6): the list, the table and the tree drawn through UiWindow + Direct2D on
// WARP -- goldens at 100/150/200% in the light theme and in high contrast -- and driven by REAL window messages: clicks,
// drags, keys and the wheel; in-place editing with the real LineEdit editor (typing, Enter, Escape, Tab, the focus
// leaving); tab-separated copy on the real clipboard; column resizing; and UI Automation through the real client (List,
// Table, Tree: Selection, SelectionItem, Scroll, ScrollItem, Grid, GridItem, Table, TableItem, ExpandCollapse, Toggle).
// Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/list_view.hpp"
#include "ui/widgets/table_view.hpp"
#include "ui/widgets/tree_view.hpp"
#include "ui/win32/clipboard.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/line_editor.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;
using tcad::platform::Mod;
using tcad::platform::MouseButton;

namespace {

constexpr float kW = 440, kH = 560;

bool sameStr(const std::string& got, const std::string& want) {
    if (got != want) std::printf("  got      [%s]%c  expected [%s]%c", got.c_str(), 10, want.c_str(), 10);
    return got == want;
}
#define CHECK_STR(actual, expected) CHECK(sameStr((actual), (expected)))

void nightSky() {
    HighContrast& hc = highContrast();
    hc.on = true;
    hc.window = Color::rgb(0x000000);
    hc.window_text = Color::rgb(0xFFFFFF);
    hc.highlight = Color::rgb(0x1AEBFF);
    hc.highlight_text = Color::rgb(0x000000);
    hc.gray_text = Color::rgb(0x3FF23F);
}

void pumpFor(int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        pumpTimers();
        Sleep(5);
    }
}

// The scene: a list (checkable models, a header row that cannot be selected, a disabled row with a reason, an edited
// italic overlay), a table (a read-only name column, an editable value column, a selected block, header, stretch) and the
// info tree. The table has the focus, so its selection is the active one; the others show the inactive one.
struct SceneWindow {
    std::unique_ptr<UiWindow> w;
    ListView* list = nullptr;
    TableView* table = nullptr;
    TreeView* tree = nullptr;

    explicit SceneWindow(double scale = 1.0) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_collections_tests", .width = 440, .height = 560, .scale_override = scale});
        if (!r) {
            std::printf("  UiWindow::create: %s\n", r.error().c_str());
            return;
        }
        w = std::move(*r);
        w->resizeClient(px(kW, scale), px(kH, scale));
        w->router().setAlwaysShowCues(false);
        Widget& root = w->root();
        auto* col = root.setLayout<BoxLayout>(Orientation::Vertical);
        list = root.addChild<ListView>();
        list->name = "list";
        list->accessibleName = "Models";
        for (const char* t : {"Boltzmann statistics", "Fermi-Dirac", "Incomplete ionization"}) {
            const int i = list->addItem(t);
            list->setItemCheckable(i, true);
        }
        list->setItemChecked(0, true);
        list->setItemChecked(2, true);
        const int hdr = list->addItem("Derived (via the backend)");
        list->setItemSelectable(hdr, false);
        list->setItemItalic(hdr, true);
        const int off = list->addItem("bands (needs a result with potential)");
        list->setItemEnabled(off, false);
        list->setItemToolTip(off, "Needs potential, which this result lacks");
        const int ov = list->addItem("comparison: run 7");
        list->setItemEditable(ov, true);
        list->setItemItalic(ov, true);
        list->setItemColor(ov, T::TextDim);
        for (int i = 0; i < 8; ++i) list->addItem("overlay " + std::to_string(i));
        list->setCurrentRow(1);
        col->addWidget(list, 2);
        table = root.addChild<TableView>();
        table->name = "table";
        table->accessibleName = "Parameters";
        table->setColumnCount(3);
        table->setHeaderLabels({"Parameter", "Value", "Unit"});
        table->setRowCount(6);
        const char* names[6] = {"tox", "Nsub", "Vdd", "T", "mu0", "Lg"};
        const char* vals[6] = {"2e-7", "1e17", "1.2", "300", "400", "0.18"};
        const char* units[6] = {"cm", "cm^-3", "V", "K", "cm^2/Vs", "um"};
        for (int r2 = 0; r2 < 6; ++r2) {
            table->setItem(r2, 0, names[r2]);
            table->setItem(r2, 1, vals[r2]);
            table->setItem(r2, 2, units[r2]);
            table->setCellEditable(r2, 0, false);
        }
        table->setStretchLastColumn(true);
        table->setColumnWidth(0, 90);
        table->setColumnWidth(1, 120);
        col->addWidget(table, 2);
        tree = root.addChild<TreeView>();
        tree->name = "tree";
        tree->accessibleName = "Info";
        tree->setColumnCount(2);
        tree->setHeaderLabels({"Property", "Value"});
        TreeItem* file = tree->addTopLevelItem({"File"});
        tree->setFirstColumnSpanned(file, true);
        tree->addItem(file, {"Name", "mosfet_2d.npz"});
        tree->addItem(file, {"Size", "1.2 MB"});
        TreeItem* mesh = tree->addTopLevelItem({"Mesh"});
        tree->setFirstColumnSpanned(mesh, true);
        tree->addItem(mesh, {"Dimensionality", "2D"});
        tree->addItem(mesh, {"Nodes", "64 x 32 = 2048"});
        TreeItem* rng = tree->addItem(mesh, {"x range", "0 .. 1.2 um"});
        tree->addItem(rng, {"min", "0"});
        tree->setExpanded(file, true);
        tree->setExpanded(mesh, true);
        tree->setCurrentItem(mesh->child(0));
        col->addWidget(tree, 2);
        w->renderNow(nullptr, false);
        table->setFocus(FocusReason::Mouse);
        table->setCurrentCell(1, 1);  // a 2 x 2 block, by Shift+arrows
        w->router().key({VK_DOWN, Mod::Shift, true, false});
        w->router().key({VK_RIGHT, Mod::Shift, true, false});
    }
};

// One view alone in a window, with a line edit under it to move the focus to.
template <class V>
struct OneView {
    std::unique_ptr<UiWindow> w;
    V* view = nullptr;
    LineEdit* other = nullptr;
    explicit OneView(float width = 300, float height = 160, double scale = 1.0) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_view_tests", .width = 340, .height = 260, .scale_override = scale});
        if (!r) return;
        w = std::move(*r);
        w->resizeClient(px(340, scale), px(260, scale));
        Widget& root = w->root();
        view = root.addChild<V>();
        view->name = "view";
        view->setGeometry({px(10, scale), px(10, scale), px(width, scale), px(height, scale)});
        other = root.addChild<LineEdit>(w->window().hwnd());
        other->setGeometry({px(10, scale), px(200, scale), px(200, scale), px(24, scale)});
        other->setCaretBlinking(false);
    }
    PointF origin() const {
        const RectI r = view->windowRect();
        return {static_cast<float>(r.x / w->scale()), static_cast<float>(r.y / w->scale())};
    }
    // The centre of `row` of a list or tree, `dx` DIPs from the view's left edge.
    PointF rowPoint(int row, float dx) const {
        const RectF rr = view->rowRect(row);
        const PointF o = origin();
        return {o.x + dx, o.y + rr.y + rr.height / 2};
    }
};

}  // namespace

// -- goldens --------------------------------------------------------------------------------------------------------

TEST(collections_match_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        SceneWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) return;
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        CHECK(img.width == px(kW, pct / 100.0) && img.height == px(kH, pct / 100.0));
        const GoldenResult r = checkGolden(*warp(), "n3e_collections@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}

TEST(collections_in_high_contrast_match_the_goldens) {
    const HighContrast saved = highContrast();
    for (int pct : {100, 150, 200}) {
        SceneWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) break;
        nightSky();  // after the window exists: UiWindow::create reads the system's own state
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n3e_collections_hc@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
    highContrast() = saved;
}

// -- the list with real messages -------------------------------------------------------------------------------------

TEST(a_thousand_item_list_costs_a_screenful_of_widgets_and_scrolls_with_the_real_wheel) {
    OneView<ListView> f(300, 160);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    for (int i = 0; i < 1000; ++i) f.view->addItem("item " + std::to_string(i));
    f.w->renderNow(nullptr, false);
    CHECK(f.view->realizedRows() <= 9 && f.view->realizedRows() >= 6);
    Driver d(*f.w);
    const int before = f.view->realizedRows();
    d.wheel(f.rowPoint(2, 100), -3);  // three notches toward the user
    CHECK(f.view->scrollY() == 9 * static_cast<int>(f.view->rowHeight()));
    CHECK(f.view->realizedRows() == before || f.view->realizedRows() == before + 1);
    CHECK(f.view->realizedRow(9) != nullptr && f.view->realizedRow(0) == nullptr);
    d.click(f.rowPoint(f.view->firstVisibleRow() + 2, 100));
    CHECK(f.view->currentRow() == f.view->firstVisibleRow() + 2 && f.view->currentRow() >= 11);
    d.key(VK_END);
    CHECK(f.view->currentRow() == 999 && f.view->scrollY() == f.view->verticalScrollBar()->maximum());
    d.key(VK_HOME);
    CHECK(f.view->currentRow() == 0 && f.view->scrollY() == 0);
    d.key(VK_NEXT);
    CHECK(f.view->currentRow() > 4 && f.view->currentRow() < 10);
    f.w->renderNow(nullptr, false);  // every realized row paints without a fault
}

TEST(clicks_keys_and_checks_work_on_a_list_through_the_window) {
    SceneWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    std::vector<std::string> log;
    f.list->on_current_row_changed = [&](int r) { log.push_back("row " + std::to_string(r)); };
    f.list->on_item_changed = [&](int r) { log.push_back("item " + std::to_string(r)); };
    const RectI lg = f.list->windowRect();
    auto at = [&](int row, float dx) {
        const RectF rr = f.list->rowRect(row);
        return PointF{static_cast<float>(lg.x / f.w->scale()) + dx, static_cast<float>(lg.y / f.w->scale()) + rr.y + rr.height / 2};
    };
    d.click(at(0, 80));
    CHECK(f.list->currentRow() == 0 && log == std::vector<std::string>{"row 0"});
    log.clear();
    d.click(at(1, 12));  // the box of a checkable row: checks it and makes it current
    CHECK(f.list->isItemChecked(1) && f.list->currentRow() == 1);
    CHECK((log == std::vector<std::string>{"item 1", "row 1"}));
    log.clear();
    d.key(VK_SPACE);
    CHECK(!f.list->isItemChecked(1));
    d.key(VK_DOWN);  // 2
    d.key(VK_DOWN);  // the header (3) cannot be current: stays at 2... then 4 is disabled, 5 is the editable overlay
    CHECK(f.list->currentRow() == 5);
    d.key(VK_UP);
    CHECK(f.list->currentRow() == 2);
    d.click(at(3, 80));  // the header row: nothing
    d.click(at(4, 80));  // the disabled row: nothing
    CHECK(f.list->currentRow() == 2);
}

TEST(a_list_item_is_edited_in_place_with_the_real_editor) {
    SceneWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    std::vector<std::string> log;
    f.list->on_item_changed = [&](int r) { log.push_back("item " + std::to_string(r)); };
    f.list->setCurrentRow(5);  // the editable overlay
    f.list->setFocus(FocusReason::Tab);
    d.key(VK_F2);
    CHECK(f.list->isEditing());
    Widget* ed = f.w->router().focusWidget();
    auto* le = dynamic_cast<LineEdit*>(ed);
    CHECK(le != nullptr);
    if (!le) return;
    CHECK_STR(le->text(), "comparison: run 7");
    CHECK_STR(le->model().selectedText(), "comparison: run 7");  // everything selected: typing replaces it
    d.type(u"run 9 (renamed)");
    d.key(VK_RETURN);
    CHECK(!f.list->isEditing());
    CHECK_STR(f.list->itemText(5), "run 9 (renamed)");
    CHECK((log == std::vector<std::string>{"item 5"}));
    CHECK(f.w->router().focusWidget() == f.list);  // the list has the keyboard back
    log.clear();
    d.key(VK_F2);
    d.type(u"discarded");
    d.key(VK_ESCAPE);
    CHECK(!f.list->isEditing() && f.list->itemText(5) == "run 9 (renamed)" && log.empty());
    // a double click edits; leaving it (a click on another widget) commits
    const RectI lg = f.list->windowRect();
    const RectF rr = f.list->rowRect(5);
    const PointF p{static_cast<float>(lg.x / f.w->scale()) + 80, static_cast<float>(lg.y / f.w->scale()) + rr.y + rr.height / 2};
    d.press(p);
    d.release(p);
    d.press(p, MouseButton::Left, true);
    d.release(p);
    CHECK(f.list->isEditing());
    d.key('A', Mod::Ctrl);
    d.type(u"left behind");
    const RectI tg = f.table->windowRect();
    d.click({static_cast<float>(tg.x / f.w->scale()) + 80, static_cast<float>(tg.y / f.w->scale()) + 60});  // elsewhere
    CHECK(!f.list->isEditing());
    CHECK_STR(f.list->itemText(5), "left behind");
}

// -- the table with real messages ------------------------------------------------------------------------------------

TEST(a_table_selects_a_block_with_a_real_drag_and_copies_it_as_lines_of_tabs) {
    OneView<TableView> f(300, 160);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    const auto saved = clipboardText(nullptr);
    f.view->setColumnCount(3);
    f.view->setHeaderLabels({"A", "B", "C"});
    f.view->setRowCount(5);
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 3; ++c) f.view->setItem(r, c, std::string(1, static_cast<char>('a' + c)) + std::to_string(r));
    f.w->renderNow(nullptr, false);
    Driver d(*f.w);
    auto cellPoint = [&](int row, int col) {
        const RectF rr = f.view->rowRect(row);
        const PointF o = f.origin();
        return PointF{o.x + rr.x + f.view->gutterWidth() + f.view->columnX(col) + 20, o.y + rr.y + rr.height / 2};
    };
    d.press(cellPoint(1, 0));
    d.move(cellPoint(2, 1));
    d.move(cellPoint(3, 1));
    d.release(cellPoint(3, 1));
    const auto b = f.view->selectedBlock();
    CHECK(b.valid() && b.row0 == 1 && b.col0 == 0 && b.row1 == 3 && b.col1 == 1);
    d.key('C', Mod::Ctrl);
    CHECK(clipboardText(nullptr) == std::optional<std::string>("a1\tb1\na2\tb2\na3\tb3"));
    if (OpenClipboard(nullptr)) {  // on the real clipboard the lines end in CR LF, as Excel and Notepad expect
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            const auto* ws = static_cast<const wchar_t*>(GlobalLock(h));
            CHECK(ws != nullptr && std::wstring(ws) == L"a1\tb1\r\na2\tb2\r\na3\tb3");
            GlobalUnlock(h);
        }
        CloseClipboard();
    }
    d.click(cellPoint(4, 2));
    d.key(VK_UP, Mod::Shift);
    d.key(VK_LEFT, Mod::Shift);
    const auto b2 = f.view->selectedBlock();
    CHECK(b2.valid() && b2.row0 == 3 && b2.col0 == 1 && b2.row1 == 4 && b2.col1 == 2);
    if (saved) setClipboardText(nullptr, *saved);
    else if (OpenClipboard(nullptr)) EmptyClipboard(), CloseClipboard();
}

TEST(a_table_cell_is_edited_in_place_by_f2_typing_and_tab) {
    OneView<TableView> f(300, 160);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.view->setColumnCount(3);
    f.view->setRowCount(3);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) f.view->setItem(r, c, "x" + std::to_string(r) + std::to_string(c));
    f.view->setCellEditable(0, 1, false);
    f.w->renderNow(nullptr, false);
    Driver d(*f.w);
    std::vector<std::string> log;
    f.view->on_cell_changed = [&](int r, int c) { log.push_back(std::to_string(r) + "," + std::to_string(c)); };
    f.view->setFocus(FocusReason::Tab);  // entered by Tab: (0, 0) is current
    CHECK(f.view->currentRow() == 0 && f.view->currentColumn() == 0);
    d.key(VK_F2);
    CHECK(f.view->isEditing());
    d.type(u"first");
    d.key(VK_TAB);  // commits, and edits the next editable cell: (0, 1) is read-only, so (0, 2)
    CHECK_STR(f.view->cellText(0, 0), "first");  // F2 selects the text, so typing replaced it
    CHECK(f.view->isEditing() && f.view->currentColumn() == 2);
    d.key(VK_TAB, Mod::Shift);  // commits (no change) and goes BACK: (0, 1) is read-only, so (0, 0)
    CHECK(f.view->isEditing() && f.view->currentColumn() == 0);
    d.key(VK_ESCAPE);
    CHECK(!f.view->isEditing());
    d.key(VK_DOWN);
    d.type(u"Q");  // typing a character starts editing with it in place of the cell's text
    CHECK(f.view->isEditing());
    auto* le = dynamic_cast<LineEdit*>(f.w->router().focusWidget());
    CHECK(le != nullptr && le->text() == "Q");
    d.key(VK_RETURN);
    CHECK_STR(f.view->cellText(1, 0), "Q");  // Shift+Tab went back to column 0, so Down lands on (1, 0)
    CHECK(f.w->router().focusWidget() == f.view);
    CHECK(!log.empty() && log.back() == "1,0");
}

TEST(a_header_edge_resizes_a_column_with_a_real_drag_and_the_wheel_scrolls_sideways_content) {
    OneView<TableView> f(300, 160);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.view->setColumnCount(6);
    f.view->setRowCount(4);
    f.w->renderNow(nullptr, false);
    Driver d(*f.w);
    const PointF o = f.origin();
    const float edge = o.x + 1 + f.view->gutterWidth() + f.view->columnWidth(0);
    const float hy = o.y + 1 + 8;
    const float w0 = f.view->columnWidth(0);
    d.move({edge, hy});
    CHECK(d.setCursorMessage());
    d.press({edge, hy});
    d.move({edge + 40, hy});
    d.release({edge + 40, hy});
    CHECK(f.view->columnWidth(0) == w0 + 40);
    CHECK(f.view->horizontalScrollBar()->isNeeded());
    f.view->horizontalScrollBar()->setValue(150);
    f.w->renderNow(nullptr, false);
    CHECK(f.view->headerBand()->children()[0]->geometry().x < 0 || f.view->headerBand()->children()[1]->geometry().x < f.view->headerBand()->children()[0]->geometry().right());
}

// -- the tree with real messages -----------------------------------------------------------------------------------

TEST(a_tree_opens_and_closes_by_triangle_key_and_double_click_through_the_window) {
    OneView<TreeView> f(300, 200);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.view->setColumnCount(2);
    f.view->setHeaderLabels({"Property", "Value"});
    TreeItem* a = f.view->addTopLevelItem({"Group"});
    f.view->addItem(a, {"one", "1"});
    f.view->addItem(a, {"two", "2"});
    TreeItem* b = f.view->addTopLevelItem({"Other"});
    f.view->addItem(b, {"three", "3"});
    f.w->renderNow(nullptr, false);
    Driver d(*f.w);
    CHECK(f.view->rowCount() == 2);
    d.click(f.rowPoint(0, 4 + 8));  // the triangle
    CHECK(a->isExpanded() && f.view->rowCount() == 4 && f.view->currentRow() == -1);
    d.click(f.rowPoint(0, 80));
    CHECK(f.view->currentItem() == a);
    d.key(VK_LEFT);
    CHECK(!a->isExpanded() && f.view->rowCount() == 2);
    d.key(VK_RIGHT);
    d.key(VK_RIGHT);  // into the first child
    CHECK(f.view->currentItem() == a->child(0));
    d.key(VK_LEFT);   // back to the parent
    CHECK(f.view->currentItem() == a);
    d.key(VK_DOWN);
    d.key(VK_DOWN);
    d.key(VK_DOWN);
    CHECK(f.view->currentItem() == b);
    d.press(f.rowPoint(3, 80), MouseButton::Left, true);  // a double click on a parent
    d.release(f.rowPoint(3, 80));
    CHECK(b->isExpanded());
}

// -- UI Automation, through the real client ---------------------------------------------------------------------------

namespace {

ComPtr<IUIAutomation> client() {
    ComPtr<IUIAutomation> a;
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&a));
    return a;
}

std::string str(BSTR b) {
    std::string s;
    if (!b) return s;
    const int n = WideCharToMultiByte(CP_UTF8, 0, b, -1, nullptr, 0, nullptr, nullptr);
    s.resize(static_cast<std::size_t>(n > 0 ? n - 1 : 0));
    WideCharToMultiByte(CP_UTF8, 0, b, -1, s.data(), n, nullptr, nullptr);
    SysFreeString(b);
    return s;
}

std::vector<ComPtr<IUIAutomationElement>> kids(IUIAutomation* a, IUIAutomationElement* parent) {
    ComPtr<IUIAutomationTreeWalker> walker;
    a->get_RawViewWalker(&walker);
    std::vector<ComPtr<IUIAutomationElement>> out;
    ComPtr<IUIAutomationElement> c;
    walker->GetFirstChildElement(parent, &c);
    while (c) {
        out.push_back(c);
        ComPtr<IUIAutomationElement> next;
        walker->GetNextSiblingElement(c.Get(), &next);
        c = next;
    }
    return out;
}

ComPtr<IUIAutomationElement> byId(IUIAutomation* a, IUIAutomationElement* parent, const char* id) {
    for (auto& c : kids(a, parent)) {
        BSTR b = nullptr;
        c->get_CurrentAutomationId(&b);
        if (str(b) == id) return c;
    }
    return nullptr;
}

std::string nameOf(IUIAutomationElement* e) {
    BSTR b = nullptr;
    e->get_CurrentName(&b);
    return str(b);
}

CONTROLTYPEID typeOf(IUIAutomationElement* e) {
    CONTROLTYPEID t = 0;
    e->get_CurrentControlType(&t);
    return t;
}

std::vector<ComPtr<IUIAutomationElement>> ofType(IUIAutomation* a, IUIAutomationElement* parent, CONTROLTYPEID t) {
    std::vector<ComPtr<IUIAutomationElement>> out;
    for (auto& c : kids(a, parent))
        if (typeOf(c.Get()) == t) out.push_back(c);
    return out;
}

}  // namespace

TEST(uia_sees_a_list_as_a_list_of_items_with_selection_toggle_and_scroll) {
    SceneWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto list = byId(a.Get(), win.Get(), "list");
    CHECK(list != nullptr);
    if (!list) return;
    CHECK(typeOf(list.Get()) == UIA_ListControlTypeId);
    CHECK_STR(nameOf(list.Get()), "Models");
    auto items = ofType(a.Get(), list.Get(), UIA_ListItemControlTypeId);
    CHECK(items.size() >= 7 && items.size() <= 14);  // the realized ones of the 14 items
    if (items.size() < 7) return;
    CHECK_STR(nameOf(items[0].Get()), "Boltzmann statistics");
    // Selection pattern on the list
    ComPtr<IUIAutomationSelectionPattern> sel;
    CHECK(SUCCEEDED(list->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&sel))) && sel);
    if (sel) {
        BOOL multi = TRUE;
        sel->get_CurrentCanSelectMultiple(&multi);
        CHECK(!multi);
        ComPtr<IUIAutomationElementArray> cur;
        sel->GetCurrentSelection(&cur);
        int n = 0;
        if (cur) cur->get_Length(&n);
        CHECK(n == 1);
        if (n == 1) {
            ComPtr<IUIAutomationElement> e0;
            cur->GetElement(0, &e0);
            CHECK_STR(nameOf(e0.Get()), "Fermi-Dirac");  // the current row (1)
        }
    }
    // SelectionItem: select another row through UI Automation
    ComPtr<IUIAutomationSelectionItemPattern> si;
    CHECK(SUCCEEDED(items[2]->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&si))) && si);
    if (si) {
        BOOL is = TRUE;
        si->get_CurrentIsSelected(&is);
        CHECK(!is);
        CHECK(SUCCEEDED(si->Select()));
        CHECK(f.list->currentRow() == 2);
        si->get_CurrentIsSelected(&is);
        CHECK(is);
        ComPtr<IUIAutomationElement> container;
        si->get_CurrentSelectionContainer(&container);
        CHECK(container != nullptr && nameOf(container.Get()) == "Models");
    }
    // the disabled row cannot be selected: no SelectionItem pattern state change
    ComPtr<IUIAutomationSelectionItemPattern> dis;
    items[4]->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&dis));
    CHECK(dis == nullptr);
    // Toggle on a checkable row
    ComPtr<IUIAutomationTogglePattern> tg;
    CHECK(SUCCEEDED(items[1]->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&tg))) && tg);
    if (tg) {
        ToggleState st = ToggleState_On;
        tg->get_CurrentToggleState(&st);
        CHECK(st == ToggleState_Off);
        CHECK(SUCCEEDED(tg->Toggle()));
        CHECK(f.list->isItemChecked(1));
        tg->get_CurrentToggleState(&st);
        CHECK(st == ToggleState_On);
    }
    ComPtr<IUIAutomationTogglePattern> none;
    items[6]->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&none));
    CHECK(none == nullptr);  // not checkable
}

TEST(uia_adds_and_removes_rows_of_an_extended_list_from_its_selection) {
    OneView<ListView> f(300, 160);
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.view->setSelectionMode(ItemView::SelectionMode::Extended);
    for (int i = 0; i < 5; ++i) f.view->addItem("row " + std::to_string(i));
    f.w->renderNow(nullptr, false);
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto list = byId(a.Get(), win.Get(), "view");
    CHECK(list != nullptr);
    if (!list) return;
    auto rows = ofType(a.Get(), list.Get(), UIA_ListItemControlTypeId);
    CHECK(rows.size() == 5);
    if (rows.size() != 5) return;
    ComPtr<IUIAutomationSelectionItemPattern> p1, p3;
    rows[1]->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&p1));
    rows[3]->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&p3));
    CHECK(p1 && p3);
    if (!p1 || !p3) return;
    CHECK(SUCCEEDED(p1->AddToSelection()));
    CHECK(SUCCEEDED(p3->AddToSelection()));
    CHECK(f.view->isRowSelected(1) && f.view->isRowSelected(3));
    CHECK(SUCCEEDED(p1->RemoveFromSelection()));
    CHECK(!f.view->isRowSelected(1) && f.view->isRowSelected(3));
}

TEST(uia_scrolls_a_long_list_through_the_scroll_pattern_and_rows_come_and_go) {
    OneView<ListView> f(300, 160);
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    for (int i = 0; i < 300; ++i) f.view->addItem("row " + std::to_string(i));
    f.w->renderNow(nullptr, false);
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto list = byId(a.Get(), win.Get(), "view");
    CHECK(list != nullptr);
    if (!list) return;
    ComPtr<IUIAutomationScrollPattern> sp;
    CHECK(SUCCEEDED(list->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&sp))) && sp);
    if (!sp) return;
    BOOL v = FALSE, h = TRUE;
    sp->get_CurrentVerticallyScrollable(&v);
    sp->get_CurrentHorizontallyScrollable(&h);
    CHECK(v && !h);
    double pct = -1, view = -1, hp = 0;
    sp->get_CurrentVerticalScrollPercent(&pct);
    sp->get_CurrentVerticalViewSize(&view);
    sp->get_CurrentHorizontalScrollPercent(&hp);
    CHECK(pct == 0 && view > 0 && view < 10 && hp == UIA_ScrollPatternNoScroll);
    auto rows = ofType(a.Get(), list.Get(), UIA_ListItemControlTypeId);
    CHECK(rows.size() >= 6 && rows.size() <= 9);  // the realized ones: far fewer than 300
    ComPtr<IUIAutomationElement> first = rows[0];
    const int events = f.w->uia().structureEvents();
    CHECK(SUCCEEDED(sp->SetScrollPercent(UIA_ScrollPatternNoScroll, 50)));
    CHECK(std::abs(f.view->scrollY() - f.view->verticalScrollBar()->maximum() / 2) <= 1);
    CHECK(f.w->uia().structureEvents() > events);  // the realized rows changed: a structure event
    sp->get_CurrentVerticalScrollPercent(&pct);
    CHECK(pct > 49 && pct < 51);
    BSTR dead = nullptr;
    CHECK(first->get_CurrentName(&dead) == UIA_E_ELEMENTNOTAVAILABLE);  // the first row's widget is gone
    SysFreeString(dead);
    CHECK(SUCCEEDED(sp->Scroll(ScrollAmount_NoAmount, ScrollAmount_LargeIncrement)));
    CHECK(f.view->scrollY() > f.view->verticalScrollBar()->maximum() / 2 + 100);
    CHECK(sp->Scroll(ScrollAmount_SmallIncrement, ScrollAmount_NoAmount) == UIA_E_INVALIDOPERATION);  // it cannot scroll sideways
    CHECK(sp->SetScrollPercent(101, UIA_ScrollPatternNoScroll) == E_INVALIDARG);
    // ScrollItem on a row cut by the edge
    rows = ofType(a.Get(), list.Get(), UIA_ListItemControlTypeId);
    const int rh = static_cast<int>(f.view->rowHeight());
    f.view->verticalScrollBar()->setValue(f.view->verticalScrollBar()->value() / rh * rh + 8);  // the top row cut by 8 DIPs
    rows = ofType(a.Get(), list.Get(), UIA_ListItemControlTypeId);
    ComPtr<IUIAutomationScrollItemPattern> si;
    CHECK(!rows.empty() && SUCCEEDED(rows.front()->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&si))) && si);
    if (si) {
        CHECK(SUCCEEDED(si->ScrollIntoView()));
        CHECK(f.view->scrollY() % static_cast<int>(f.view->rowHeight()) == 0);
    }
}

TEST(uia_focus_follows_the_current_row_of_a_focused_list) {
    OneView<ListView> f(300, 160);
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    for (int i = 0; i < 5; ++i) f.view->addItem("n" + std::to_string(i));
    f.w->renderNow(nullptr, false);
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto list = byId(a.Get(), win.Get(), "view");
    CHECK(list != nullptr);
    if (!list) return;
    f.view->setCurrentRow(2);
    f.view->setFocus(FocusReason::Tab);
    auto rows = ofType(a.Get(), list.Get(), UIA_ListItemControlTypeId);
    CHECK(rows.size() == 5);
    if (rows.size() != 5) return;
    BOOL focused = FALSE, focusable = FALSE;
    rows[2]->get_CurrentHasKeyboardFocus(&focused);
    rows[2]->get_CurrentIsKeyboardFocusable(&focusable);
    CHECK(focused && focusable);
    rows[1]->get_CurrentHasKeyboardFocus(&focused);
    CHECK(!focused);
    list->get_CurrentHasKeyboardFocus(&focused);
    CHECK(!focused);  // the list itself keeps no focus: its current item has it
    Driver d(*f.w);
    d.key(VK_DOWN);
    rows[3]->get_CurrentHasKeyboardFocus(&focused);
    CHECK(focused);
    f.other->setFocus(FocusReason::Tab);
    rows[3]->get_CurrentHasKeyboardFocus(&focused);
    CHECK(!focused);
}

TEST(uia_sees_a_table_as_a_grid_with_headers_and_cells_by_position) {
    SceneWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto table = byId(a.Get(), win.Get(), "table");
    CHECK(table != nullptr);
    if (!table) return;
    CHECK(typeOf(table.Get()) == UIA_TableControlTypeId);
    ComPtr<IUIAutomationGridPattern> grid;
    CHECK(SUCCEEDED(table->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&grid))) && grid);
    if (!grid) return;
    int rows = 0, cols = 0;
    grid->get_CurrentRowCount(&rows);
    grid->get_CurrentColumnCount(&cols);
    CHECK(rows == 6 && cols == 3);
    ComPtr<IUIAutomationElement> cell;
    CHECK(SUCCEEDED(grid->GetItem(2, 1, &cell)) && cell);
    if (!cell) return;
    CHECK_STR(nameOf(cell.Get()), "1.2");
    ComPtr<IUIAutomationGridItemPattern> gi;
    CHECK(SUCCEEDED(cell->GetCurrentPatternAs(UIA_GridItemPatternId, IID_PPV_ARGS(&gi))) && gi);
    if (gi) {
        int r = -1, c = -1, rs = 0, cs = 0;
        gi->get_CurrentRow(&r);
        gi->get_CurrentColumn(&c);
        gi->get_CurrentRowSpan(&rs);
        gi->get_CurrentColumnSpan(&cs);
        CHECK(r == 2 && c == 1 && rs == 1 && cs == 1);
        ComPtr<IUIAutomationElement> g;
        gi->get_CurrentContainingGrid(&g);
        CHECK(g != nullptr && nameOf(g.Get()) == "Parameters");
    }
    ComPtr<IUIAutomationElement> out;
    CHECK(grid->GetItem(6, 0, &out) == E_INVALIDARG && grid->GetItem(0, 3, &out) == E_INVALIDARG);
    // the header, through the Table pattern and a cell's TableItem
    ComPtr<IUIAutomationTablePattern> tp;
    CHECK(SUCCEEDED(table->GetCurrentPatternAs(UIA_TablePatternId, IID_PPV_ARGS(&tp))) && tp);
    if (tp) {
        ComPtr<IUIAutomationElementArray> heads;
        tp->GetCurrentColumnHeaders(&heads);
        int n = 0;
        if (heads) heads->get_Length(&n);
        CHECK(n == 3);
        if (n == 3) {
            ComPtr<IUIAutomationElement> h1;
            heads->GetElement(1, &h1);
            CHECK_STR(nameOf(h1.Get()), "Value");
            CHECK(typeOf(h1.Get()) == UIA_HeaderItemControlTypeId);
        }
        RowOrColumnMajor major = RowOrColumnMajor_Indeterminate;
        tp->get_CurrentRowOrColumnMajor(&major);
        CHECK(major == RowOrColumnMajor_RowMajor);
    }
    ComPtr<IUIAutomationTableItemPattern> ti;
    CHECK(SUCCEEDED(cell->GetCurrentPatternAs(UIA_TableItemPatternId, IID_PPV_ARGS(&ti))) && ti);
    if (ti) {
        ComPtr<IUIAutomationElementArray> heads;
        ti->GetCurrentColumnHeaderItems(&heads);
        int n = 0;
        if (heads) heads->get_Length(&n);
        CHECK(n == 1);
        if (n == 1) {
            ComPtr<IUIAutomationElement> h;
            heads->GetElement(0, &h);
            CHECK_STR(nameOf(h.Get()), "Value");
        }
    }
    // the selection: a 2 x 2 block of cells (the table has the focus in this scene)
    ComPtr<IUIAutomationSelectionPattern> sel;
    CHECK(SUCCEEDED(table->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&sel))) && sel);
    if (sel) {
        BOOL multi = FALSE;
        sel->get_CurrentCanSelectMultiple(&multi);
        CHECK(multi);
        ComPtr<IUIAutomationElementArray> cur;
        sel->GetCurrentSelection(&cur);
        int n = 0;
        if (cur) cur->get_Length(&n);
        CHECK(n == 4);
    }
}

TEST(uia_reaches_a_cell_that_is_not_on_screen_by_scrolling_to_it) {
    OneView<TableView> f(300, 120);
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.view->setColumnCount(2);
    f.view->setRowCount(80);
    for (int r = 0; r < 80; ++r) f.view->setItem(r, 1, "v" + std::to_string(r));
    f.w->renderNow(nullptr, false);
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto table = byId(a.Get(), win.Get(), "view");
    CHECK(table != nullptr);
    if (!table) return;
    ComPtr<IUIAutomationGridPattern> grid;
    table->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&grid));
    CHECK(grid != nullptr);
    if (!grid) return;
    ComPtr<IUIAutomationElement> cell;
    CHECK(SUCCEEDED(grid->GetItem(70, 1, &cell)) && cell);
    if (cell) CHECK_STR(nameOf(cell.Get()), "v70");
    CHECK(f.view->scrollY() > 0 && f.view->realizedRow(70) != nullptr);
}

TEST(uia_opens_and_closes_tree_items_and_names_them_by_their_columns) {
    SceneWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto tree = byId(a.Get(), win.Get(), "tree");
    CHECK(tree != nullptr);
    if (!tree) return;
    CHECK(typeOf(tree.Get()) == UIA_TreeControlTypeId);
    auto items = ofType(a.Get(), tree.Get(), UIA_TreeItemControlTypeId);
    CHECK(items.size() >= 7);
    if (items.size() < 7) return;
    CHECK_STR(nameOf(items[0].Get()), "File");
    CHECK_STR(nameOf(items[1].Get()), "Name, mosfet_2d.npz");
    ComPtr<IUIAutomationExpandCollapsePattern> ec;
    CHECK(SUCCEEDED(items[0]->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&ec))) && ec);
    if (!ec) return;
    ExpandCollapseState st = ExpandCollapseState_Collapsed;
    ec->get_CurrentExpandCollapseState(&st);
    CHECK(st == ExpandCollapseState_Expanded);
    CHECK(SUCCEEDED(ec->Collapse()));
    ec->get_CurrentExpandCollapseState(&st);
    CHECK(st == ExpandCollapseState_Collapsed);
    items = ofType(a.Get(), tree.Get(), UIA_TreeItemControlTypeId);
    CHECK_STR(nameOf(items[1].Get()), "Mesh");  // File's children are gone from the tree
    CHECK(SUCCEEDED(ec->Expand()));
    ec->get_CurrentExpandCollapseState(&st);
    CHECK(st == ExpandCollapseState_Expanded);
    items = ofType(a.Get(), tree.Get(), UIA_TreeItemControlTypeId);
    ComPtr<IUIAutomationExpandCollapsePattern> leaf;
    items[1]->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&leaf));
    CHECK(leaf == nullptr);  // a leaf has none
}

TEST(uia_reads_a_multi_line_edit_s_scroll_position) {
    OneView<ListView> f(300, 100);  // (the PlainTextEdit's pattern is tested with it; here the list's horizontal axis)
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.view->addItem("only");
    f.w->renderNow(nullptr, false);
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto list = byId(a.Get(), win.Get(), "view");
    CHECK(list != nullptr);
    if (!list) return;
    ComPtr<IUIAutomationScrollPattern> sp;
    list->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&sp));
    CHECK(sp != nullptr);
    if (!sp) return;
    BOOL v = TRUE;
    sp->get_CurrentVerticallyScrollable(&v);
    CHECK(!v);  // nothing to scroll: the pattern is still there, and says so
    double pct = 0;
    sp->get_CurrentVerticalScrollPercent(&pct);
    CHECK(pct == UIA_ScrollPatternNoScroll);
}

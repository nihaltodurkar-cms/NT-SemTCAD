// Portable tests of N3e's list (NATIVE-DESKTOP-PLAN.md 27.8.6): the ItemView engine (realized rows, scrolling, current
// row and selection, keys, typeahead, copy, in-place editing through the host's editor, UI Automation state) as the
// ListView uses it. The table and the tree have their own file. Part of tcad_ui_core_tests: no Win32. Every number is
// worked out by hand from the fake text engine: 6 DIPs a byte at the 12-DIP UI font, a 15-DIP line, so a row is
// ceil(15 + 6) = 21 DIPs; a 200 x 105 list has a 198 x 103 viewport (4.9 rows) while it has no bar.
#include "mini_test.hpp"

#include "ui/core/clipboard.hpp"
#include "ui/core/inline_editor.hpp"
#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/widgets/list_view.hpp"

#include <map>
#include <string>
#include <vector>

using namespace tcad::ui;
using tcad::desktop::theme::T;
using tcad::platform::KeyEvent;
using tcad::platform::Mod;
using tcad::platform::MouseButton;
using tcad::platform::MouseEvent;
using tcad::platform::MouseType;
namespace keys = tcad::ui::keys;

namespace {

class FakeText final : public TextEngine {
public:
    SizeF measure(std::string_view s, const TextStyle& st) override { return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size}; }
    TextHit hitTest(std::string_view s, const TextStyle& st, float, PointF p) override {
        const float w = 0.5f * st.size;
        return {static_cast<std::size_t>(std::clamp(std::lround(p.x / w), 0L, static_cast<long>(s.size()))), true};
    }
    RectF caretRect(std::string_view, const TextStyle& st, float, std::size_t o) override { return {0.5f * st.size * static_cast<float>(o), 0, 0, 1.25f * st.size}; }
};

class FakeTimers final : public TimerService {
public:
    struct Tm {
        long long due;
        int interval;
        bool repeat;
        std::function<void()> fn;
    };
    long long now = 0;
    TimerId next = 1;
    std::map<TimerId, Tm> live;
    TimerId start(int ms, bool repeat, std::function<void()> fn) override {
        live[next] = {now + ms, ms, repeat, std::move(fn)};
        return next++;
    }
    void stop(TimerId id) override { live.erase(id); }
    void advance(int ms) {
        const long long end = now + ms;
        for (;;) {
            TimerId id = 0;
            long long best = end + 1;
            for (auto& [k, t] : live)
                if (t.due <= end && t.due < best) id = k, best = t.due;
            if (!id) break;
            now = best;
            auto fn = live[id].fn;
            if (live[id].repeat) live[id].due += live[id].interval;
            else live.erase(id);
            fn();
        }
        now = end;
    }
};

class FakeClipboard final : public Clipboard {
public:
    std::optional<std::string> value;
    void setText(std::string_view s) override { value = std::string(s); }
    std::optional<std::string> text() override { return value; }
};

class FakeEditor final : public InlineEditor {
public:
    std::string value;
    bool selected_all = false;
    FakeEditor() { setFocusPolicy(FocusPolicy::Strong); }
    std::string text() const override { return value; }
    void setText(std::string_view s) override { value = std::string(s); }
    void selectAll() override { selected_all = true; }
    bool keyEvent(const KeyEvent& e) override {
        if (!e.down) return false;
        if (e.vk == keys::Return && on_finished) return on_finished(true), true;
        if (e.vk == keys::Escape && on_finished) return on_finished(false), true;
        return false;
    }
    void focusChanged(bool in, FocusReason) override {
        if (!in && on_finished) on_finished(true);  // the focus left: committed, as the real editor does
    }
};

class Host final : public UiHost {
public:
    FakeText text;
    FakeTimers clock;
    FakeClipboard board;
    Widget root;
    InputRouter router{root};
    bool editing_allowed = true;
    FakeEditor* last_editor = nullptr;
    int editors_made = 0;
    Host() {
        root.setHost(this);
        root.setGeometry({0, 0, 500, 400});
        router.setTimers(&clock);
    }
    ~Host() override { root.setHost(nullptr); }
    void invalidate(const RectI&) override {}
    void scheduleLayout() override {}
    TextEngine& textEngine() override { return text; }
    double scale() const override { return 1.0; }
    InputRouter* input() override { return &router; }
    TimerService* timers() override { return &clock; }
    Clipboard* clipboard() override { return &board; }
    bool canCreateInlineEditor() const override { return editing_allowed; }
    std::unique_ptr<InlineEditor> createInlineEditor() override {
        if (!editing_allowed) return nullptr;
        auto e = std::make_unique<FakeEditor>();
        last_editor = e.get();
        ++editors_made;
        return e;
    }
    void widgetGone(Widget* w) override {
        if (w == last_editor) last_editor = nullptr;
        router.widgetGone(w);
    }
};

constexpr unsigned kLeftBit = 1u << static_cast<unsigned>(MouseButton::Left);

MouseEvent mouse(MouseType t, float x, float y, unsigned held = 0, Mod mods = Mod::None, MouseButton b = MouseButton::Left) {
    MouseEvent e;
    e.type = t;
    e.button = t == MouseType::Move ? MouseButton::None : b;
    e.x = x;
    e.y = y;
    e.buttons_down = held;
    e.mods = mods;
    return e;
}
MouseEvent wheel(float x, float y, double steps) {
    MouseEvent e;
    e.type = MouseType::Wheel;
    e.x = x;
    e.y = y;
    e.wheel_steps = steps;
    return e;
}
KeyEvent key(int vk, Mod m = Mod::None) { return {vk, m, true, false}; }

// A list at (10, 10), 200 x 105. Row i's centre is at window y = 21 + 21 i + 10.5 - 10 = 11 + 21 i + 10.5.
struct Rig {
    Host h;
    ListView* l;
    std::vector<std::string> log;
    explicit Rig(int items = 0, const char* stem = "item ") {
        l = h.root.addChild<ListView>();
        l->setGeometry({10, 10, 200, 105});
        for (int i = 0; i < items; ++i) l->addItem(stem + std::to_string(i));
        l->on_current_row_changed = [this](int r) { log.push_back("current " + std::to_string(r)); };
        l->on_item_changed = [this](int r) { log.push_back("item " + std::to_string(r)); };
        l->on_row_activated = [this](int r) { log.push_back("activated " + std::to_string(r)); };
    }
    static float Y(int row) { return 11 + 21.0f * static_cast<float>(row) + 10.5f; }
    static float X(float x = 40) { return 11 + x; }
    void click(int row, float x = 40, Mod m = Mod::None) {
        h.router.mouse(mouse(MouseType::Down, X(x), Y(row), kLeftBit, m));
        h.router.mouse(mouse(MouseType::Up, X(x), Y(row), 0, m));
    }
    void dblclick(int row, float x = 40) {
        h.router.mouse(mouse(MouseType::Down, X(x), Y(row), kLeftBit));
        h.router.mouse(mouse(MouseType::Up, X(x), Y(row), 0));
        h.router.mouse(mouse(MouseType::DoubleClick, X(x), Y(row), kLeftBit));
        h.router.mouse(mouse(MouseType::Up, X(x), Y(row), 0));
    }
    void key(int vk, Mod m = Mod::None) { h.router.key(::key(vk, m)); }
    void type(const std::string& s) {
        for (char c : s) h.router.character(static_cast<char32_t>(c));
    }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
    std::string sel() const {
        std::string s;
        for (int r : l->selectedRows()) s += (s.empty() ? "" : ",") + std::to_string(r);
        return s;
    }
};

bool is(const std::string& got, const std::string& want) {
    if (got != want) std::printf("  got      [%s]%c  expected [%s]%c", got.c_str(), 10, want.c_str(), 10);
    return got == want;
}
#define CHECK_STR(a, b) CHECK(is((a), (b)))

}  // namespace

// -- items, geometry and realization ------------------------------------------------------------------------------

TEST(an_empty_list_has_no_rows_no_current_and_no_bars) {
    Rig r;
    CHECK(r.l->count() == 0 && r.l->currentRow() == -1 && r.l->realizedRows() == 0);
    CHECK(r.l->sizeHint() == (SizeF{256, 192}) && r.l->minimumSizeHint() == (SizeF{70, 70}));
    CHECK(!r.l->verticalScrollBar()->isVisibleSelf() && !r.l->horizontalScrollBar()->isVisibleSelf());
    CHECK(r.l->selectedRows().empty() && r.l->firstVisibleRow() == -1 && r.l->lastVisibleRow() == -1);
    CHECK(r.l->accessibleIsSelectionContainer() && r.l->focusPolicy() == FocusPolicy::Strong);
}

TEST(the_first_item_added_is_not_current_and_rows_are_21_dips_in_the_viewport) {
    Rig r;
    r.l->addItem("alpha");
    CHECK(r.l->currentRow() == -1 && r.take().empty());  // Qt's QListWidget: nothing is current until the user or the program says
    r.l->addItem("beta");
    r.l->addItem("gamma");
    CHECK(r.l->rowHeight() == 21 && r.l->realizedRows() == 3);
    CHECK((r.l->viewportRect() == RectF{1, 1, 198, 103}));
    ItemRow* row1 = r.l->realizedRow(1);
    CHECK(row1 != nullptr && row1->row() == 1);
    if (row1) {
        CHECK((row1->geometry() == RectI{0, 21, 198, 21}));  // relative to the viewport
        CHECK_STR(row1->accessibleName, "beta");
    }
    CHECK((r.l->rowRect(2) == RectF{1, 43, 198, 21}));
    CHECK(r.l->rowAt({50, 12}) == 0 && r.l->rowAt({50, 22}) == 1 && r.l->rowAt({50, 63}) == 2 && r.l->rowAt({50, 70}) == -1);
    CHECK(r.l->rowAt({0.5f, 12}) == -1 && r.l->rowAt({50, 0.5f}) == -1);  // on the frame
    CHECK(r.l->itemText(2) == "gamma" && r.l->itemText(7).empty());
}

TEST(only_the_rows_in_view_are_realized_and_they_are_recycled_as_it_scrolls) {
    Rig r(1000);
    CHECK(r.l->verticalScrollBar()->isVisibleSelf());
    CHECK((r.l->viewportRect() == RectF{1, 1, 184, 103}));  // 14 DIPs of bar
    CHECK(r.l->realizedRows() == 5);                         // ceil(103 / 21) = 5 rows touch the viewport
    CHECK(r.l->firstVisibleRow() == 0 && r.l->lastVisibleRow() == 4);
    CHECK(r.l->verticalScrollBar()->maximum() == 21000 - 103 && r.l->verticalScrollBar()->singleStep() == 21);
    CHECK(r.l->viewport()->children().size() == 5);  // the realized rows are the viewport's only children
    r.l->verticalScrollBar()->setValue(210);
    CHECK(r.l->viewport()->children().size() == 5);  // the ones scrolled out were released, not left behind
    CHECK(r.l->realizedRows() == 5 && r.l->realizedRow(10) != nullptr && r.l->realizedRow(0) == nullptr);
    CHECK((r.l->realizedRow(10)->geometry() == RectI{0, 0, 184, 21}));
    r.l->verticalScrollBar()->setValue(220);  // not on a row boundary: 6 rows touch
    CHECK(r.l->realizedRows() == 6 && r.l->firstVisibleRow() == 10 && r.l->lastVisibleRow() == 15);
    CHECK((r.l->realizedRow(10)->geometry() == RectI{0, -10, 184, 21}));  // the top one is cut by the viewport
    CHECK_STR(r.l->realizedRow(12)->accessibleName, "item 12");
    r.l->verticalScrollBar()->setValue(r.l->verticalScrollBar()->maximum());
    CHECK(r.l->lastVisibleRow() == 999 && r.l->realizedRow(999) != nullptr);
}

TEST(rows_cut_by_the_viewport_do_not_paint_over_the_bars_and_clicks_there_miss_them) {
    Rig r(1000);
    r.l->verticalScrollBar()->setValue(220);
    // the top row (10) is cut: its visible part is y 1..12 (11 DIPs). A press on the frame row above it is not on a row
    r.h.router.mouse(mouse(MouseType::Down, 50, 10.5f, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 50, 10.5f, 0));
    CHECK(r.l->currentRow() == -1);
    r.h.router.mouse(mouse(MouseType::Down, 50, 15, kLeftBit));  // inside the cut row
    r.h.router.mouse(mouse(MouseType::Up, 50, 15, 0));
    CHECK(r.l->currentRow() == 10);
    CHECK(r.l->scrollY() == 210);  // selecting a row scrolls it whole into view
}

// -- the current row, signals and keys ----------------------------------------------------------------------------

TEST(a_click_makes_a_row_current_and_selected_and_reports_it) {
    Rig r(5);
    r.click(2);
    CHECK(r.l->currentRow() == 2 && r.sel() == "2");
    CHECK_STR(r.take(), "current 2");
    r.click(2);  // the same row: nothing to report
    CHECK_STR(r.take(), "");
    r.click(4);
    CHECK_STR(r.take(), "current 4");
    CHECK(r.h.router.focusWidget() == r.l);  // a click gives the list the focus (not a row)
}

TEST(the_programs_changes_report_too_and_silent_or_blocked_ones_do_not) {
    Rig r(5);
    r.l->setCurrentRow(3);
    CHECK_STR(r.take(), "current 3");
    r.l->setCurrentRowSilent(1);
    CHECK(r.l->currentRow() == 1 && r.log.empty());
    r.l->setSignalsBlocked(true);
    r.l->setCurrentRow(2);
    r.l->setItemText(2, "changed");
    CHECK(r.l->currentRow() == 2 && r.log.empty());
    r.l->setSignalsBlocked(false);
    r.l->setCurrentRow(-1);
    CHECK_STR(r.take(), "current -1");
    CHECK(r.l->selectedRows().empty());
    r.l->setCurrentRow(99);  // out of range: no current
    CHECK(r.l->currentRow() == -1);
    r.l->setCurrentRow(1);
    r.take();
    r.l->setCurrentRow(-5);
    CHECK_STR(r.take(), "current -1");
}

TEST(removing_the_current_row_makes_its_neighbour_current_and_clear_leaves_none) {
    Rig r(5);
    r.l->setCurrentRow(2);
    r.take();
    r.l->removeItem(2);
    CHECK(r.l->count() == 4 && r.l->currentRow() == 2 && r.l->itemText(2) == "item 3");
    CHECK_STR(r.take(), "current 2");  // a different item is current now (the neighbour took the place), so it is reported
    r.l->setCurrentRow(3);
    r.take();
    r.l->removeItem(3);  // the last one: the one before it
    CHECK(r.l->currentRow() == 2);
    CHECK_STR(r.take(), "current 2");
    r.take();
    r.l->removeItem(0);  // before the current: it moves up with its row, the same item: nothing to report
    CHECK(r.l->currentRow() == 1 && r.l->itemText(1) == "item 3");
    CHECK_STR(r.take(), "");
    r.l->clear();
    CHECK(r.l->count() == 0 && r.l->currentRow() == -1 && r.l->realizedRows() == 0);
    CHECK_STR(r.take(), "current -1");
    r.l->clear();  // already empty
    CHECK_STR(r.take(), "");
    r.l->insertItem(0, "x");
    CHECK(r.l->currentRow() == -1);
}

TEST(inserting_before_the_current_row_moves_it_with_its_text) {
    Rig r(3);
    r.l->setCurrentRow(1);
    r.take();
    ItemRow* before = r.l->realizedRow(1);
    r.l->insertItem(0, "new");
    CHECK(r.l->realizedRow(2) == before && before->row() == 2);  // the row keeps its widget and is re-indexed
    CHECK_STR(before->accessibleName, "item 1");
    CHECK(r.l->currentRow() == 2 && r.l->itemText(2) == "item 1" && r.l->isRowSelected(2) && !r.l->isRowSelected(1));
    CHECK_STR(r.take(), "");
}

TEST(arrow_page_home_and_end_keys_move_the_current_row) {
    Rig r(30);
    r.l->setFocus(FocusReason::Tab);
    r.key(keys::Down);
    CHECK(r.l->currentRow() == 0);  // none yet: the first
    r.key(keys::Down);
    r.key(keys::Down);
    CHECK(r.l->currentRow() == 2);
    r.key(keys::Up);
    CHECK(r.l->currentRow() == 1);
    r.key(keys::Home);
    CHECK(r.l->currentRow() == 0);
    r.key(keys::Up);
    CHECK(r.l->currentRow() == 0);  // the first: stays
    r.key(keys::End);
    CHECK(r.l->currentRow() == 29 && r.l->scrollY() == r.l->verticalScrollBar()->maximum());  // scrolled to show it
    r.key(keys::Down);
    CHECK(r.l->currentRow() == 29);
    r.key(keys::Home);
    r.key(keys::PageDown);  // a view of rows less one: floor(103 / 21) - 1 = 3
    CHECK(r.l->currentRow() == 3);
    r.key(keys::PageDown);
    r.key(keys::PageDown);
    CHECK(r.l->currentRow() == 9);
    r.key(keys::PageUp);
    CHECK(r.l->currentRow() == 6);
    r.take();
    r.key(keys::Return);
    CHECK_STR(r.take(), "activated 6");
    r.l->setCurrentRow(-1);
    r.take();
    r.key(keys::Return);
    CHECK_STR(r.take(), "");  // nothing current: Enter is not used
}

TEST(a_view_keeps_the_current_row_visible_when_it_moves_by_key) {
    Rig r(30);
    r.l->setFocus(FocusReason::Tab);
    for (int i = 0; i < 8; ++i) r.key(keys::Down);
    CHECK(r.l->currentRow() == 7);
    CHECK(r.l->scrollY() == 65);  // the least scrolling that shows row 7 whole: its bottom is 8 * 21 = 168, the viewport 103
    const float bottom = r.l->rowRect(7).bottom();
    CHECK(bottom <= r.l->viewportRect().bottom() + 0.01f);
    for (int i = 0; i < 7; ++i) r.key(keys::Up);
    CHECK(r.l->currentRow() == 0 && r.l->scrollY() == 0);
}

TEST(disabled_and_unselectable_rows_cannot_be_current_and_keys_skip_them) {
    Rig r(6);
    r.l->setItemSelectable(0, false);  // a header row, as the field list's
    r.l->setItemEnabled(2, false);
    r.l->setItemEnabled(3, false);
    r.l->setFocus(FocusReason::Tab);
    r.key(keys::Down);
    CHECK(r.l->currentRow() == 1);  // not the header
    r.key(keys::Down);
    CHECK(r.l->currentRow() == 4);  // 2 and 3 are skipped
    r.key(keys::Up);
    CHECK(r.l->currentRow() == 1);
    r.key(keys::Up);
    CHECK(r.l->currentRow() == 1);  // row 0 is not selectable
    r.key(keys::Home);
    CHECK(r.l->currentRow() == 1);
    r.key(keys::End);
    CHECK(r.l->currentRow() == 5);
    r.take();
    r.l->scrollToTop();  // (End scrolled the six rows)
    r.click(2);  // a press on a disabled row does nothing
    r.click(0);
    CHECK(r.l->currentRow() == 5 && r.take().empty());
    r.l->setCurrentRow(3);  // nor can the program
    CHECK(r.l->currentRow() == 5);
}

// -- selection --------------------------------------------------------------------------------------------------

TEST(extended_selection_takes_ctrl_toggle_shift_range_and_ctrl_a) {
    Rig r(10);
    r.l->setGeometry({10, 10, 200, 260});  // tall enough to click every row
    r.l->setSelectionMode(ItemView::SelectionMode::Extended);
    r.click(2);
    CHECK_STR(r.sel(), "2");
    r.click(4, 40, Mod::Ctrl);
    CHECK_STR(r.sel(), "2,4");
    r.click(2, 40, Mod::Ctrl);  // toggled off
    CHECK_STR(r.sel(), "4");
    r.click(1);
    r.click(3, 40, Mod::Shift);
    CHECK_STR(r.sel(), "1,2,3");
    r.click(0, 40, Mod::Shift);  // the anchor stays at 1
    CHECK_STR(r.sel(), "0,1");
    r.take();
    r.key('A', Mod::Ctrl);
    CHECK_STR(r.sel(), "0,1,2,3,4,5,6,7,8,9");
    r.click(5);
    CHECK_STR(r.sel(), "5");
    r.key(keys::Down, Mod::Shift);
    r.key(keys::Down, Mod::Shift);
    CHECK_STR(r.sel(), "5,6,7");
    r.key(keys::Up, Mod::Shift);
    CHECK_STR(r.sel(), "5,6");
    r.key(keys::Down, Mod::Ctrl);  // the current row alone moves
    CHECK(r.l->currentRow() == 7 && r.sel() == "5,6");
    r.key(keys::Space, Mod::Ctrl);  // and Ctrl+Space toggles it
    CHECK_STR(r.sel(), "5,6,7");
    r.key(keys::Down);  // a plain move selects only the new row
    CHECK_STR(r.sel(), "8");
    CHECK(r.l->accessibleCanSelectMultiple());
}

TEST(a_drag_selects_a_range_in_extended_mode_and_moves_the_current_row_in_single_mode) {
    Rig r(10);
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(), Rig::Y(1), kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(), Rig::Y(3), kLeftBit));
    CHECK(r.l->currentRow() == 3 && r.sel() == "3");
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(), Rig::Y(3), 0));
    r.l->setSelectionMode(ItemView::SelectionMode::Extended);
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(), Rig::Y(1), kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(), Rig::Y(3), kLeftBit));
    CHECK_STR(r.sel(), "1,2,3");
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(), Rig::Y(0), kLeftBit));
    CHECK_STR(r.sel(), "0,1");
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(), Rig::Y(0), 0));
}

TEST(selection_none_selects_nothing_and_a_mode_change_keeps_only_the_current_row) {
    Rig r(5);
    r.l->setSelectionMode(ItemView::SelectionMode::None);
    r.click(2);
    CHECK(r.l->selectedRows().empty() && !r.l->isRowSelected(2));
    r.l->setSelectionMode(ItemView::SelectionMode::Extended);
    r.l->setCurrentRow(1);
    r.click(3, 40, Mod::Ctrl);
    CHECK_STR(r.sel(), "1,3");
    r.l->setSelectionMode(ItemView::SelectionMode::Single);
    CHECK_STR(r.sel(), "3");  // the single selection is the current row
}

// -- mouse and wheel ---------------------------------------------------------------------------------------------

TEST(the_wheel_scrolls_three_rows_a_notch_over_the_list_with_or_without_the_focus) {
    Rig r(100);
    r.h.router.mouse(wheel(Rig::X(), Rig::Y(1), -1));  // toward the user: down
    CHECK(r.l->scrollY() == 63);
    r.h.router.mouse(wheel(Rig::X(), Rig::Y(1), 1));
    CHECK(r.l->scrollY() == 0);
    r.h.router.mouse(wheel(Rig::X(), Rig::Y(1), 1));
    CHECK(r.l->scrollY() == 0);
    Rig few(3);
    few.h.router.mouse(wheel(Rig::X(), Rig::Y(1), -1));  // nothing to scroll: not used
    CHECK(few.l->scrollY() == 0);
}

TEST(a_right_press_selects_an_unselected_row_but_keeps_a_selection_it_lands_in) {
    Rig r(6);
    r.l->setGeometry({10, 10, 200, 260});
    r.l->setSelectionMode(ItemView::SelectionMode::Extended);
    r.click(1);
    r.click(3, 40, Mod::Shift);
    CHECK_STR(r.sel(), "1,2,3");
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(), Rig::Y(2), kLeftBit << 1, Mod::None, MouseButton::Right));
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(), Rig::Y(2), 0, Mod::None, MouseButton::Right));
    CHECK_STR(r.sel(), "1,2,3");  // inside it: kept, for a context menu to act on
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(), Rig::Y(5), kLeftBit << 1, Mod::None, MouseButton::Right));
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(), Rig::Y(5), 0, Mod::None, MouseButton::Right));
    CHECK_STR(r.sel(), "5");
}

// -- typeahead and copy -------------------------------------------------------------------------------------------

TEST(typing_selects_the_next_row_starting_with_it_and_the_same_letter_cycles) {
    Rig r;
    for (const char* t : {"Alpha", "Beta", "Bravo", "Charlie", "alpine"}) r.l->addItem(t);
    r.l->setFocus(FocusReason::Tab);
    r.type("b");
    CHECK(r.l->currentRow() == 1);
    r.h.clock.advance(300);
    r.type("b");  // the same letter again: the next one
    CHECK(r.l->currentRow() == 2);
    r.h.clock.advance(300);
    r.type("b");
    CHECK(r.l->currentRow() == 1);  // wrapped
    r.h.clock.advance(1100);        // forgotten
    r.type("br");
    CHECK(r.l->currentRow() == 2);
    r.h.clock.advance(1100);
    r.type("A");
    CHECK(r.l->currentRow() == 4);  // case folded, from after the current row
    r.h.clock.advance(1100);
    r.type("zz");
    CHECK(r.l->currentRow() == 4);  // nothing starts with it: stays
    r.h.clock.advance(1100);
    r.type(" ");  // a space first is not typeahead (Space belongs to check boxes)
    CHECK(r.l->currentRow() == 4);
}

TEST(copy_puts_the_selected_rows_text_on_the_clipboard_one_per_line) {
    Rig r(6, "row ");
    r.click(2);
    r.key('C', Mod::Ctrl);
    CHECK(r.h.board.value == std::optional<std::string>("row 2"));
    r.h.board.value.reset();
    r.l->setSelectionMode(ItemView::SelectionMode::Extended);
    r.click(1);
    r.click(3, 40, Mod::Shift);
    r.key(keys::Insert, Mod::Ctrl);
    CHECK(r.h.board.value == std::optional<std::string>("row 1\nrow 2\nrow 3"));
    r.l->setCurrentRow(-1);
    r.h.board.value.reset();
    CHECK(!r.l->copy() && !r.h.board.value);  // nothing selected: the clipboard is left alone
}

// -- items: data, flags and looks --------------------------------------------------------------------------------

TEST(items_carry_user_data_under_roles_and_are_found_by_it) {
    Rig r(3);
    r.l->setItemData(0, 0, std::string("id-a"));
    r.l->setItemData(1, 0, std::string("id-b"));
    r.l->setItemData(1, 7, 42);
    r.l->setItemData(2, 7, true);
    CHECK(std::get<std::string>(r.l->itemData(1, 0)) == "id-b" && std::get<int>(r.l->itemData(1, 7)) == 42);
    CHECK(r.l->itemData(0, 7).index() == 0 && r.l->itemData(9, 0).index() == 0);  // none: empty
    CHECK(r.l->findData(std::string("id-b")) == 1 && r.l->findData(std::string("nope")) == -1);
    CHECK(r.l->findData(true, 7) == 2 && r.l->findData(42, 7) == 1);
    r.l->setCurrentRow(1);
    CHECK(std::get<std::string>(r.l->currentData()) == "id-b" && r.l->currentText() == "item 1");
    r.l->setCurrentRow(-1);
    CHECK(r.l->currentData().index() == 0 && r.l->currentText().empty());
}

TEST(a_checkable_item_toggles_from_its_box_and_from_space_and_reports_it) {
    Rig r(3);
    r.l->setItemCheckable(1, true);
    r.take();
    CHECK(r.l->accessibleSelectionContainer() == nullptr);  // (the list itself has no container)
    r.click(1, 12);  // on the box (6..20)
    CHECK(r.l->isItemChecked(1) && r.l->currentRow() == 1);
    CHECK_STR(r.take(), "item 1, current 1");  // toggled, then current
    r.click(1, 12);
    CHECK(!r.l->isItemChecked(1));
    r.take();
    r.click(1, 80);  // on the text: selects, does not toggle
    CHECK(!r.l->isItemChecked(1) && r.take().empty());
    r.key(keys::Space);
    CHECK(r.l->isItemChecked(1));
    CHECK_STR(r.take(), "item 1");
    r.l->setItemChecked(1, false);  // the program's change reports too
    CHECK_STR(r.take(), "item 1");
    r.l->setItemChecked(1, false);
    CHECK_STR(r.take(), "");
    r.l->setSignalsBlocked(true);
    r.l->setItemChecked(1, true);
    r.l->setSignalsBlocked(false);
    CHECK(r.l->isItemChecked(1) && r.log.empty());
    r.l->setItemEnabled(1, false);
    r.click(1, 12);
    r.key(keys::Space);
    CHECK(r.l->isItemChecked(1));  // disabled: no toggling
    // an item that is not checkable: Space is not used
    r.l->setCurrentRow(0);
    r.take();
    r.key(keys::Space);
    CHECK(r.log.empty());
}

TEST(a_checkable_rows_accessible_toggle_state_follows_it) {
    Rig r(2);
    r.l->setItemCheckable(0, true);
    ItemRow* a = r.l->realizedRow(0);
    ItemRow* b = r.l->realizedRow(1);
    CHECK(a && b && a->accessibleToggleState() == 0 && b->accessibleToggleState() == -1);
    a->accessibleToggle();
    CHECK(r.l->isItemChecked(0) && a->accessibleToggleState() == 1);
    b->accessibleToggle();  // not checkable: nothing
    CHECK(!r.l->isItemChecked(1));
}

TEST(items_paint_their_flags_selection_fill_check_box_italic_and_a_disabled_faint_text) {
    Rig r(4);
    r.l->setItemCheckable(0, true);
    r.l->setItemChecked(0, true);
    r.l->setItemItalic(1, true);
    r.l->setItemBold(1, true);
    r.l->setItemEnabled(2, false);
    r.l->setItemColor(3, T::Error);
    r.l->setCurrentRow(1);
    r.l->setFocus(FocusReason::Tab);
    auto paintRow = [&](int row) {
        auto rec = std::make_unique<RecordingPainter>();
        r.l->realizedRow(row)->paint(*rec);
        return rec;
    };
    auto p0 = paintRow(0);
    int lines = 0;
    for (const auto& op : p0->ops()) lines += op.kind == "line";
    CHECK(lines == 2);  // the check mark: two strokes
    bool text_after_box = false;
    for (const auto& op : p0->ops())
        if (op.kind == "text" && op.text == "item 0") text_after_box = op.rect.x == 6 + 14 + 6;
    CHECK(text_after_box);
    auto p1 = paintRow(1);
    bool sel_fill = false, styled = false, focus_frame = false;
    for (const auto& op : p1->ops()) {
        if (op.kind == "fillRect" && op.rect == (RectF{0, 0, 198, 21})) sel_fill = op.color.b == token(T::Selection).b;
        if (op.kind == "text" && op.text == "item 1") styled = op.style.italic && op.style.bold;
        if (op.kind == "strokeRect" && op.color.g == token(T::Focus).g) focus_frame = true;
    }
    CHECK(sel_fill && styled && focus_frame);
    auto p2 = paintRow(2);
    bool faint = false;
    for (const auto& op : p2->ops())
        if (op.kind == "text" && op.text == "item 2") faint = op.style.color.g == token(T::TextFaint).g;
    CHECK(faint);
    auto p3 = paintRow(3);
    bool red = false;
    for (const auto& op : p3->ops())
        if (op.kind == "text" && op.text == "item 3") red = op.style.color.g == token(T::Error).g;
    CHECK(red);
    r.l->clearFocus();  // the selection stays visible, in the inactive colour
    auto q1 = paintRow(1);
    bool inactive = false, frame = false;
    for (const auto& op : q1->ops()) {
        if (op.kind == "fillRect" && op.rect == (RectF{0, 0, 198, 21})) inactive = op.color.b == token(T::AlternateBase).b;
        if (op.kind == "strokeRect" && op.color.g == token(T::Focus).g) frame = true;
    }
    CHECK(inactive && !frame);  // and no focus frame without the focus
}

TEST(in_high_contrast_a_selected_rows_text_is_highlight_text_and_an_inactive_one_is_outlined) {
    const HighContrast saved = highContrast();
    highContrast() = {true, Color::rgb(0x000000), Color::rgb(0xFFFFFF), Color::rgb(0x1AEBFF), Color::rgb(0x000000), Color::rgb(0x3FF23F)};
    Rig r(3);
    r.l->setCurrentRow(1);
    r.l->setFocus(FocusReason::Tab);
    RecordingPainter a;
    r.l->realizedRow(1)->paint(a);
    bool black_text = false;
    for (const auto& op : a.ops())
        if (op.kind == "text" && op.text == "item 1") black_text = op.style.color.r == 0.0f;
    CHECK(black_text);
    r.l->clearFocus();
    RecordingPainter i;
    r.l->realizedRow(1)->paint(i);
    int outlines = 0, fills = 0;
    for (const auto& op : i.ops()) {
        outlines += op.kind == "strokeRect" && op.rect.width > 150;
        fills += op.kind == "fillRect";
    }
    CHECK(outlines == 1 && fills == 0);
    highContrast() = saved;
}

// -- in-place editing ---------------------------------------------------------------------------------------------

TEST(f2_and_a_double_click_edit_an_editable_item_in_place_and_enter_commits) {
    Rig r(3);
    r.l->setItemEditable(1, true);
    r.l->setCurrentRow(1);
    r.take();
    r.l->setFocus(FocusReason::Tab);
    r.key(keys::F2);
    CHECK(r.l->isEditing() && r.h.last_editor != nullptr);
    FakeEditor* ed = r.h.last_editor;
    if (!ed) return;
    CHECK_STR(ed->value, "item 1");
    CHECK(ed->selected_all);
    CHECK(ed->parent() == r.l->realizedRow(1)->parent());  // in the viewport, over the row
    // over the text, inside the row: x from the text start less a little, the row's height less 2
    CHECK((ed->geometry() == RectI{3, 22, 192, 19}));
    CHECK(r.h.router.focusWidget() == ed);
    ed->setText("renamed");
    r.h.router.key(key(keys::Return));
    CHECK(!r.l->isEditing());
    CHECK_STR(r.l->itemText(1), "renamed");
    CHECK_STR(r.take(), "item 1");
    CHECK(r.h.router.focusWidget() == r.l);  // the list has the focus back
    // a double click does the same
    r.dblclick(1);
    CHECK(r.l->isEditing());
    CHECK_STR(r.h.last_editor->value, "renamed");
    r.h.last_editor->setText("again");
    r.h.router.key(key(keys::Escape));
    CHECK(!r.l->isEditing() && r.l->itemText(1) == "renamed");  // cancelled: unchanged, and nothing reported
    CHECK_STR(r.take(), "");
}

TEST(editing_ends_without_a_change_report_when_the_text_is_the_same_and_commits_when_the_focus_leaves) {
    Rig r(3);
    r.l->setItemEditable(0, true);
    r.l->setCurrentRow(0);
    r.take();
    r.l->editItem(0);
    CHECK(r.l->isEditing());
    r.h.last_editor->setText("moved on");
    r.l->clearFocus();  // not the editor's focus yet: it took it. Take it away.
    r.h.router.setFocus(nullptr, FocusReason::Mouse);
    CHECK(!r.l->isEditing());
    CHECK_STR(r.l->itemText(0), "moved on");  // the focus leaving commits (Qt's default)
    CHECK(r.h.router.focusWidget() == nullptr);  // and the list does not steal it back
}

TEST(items_that_are_not_editable_or_a_host_without_an_editor_do_not_edit) {
    Rig r(3);
    r.l->setCurrentRow(1);
    r.l->setFocus(FocusReason::Tab);
    r.take();
    r.key(keys::F2);
    CHECK(!r.l->isEditing() && r.h.editors_made == 0);  // not editable
    r.dblclick(1);
    CHECK(!r.l->isEditing());
    CHECK_STR(r.take(), "activated 1");  // an ordinary double click: activated
    r.l->setItemEditable(1, true);
    r.h.editing_allowed = false;
    r.key(keys::F2);
    r.dblclick(1);
    CHECK(!r.l->isEditing() && r.h.editors_made == 0);
    CHECK_STR(r.take(), "activated 1");
    r.l->editItem(1);
    CHECK(!r.l->isEditing());
}

TEST(removing_rows_or_clearing_closes_an_open_editor_without_committing) {
    Rig r(3);
    r.l->setItemEditable(1, true);
    r.l->editItem(1);
    CHECK(r.l->isEditing());
    r.h.last_editor->setText("lost");
    r.l->removeItem(0);
    CHECK(!r.l->isEditing() && r.l->itemText(0) == "item 1");
    r.l->editItem(0);
    CHECK(r.l->isEditing());
    r.l->clear();
    CHECK(!r.l->isEditing() && r.l->count() == 0);
}

// -- UI Automation state ------------------------------------------------------------------------------------------

TEST(rows_are_list_items_with_names_selection_state_and_a_container) {
    Rig r(5, "name ");
    r.l->setItemEnabled(3, false);
    r.l->setItemToolTip(1, "tip one");
    r.l->setCurrentRow(2);
    ItemRow* a = r.l->realizedRow(2);
    CHECK(a && a->accessibleRole() == Role::ListItem && a->accessibleName == "name 2");
    CHECK(a->accessibleSelectionState() == 1 && r.l->realizedRow(1)->accessibleSelectionState() == 0);
    CHECK(r.l->realizedRow(3)->accessibleSelectionState() == -1);  // disabled: not selectable
    CHECK(r.l->realizedRow(1)->toolTip == "tip one");
    CHECK(a->accessibleSelectionContainer() == r.l);
    CHECK(r.l->accessibleSelection() == std::vector<Widget*>{a});
    CHECK(!r.l->accessibleCanSelectMultiple() && !r.l->accessibleSelectionRequired());
    // selecting through UI Automation: exclusive, and it speaks
    r.take();
    r.l->realizedRow(4)->accessibleSelect();
    CHECK(r.l->currentRow() == 4 && r.take() == "current 4");
    r.l->realizedRow(3)->accessibleSelect();  // disabled: nothing
    CHECK(r.l->currentRow() == 4);
    // a single-selection list: AddToSelection works only with nothing selected, Remove never
    CHECK(!r.l->realizedRow(0)->accessibleAddToSelection() && !r.l->realizedRow(4)->accessibleRemoveFromSelection());
    CHECK(r.l->realizedRow(4)->accessibleAddToSelection());  // already the selection
    r.l->setCurrentRow(-1);
    CHECK(r.l->realizedRow(0)->accessibleAddToSelection() && r.l->currentRow() == 0);
    CHECK(r.l->viewport()->accessibleIsStructural());
}

TEST(an_extended_list_adds_and_removes_rows_through_ui_automation) {
    Rig r(5);
    r.l->setSelectionMode(ItemView::SelectionMode::Extended);
    r.l->realizedRow(1)->accessibleSelect();
    CHECK(r.l->realizedRow(3)->accessibleAddToSelection());
    CHECK_STR(r.sel(), "1,3");
    CHECK(r.l->realizedRow(1)->accessibleRemoveFromSelection());
    CHECK_STR(r.sel(), "3");
    CHECK(r.l->accessibleSelection().size() == 1 && r.l->accessibleCanSelectMultiple());
}

TEST(the_list_reports_its_scroll_position_and_scrolls_on_request) {
    Rig r(100);
    AccessibleScroll s = r.l->accessibleScroll();
    CHECK(s.valid && s.vertical && !s.horizontal && s.v_percent == 0);
    CHECK(s.v_view > 4.9 && s.v_view < 5.0);  // 103 of 2100 DIPs
    r.l->accessibleScrollBy(0, 2);  // a large increment: a page
    CHECK(r.l->scrollY() == r.l->verticalScrollBar()->pageStep());
    r.l->accessibleScrollBy(0, -1);  // a small decrement: a row
    CHECK(r.l->scrollY() == r.l->verticalScrollBar()->pageStep() - 21);
    r.l->accessibleSetScrollPercent(-1, 50);
    const int half = r.l->verticalScrollBar()->maximum() / 2;
    CHECK(std::abs(r.l->scrollY() - half) <= 1);
    r.l->accessibleSetScrollPercent(-1, 100);
    CHECK(r.l->scrollY() == r.l->verticalScrollBar()->maximum() && r.l->accessibleScroll().v_percent == 100);
    r.l->scrollToTop();
    r.l->realizedRow(3)->accessibleScrollIntoView();  // already whole: nothing
    CHECK(r.l->scrollY() == 0);
    r.l->verticalScrollBar()->setValue(500);
    ItemRow* row = r.l->realizedRow(r.l->firstVisibleRow());
    row->accessibleScrollIntoView();  // cut at the top: scrolled so it shows whole
    CHECK(r.l->scrollY() % 21 == 0 || r.l->scrollY() == 504);
}

TEST(a_list_destroyed_while_the_pointer_is_over_a_row_or_a_row_is_pressed_is_safe) {
    auto host = std::make_unique<Host>();
    ListView* l = host->root.addChild<ListView>();
    l->setGeometry({10, 10, 200, 105});
    for (int i = 0; i < 20; ++i) l->addItem("x" + std::to_string(i));
    host->router.mouse(mouse(MouseType::Move, Rig::X(), Rig::Y(1), 0));
    host->router.mouse(mouse(MouseType::Down, Rig::X(), Rig::Y(1), kLeftBit));  // the row holds the grab
    l->clear();  // the rows go while pressed
    host->router.mouse(mouse(MouseType::Move, Rig::X(), Rig::Y(2), kLeftBit));
    host->router.mouse(mouse(MouseType::Up, Rig::X(), Rig::Y(2), 0));
    for (int i = 0; i < 20; ++i) l->addItem("y" + std::to_string(i));
    host->router.mouse(mouse(MouseType::Move, Rig::X(), Rig::Y(1), 0));
    host->root.release(l);  // the whole list goes with a row hovered
    CHECK(host->root.children().empty());
}

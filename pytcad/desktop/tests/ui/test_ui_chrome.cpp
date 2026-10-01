// Portable tests of N3f's containers (NATIVE-DESKTOP-PLAN.md 27.8.7): the TabWidget, the Splitter and the ScrollArea -- sizes,
// signals, keys, the mouse, painting and UI Automation state. Part of tcad_ui_core_tests: no Win32. The fake text engine gives
// 6 DIPs a byte and a 15-DIP line, so a tab bar is ceil(15 + 2 x 5) = 25 DIPs high and a tab is its text plus 24 wide (48 at
// least).
#include "mini_test.hpp"

#include "fake_ui.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/message_box.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/splitter.hpp"
#include "ui/widgets/tab_widget.hpp"

using namespace tcad::ui;
using namespace tcad::ui::fake;
using tcad::desktop::theme::T;
namespace keys = tcad::ui::keys;

namespace {

// A page with a size hint of its own, and a flag for the focus it can take.
struct Page : Widget {
    SizeF hint{100, 60};
    Page() = default;
    explicit Page(SizeF h) : hint(h) {}
    SizeF sizeHint() const override { return hint; }
    SizeF minimumSizeHint() const override { return {10, 10}; }
};

struct FocusPage : Page {
    FocusPage() { setFocusPolicy(FocusPolicy::Strong); }
    bool keyEvent(const KeyEvent&) override { return false; }
};

// -- tabs ----------------------------------------------------------------------------------------------------------

struct TabRig {
    Host h;
    TabWidget* t;
    std::vector<std::string> log;
    TabRig() {
        t = h.root.addChild<TabWidget>();
        t->setGeometry({10, 10, 300, 200});
        t->on_current_changed = [this](int i) { log.push_back("tab " + std::to_string(i)); };
        t->addTab(std::make_unique<Page>(), "&Structure");
        t->addTab(std::make_unique<Page>(), "&Process");
        t->addTab(std::make_unique<Page>(), "&Catalog");
    }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
    void key(int vk, Mod m = Mod::None) { h.router.key(::tcad::ui::fake::key(vk, m)); }
    // a click in tab i's middle (window coords: the widget is at (10, 10))
    void clickTab(int i) {
        const RectF r = t->tabRect(i);
        const float x = 10 + r.x + r.width / 2, y = 10 + r.height / 2;
        h.router.mouse(mouse(MouseType::Down, x, y, kLeftBit));
        h.router.mouse(mouse(MouseType::Up, x, y, 0));
    }
};

}  // namespace

TEST(the_first_tab_added_is_current_and_only_its_page_is_visible) {
    TabRig r;
    CHECK(r.t->count() == 3 && r.t->currentIndex() == 0);
    CHECK_STR(r.take(), "tab 0");  // the first tab becomes current, and says so
    CHECK(r.t->widget(0)->isVisibleSelf() && !r.t->widget(1)->isVisibleSelf() && !r.t->widget(2)->isVisibleSelf());
    CHECK(r.t->currentWidget() == r.t->widget(0) && r.t->widget(7) == nullptr && r.t->indexOf(r.t->widget(2)) == 2);
    CHECK(r.t->accessibleRole() == Role::Tab);
}

TEST(tabs_are_laid_out_from_their_text_and_the_page_fills_the_rest_inside_a_frame) {
    TabRig r;
    CHECK(r.t->tabBarHeight() == 25);
    CHECK((r.t->tabRect(0) == RectF{0, 0, 78, 25}));  // "Structure": 9 x 6 + 24
    CHECK((r.t->tabRect(1) == RectF{78, 0, 66, 25}) && (r.t->tabRect(2) == RectF{144, 0, 66, 25}));
    CHECK((r.t->tabButton(1)->geometry() == RectI{78, 0, 66, 25}));
    CHECK((r.t->pageRect() == RectF{0, 24, 300, 176}));  // one DIP under the bar, so the selected tab joins the page's frame
    CHECK((r.t->widget(0)->geometry() == RectI{1, 25, 298, 174}));
    CHECK(r.t->tabButton(1)->accessibleName == "Process" && r.t->tabButton(1)->mnemonic() == U'P');
    r.t->setTabText(1, "&Run");
    CHECK((r.t->tabRect(1) == RectF{78, 0, 48, 25}));  // "Run" is 18 + 24 = 42: the minimum is 48
    CHECK(r.t->tabButton(1)->accessibleName == "Run" && r.t->tabText(1) == "&Run" && r.t->tabText(9).empty());
}

TEST(many_tabs_share_the_width_down_to_a_minimum) {
    Host h;
    auto* t = h.root.addChild<TabWidget>();
    t->setGeometry({0, 0, 300, 100});
    for (int i = 0; i < 6; ++i) t->addTab(std::make_unique<Page>(), "Structure");  // 6 x 78 = 468 > 300
    CHECK(t->tabRect(0).width == 50 && t->tabRect(5).x == 250);  // floor(78 x 300 / 468) each
    t->setGeometry({0, 0, 120, 100});
    CHECK(t->tabRect(0).width == 48);  // never below 48: the bar then overflows, which is stated
}

TEST(current_changed_reports_the_programs_changes_too_and_silent_or_refused_ones_do_not) {
    TabRig r;
    r.take();
    r.t->setCurrentIndex(2);
    CHECK_STR(r.take(), "tab 2");
    CHECK(r.t->widget(2)->isVisibleSelf() && !r.t->widget(0)->isVisibleSelf());
    r.t->setCurrentIndex(2);  // the same
    r.t->setCurrentIndex(9);  // out of range
    r.t->setCurrentIndex(-1);
    CHECK_STR(r.take(), "");
    r.t->setCurrentIndexSilent(0);
    CHECK(r.t->currentIndex() == 0 && r.log.empty());
    r.t->setTabEnabled(1, false);
    r.t->setCurrentIndex(1);  // a disabled tab cannot be selected
    r.t->setCurrentIndexSilent(1);
    CHECK(r.t->currentIndex() == 0 && r.log.empty() && !r.t->isTabEnabled(1) && r.t->isTabEnabled(0) && !r.t->isTabEnabled(7));
}

TEST(a_click_selects_a_tab_and_gives_the_widget_the_focus_and_a_disabled_tab_does_nothing) {
    TabRig r;
    r.take();
    r.clickTab(1);
    CHECK(r.t->currentIndex() == 1 && r.take() == "tab 1");
    CHECK(r.h.router.focusWidget() == r.t);
    r.t->setTabEnabled(2, false);
    r.clickTab(2);
    CHECK(r.t->currentIndex() == 1 && r.take().empty());
}

TEST(left_right_home_and_end_move_between_enabled_tabs_when_the_bar_has_the_focus) {
    TabRig r;
    r.t->setFocus(FocusReason::Tab);
    r.take();
    r.t->setTabEnabled(1, false);
    r.key(keys::Right);  // the disabled one is skipped
    CHECK(r.t->currentIndex() == 2);
    r.key(keys::Right);
    CHECK(r.t->currentIndex() == 2);  // the last: stays
    r.key(keys::Left);
    CHECK(r.t->currentIndex() == 0);
    r.key(keys::Left);
    CHECK(r.t->currentIndex() == 0);
    r.key(keys::End);
    CHECK(r.t->currentIndex() == 2);
    r.key(keys::Home);
    CHECK(r.t->currentIndex() == 0);
}

TEST(ctrl_tab_and_ctrl_page_keys_cycle_the_tabs_from_anywhere_inside_wrapping_and_skipping_disabled_ones) {
    TabRig r;
    auto* field = r.t->widget(0)->addChild<FocusPage>();
    field->setGeometry({0, 0, 50, 20});
    field->setFocus(FocusReason::Tab);
    r.take();
    r.key(keys::Tab, Mod::Ctrl);  // the focus is in the page: the chord still reaches the tab widget
    CHECK(r.t->currentIndex() == 1);
    r.t->setTabEnabled(2, false);
    r.key(keys::Tab, Mod::Ctrl);  // 2 is disabled: round to 0
    CHECK(r.t->currentIndex() == 0);
    r.key(keys::Tab, Mod::Ctrl | Mod::Shift);
    CHECK(r.t->currentIndex() == 1);
    r.key(keys::PageDown, Mod::Ctrl);
    CHECK(r.t->currentIndex() == 0);
    r.key(keys::PageUp, Mod::Ctrl);
    CHECK(r.t->currentIndex() == 1);
    CHECK(r.h.router.focusWidget() == r.t);  // the focus left the hidden page for the tab widget, so the chords keep working
    r.t->setCurrentIndex(0);
    field->setFocus(FocusReason::Tab);
    r.take();
    r.key(keys::Left);  // a plain arrow in the field is the field's, not the tabs'
    CHECK(r.t->currentIndex() == 0 && r.take().empty());
    r.t->setFocus(FocusReason::Tab);
    r.t->setCurrentIndex(1);
    r.key(keys::Left);  // on the tab widget itself it is
    CHECK(r.t->currentIndex() == 0);
    r.t->setTabEnabled(2, true);
    r.key(keys::Tab, Mod::Ctrl | Mod::Shift);  // Ctrl+Shift+Tab goes back, round to the last
    CHECK(r.t->currentIndex() == 2);
    r.key(keys::Tab, Mod::Ctrl);
    CHECK(r.t->currentIndex() == 0);
}

TEST(alt_and_a_tabs_letter_selects_it_and_focuses_the_widget) {
    TabRig r;
    r.take();
    r.key('C', Mod::Alt);
    CHECK(r.t->currentIndex() == 2 && r.take() == "tab 2");
    CHECK(r.h.router.focusWidget() == r.t);
    r.t->setTabEnabled(1, false);
    r.key('P', Mod::Alt);
    CHECK(r.t->currentIndex() == 2);  // disabled
}

TEST(inserting_before_the_current_tab_keeps_it_current_and_removing_the_current_one_makes_its_neighbour_current) {
    TabRig r;
    r.t->setCurrentIndex(1);
    r.take();
    Widget* extra_ptr = nullptr;
    auto extra = std::make_unique<Page>();
    extra_ptr = extra.get();
    CHECK(r.t->insertTab(0, std::move(extra), "&New") == 0);
    CHECK(r.t->currentIndex() == 2 && r.t->widget(0) == extra_ptr && r.t->tabButton(2)->index() == 2);  // the same page is current
    CHECK(r.t->widget(2)->isVisibleSelf() && !extra_ptr->isVisibleSelf() && r.log.empty());
    auto page = r.t->removeTab(2);  // the current one
    CHECK(page != nullptr && r.t->count() == 3 && r.t->currentIndex() == 2);  // its neighbour took the index
    CHECK_STR(r.take(), "tab 2");
    CHECK(r.t->widget(2)->isVisibleSelf());
    r.t->removeTab(0);  // before the current: it moves down with its page
    CHECK(r.t->currentIndex() == 1 && r.log.empty());
    CHECK(r.t->removeTab(9) == nullptr);
    r.t->removeTab(1);
    r.t->removeTab(0);
    CHECK(r.t->count() == 0 && r.t->currentIndex() == -1);
    CHECK_STR(r.take(), "tab 0, tab -1");
    CHECK(r.t->addTab(nullptr, "x") == -1);
}

TEST(a_tab_widget_asks_the_pages_for_its_size) {
    Host h;
    auto* t = h.root.addChild<TabWidget>();
    t->setGeometry({0, 0, 300, 200});
    t->addTab(std::make_unique<Page>(SizeF{200, 80}), "A");
    t->addTab(std::make_unique<Page>(SizeF{120, 150}), "B");
    CHECK(t->sizeHint() == (SizeF{std::max(202.0f, 48.0f + 48.0f), 150 + 25 + 1}));
    CHECK(t->minimumSizeHint() == (SizeF{12, 10 + 25 + 1}));
}

TEST(tabs_paint_the_selected_one_open_to_the_page_and_the_focus_frame_only_with_the_focus) {
    TabRig r;
    r.t->setTabEnabled(2, false);
    auto paint = [&](int i) {
        auto rec = std::make_unique<RecordingPainter>();
        r.t->tabButton(i)->paint(*rec);
        return rec;
    };
    auto sel = paint(0);
    bool base = false, accent = false, frame = false;
    for (const auto& op : sel->ops()) {
        if (op.kind == "fillRect" && op.rect == (RectF{0, 0, 78, 25})) base = op.color.b == token(T::Base).b;
        if (op.kind == "fillRect" && op.color.b == token(T::Accent).b && op.rect.height == 2) accent = true;
        if (op.kind == "strokeRect" && op.color.g == token(T::Focus).g) frame = true;
    }
    CHECK(base && accent && !frame);
    auto other = paint(1);
    bool alt = false;
    for (const auto& op : other->ops()) alt = alt || (op.kind == "fillRect" && op.rect == (RectF{0, 0, 66, 25}) && op.color.b == token(T::AlternateBase).b);
    CHECK(alt);
    bool faint = false;
    for (const auto& op : paint(2)->ops()) faint = faint || (op.kind == "text" && op.style.color.g == token(T::TextFaint).g);
    CHECK(faint);
    r.t->setFocus(FocusReason::Tab);
    bool focused = false;
    for (const auto& op : paint(0)->ops()) focused = focused || (op.kind == "strokeRect" && op.color.g == token(T::Focus).g);
    CHECK(focused);
}

TEST(tabs_are_tab_items_with_selection_state_and_the_focus_is_on_the_current_one) {
    TabRig r;
    TabButton* b1 = r.t->tabButton(1);
    CHECK(b1->accessibleRole() == Role::TabItem && b1->accessibleSelectionState() == 0 && r.t->tabButton(0)->accessibleSelectionState() == 1);
    CHECK(b1->accessibleSelectionContainer() == r.t && r.t->accessibleIsSelectionContainer());
    CHECK(r.t->accessibleSelection() == std::vector<Widget*>{r.t->tabButton(0)});
    b1->accessibleSelect();
    CHECK(r.t->currentIndex() == 1 && b1->accessibleSelectionState() == 1);
    r.t->setTabEnabled(2, false);
    CHECK(r.t->tabButton(2)->accessibleSelectionState() == -1);
    r.t->tabButton(2)->accessibleSelect();
    CHECK(r.t->currentIndex() == 1);
    r.t->setFocus(FocusReason::Tab);
    CHECK(r.t->accessibleFocusChild() == b1 && b1->accessibleFocused() && !r.t->accessibleFocused());
    r.t->setTabToolTip(1, "the process flow");
    CHECK_STR(b1->toolTip, "the process flow");
}

// -- the splitter ----------------------------------------------------------------------------------------------------

namespace {

struct SplitRig {
    Host h;
    Splitter* s;
    Page *a, *b, *c = nullptr;
    std::vector<std::string> log;
    explicit SplitRig(Orientation o = Orientation::Horizontal, bool three = false, float w = 300, float hgt = 100) {
        s = h.root.addChild<Splitter>(o);
        auto pa = std::make_unique<Page>(SizeF{100, 50});
        auto pb = std::make_unique<Page>(SizeF{200, 50});
        a = pa.get();
        b = pb.get();
        s->addWidget(std::move(pa));
        s->addWidget(std::move(pb));
        if (three) {
            auto pc = std::make_unique<Page>(SizeF{100, 50});
            c = pc.get();
            s->addWidget(std::move(pc));
        }
        s->setGeometry({10, 10, static_cast<int>(w), static_cast<int>(hgt)});
        s->on_splitter_moved = [this](int i) { log.push_back("moved " + std::to_string(i)); };
        s->on_drag_started = [this] { log.push_back("start"); };
        s->on_drag_finished = [this] { log.push_back("end"); };
    }
    std::string take() {
        std::string t;
        for (const auto& e : log) t += (t.empty() ? "" : ", ") + e;
        log.clear();
        return t;
    }
    // the handle's centre in window coordinates
    PointF handleAt(int i) const {
        const RectI g = s->handle(i)->geometry();
        const RectI o = s->geometry();
        return {static_cast<float>(o.x + g.x + g.width / 2.0), static_cast<float>(o.y + g.y + g.height / 2.0)};
    }
};

}  // namespace

TEST(a_splitter_lays_its_children_out_in_proportion_to_their_hints_with_a_handle_between) {
    SplitRig r;
    CHECK(r.s->count() == 2 && r.s->handle(0) != nullptr && r.s->handle(1) == nullptr && r.s->handleWidth() == 5);
    // 300 - 5 for the handle = 295, split 1 : 2 -> 98.33 and 196.67
    CHECK((r.s->sizes() == std::vector<int>{98, 197}));
    CHECK((r.a->geometry() == RectI{0, 0, 98, 100}) && (r.s->handle(0)->geometry() == RectI{98, 0, 5, 100}) && (r.b->geometry() == RectI{103, 0, 197, 100}));
    CHECK(r.s->indexOf(r.b) == 1 && r.s->widget(1) == r.b && r.s->widget(5) == nullptr);
    CHECK(r.s->handle(0)->accessibleRole() == Role::Splitter && r.s->handle(0)->cursor() == Cursor::SizeWE);
    CHECK(r.s->sizeHint() == (SizeF{305, 50}) && r.s->minimumSizeHint() == (SizeF{5, 10}));  // the minimums are 10, children collapsible: 0 + 0 + 5
}

TEST(a_vertical_splitter_works_along_the_height) {
    SplitRig r(Orientation::Vertical, false, 100, 300);
    CHECK(r.s->orientation() == Orientation::Vertical);
    CHECK(r.a->geometry().width == 100 && r.a->geometry().y == 0 && r.s->handle(0)->cursor() == Cursor::SizeNS);
    const auto sz = r.s->sizes();
    CHECK(sz[0] + sz[1] == 295);
    CHECK(r.s->handle(0)->geometry().width == 100 && r.s->handle(0)->geometry().height == 5);
}

TEST(set_sizes_sets_the_children_exactly_and_a_resize_scales_them_in_proportion) {
    SplitRig r;
    r.s->setSizes({100, 195});
    CHECK((r.s->sizes() == std::vector<int>{100, 195}));
    CHECK((r.a->geometry() == RectI{0, 0, 100, 100}) && (r.b->geometry() == RectI{105, 0, 195, 100}));
    r.s->setGeometry({10, 10, 400, 100});  // 395 now: 100 : 195 stays
    CHECK((r.s->sizes() == std::vector<int>{134, 261}));
    r.s->setSizes({50});  // fewer values than children: the rest are kept
    CHECK(r.s->sizes()[0] == 50);
    r.s->setSizes({10, 10, 10});  // more: ignored
    CHECK(r.s->sizes().size() == 2);
}

TEST(stretch_factors_decide_who_takes_a_resize) {
    SplitRig r;
    r.s->setSizes({100, 195});
    r.s->setStretchFactor(0, 0);
    r.s->setStretchFactor(1, 1);
    r.s->setGeometry({10, 10, 400, 100});
    CHECK((r.s->sizes() == std::vector<int>{100, 295}));  // the whole 100 went to the stretching child
    r.s->setGeometry({10, 10, 200, 100});
    CHECK((r.s->sizes() == std::vector<int>{100, 95}));
}

TEST(a_drag_moves_the_boundary_live_and_reports_start_moves_and_end) {
    SplitRig r;
    r.s->setSizes({100, 195});
    const PointF p = r.handleAt(0);
    r.h.router.mouse(mouse(MouseType::Down, p.x, p.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, p.x + 30, p.y, kLeftBit));
    CHECK((r.s->sizes() == std::vector<int>{130, 165}));
    r.h.router.mouse(mouse(MouseType::Move, p.x + 50, p.y, kLeftBit));
    CHECK((r.s->sizes() == std::vector<int>{150, 145}));
    r.h.router.mouse(mouse(MouseType::Up, p.x + 50, p.y, 0));
    CHECK_STR(r.take(), "start, moved 0, moved 0, end");
    r.h.router.mouse(mouse(MouseType::Move, p.x + 90, p.y, 0));  // released: nothing follows the pointer
    CHECK((r.s->sizes() == std::vector<int>{150, 145}));
}

TEST(a_child_stops_at_its_minimum_unless_it_is_collapsible_and_then_snaps_shut_below_half) {
    SplitRig r;
    r.a->setMinimumWidth(60);
    r.b->setMinimumWidth(120);
    r.s->setChildrenCollapsible(false);
    r.s->setSizes({100, 195});
    const PointF p = r.handleAt(0);
    r.h.router.mouse(mouse(MouseType::Down, p.x, p.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, p.x + 200, p.y, kLeftBit));
    CHECK((r.s->sizes() == std::vector<int>{175, 120}));  // the right child stops at 120
    r.h.router.mouse(mouse(MouseType::Move, p.x - 200, p.y, kLeftBit));
    CHECK((r.s->sizes() == std::vector<int>{60, 235}));   // the left child at 60
    r.h.router.mouse(mouse(MouseType::Up, p.x - 200, p.y, 0));
    CHECK(!r.s->isCollapsed(0));
    // collapsible (the default): below half its minimum (30) it snaps shut, between half and the minimum it stops at the minimum
    r.s->setCollapsible(0, true);
    const PointF q = r.handleAt(0);
    r.h.router.mouse(mouse(MouseType::Down, q.x, q.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, q.x - 20, q.y, kLeftBit));  // 40: between 30 and 60
    CHECK(r.s->sizes()[0] == 60 && !r.s->isCollapsed(0));
    r.h.router.mouse(mouse(MouseType::Move, q.x - 40, q.y, kLeftBit));  // 20: under 30
    CHECK((r.s->sizes() == std::vector<int>{0, 295}) && r.s->isCollapsed(0));
    r.h.router.mouse(mouse(MouseType::Up, q.x - 40, q.y, 0));
    CHECK(r.a->geometry().width == 0 && r.s->handle(0)->geometry().x == 0);
    // and it comes back when the handle is dragged out
    const PointF z = r.handleAt(0);
    r.h.router.mouse(mouse(MouseType::Down, z.x, z.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, z.x + 100, z.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, z.x + 100, z.y, 0));
    CHECK(r.s->sizes()[0] == 100 && !r.s->isCollapsed(0));
}

TEST(without_opaque_resize_a_line_follows_the_pointer_and_the_children_change_on_release) {
    SplitRig r;
    r.s->setOpaqueResize(false);
    CHECK(!r.s->opaqueResize());
    r.s->setSizes({100, 195});
    const PointF p = r.handleAt(0);
    r.h.router.mouse(mouse(MouseType::Down, p.x, p.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, p.x + 40, p.y, kLeftBit));
    CHECK((r.s->sizes() == std::vector<int>{100, 195}));  // the child (a 3D view) is not resized while dragging
    CHECK(r.s->ghostPosition() == 142.5f);                // 140 of the first child, the handle's middle
    CHECK_STR(r.take(), "start");
    r.h.router.mouse(mouse(MouseType::Up, p.x + 40, p.y, 0));
    CHECK((r.s->sizes() == std::vector<int>{140, 155}) && r.s->ghostPosition() == -1);
    CHECK_STR(r.take(), "moved 0, end");
    // a drag that goes nowhere changes nothing and still brackets
    const PointF q = r.handleAt(0);
    r.h.router.mouse(mouse(MouseType::Down, q.x, q.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, q.x, q.y, 0));
    CHECK_STR(r.take(), "start, end");
}

TEST(the_handle_is_a_tab_stop_and_the_arrow_keys_move_it_by_10_page_keys_by_50) {
    SplitRig r;
    r.s->setSizes({100, 195});
    r.s->handle(0)->setFocus(FocusReason::Tab);
    CHECK(r.s->handle(0)->hasFocus());
    r.h.router.key(key(keys::Right));
    CHECK((r.s->sizes() == std::vector<int>{110, 185}));
    r.h.router.key(key(keys::Left));
    r.h.router.key(key(keys::Left));
    CHECK((r.s->sizes() == std::vector<int>{90, 205}));
    r.h.router.key(key(keys::PageDown));
    CHECK((r.s->sizes() == std::vector<int>{140, 155}));
    r.h.router.key(key(keys::PageUp));
    CHECK((r.s->sizes() == std::vector<int>{90, 205}));
    r.h.router.key(key(keys::Down));  // an arrow across the axis is not its
    CHECK((r.s->sizes() == std::vector<int>{90, 205}));
    r.h.router.key(key(keys::End));
    CHECK((r.s->sizes() == std::vector<int>{295, 0}) && r.s->isCollapsed(1));
    r.h.router.key(key(keys::Home));
    CHECK((r.s->sizes() == std::vector<int>{0, 295}));
    CHECK(r.take().find("moved 0") != std::string::npos);
    SplitRig v(Orientation::Vertical, false, 100, 300);
    v.s->setSizes({100, 195});
    v.s->handle(0)->setFocus(FocusReason::Tab);
    v.h.router.key(key(keys::Down));
    CHECK(v.s->sizes()[0] == 110);
    v.h.router.key(key(keys::Right));  // and across the vertical axis, not its
    CHECK(v.s->sizes()[0] == 110);
}

TEST(with_three_children_each_handle_moves_only_its_two_neighbours) {
    SplitRig r(Orientation::Horizontal, true, 310, 100);
    r.s->setSizes({100, 100, 100});
    CHECK(r.s->handle(1) != nullptr && r.s->handle(2) == nullptr);
    const PointF p = r.handleAt(1);
    r.h.router.mouse(mouse(MouseType::Down, p.x, p.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, p.x - 20, p.y, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, p.x - 20, p.y, 0));
    CHECK((r.s->sizes() == std::vector<int>{100, 80, 120}));
    CHECK_STR(r.take(), "start, moved 1, end");
    CHECK((r.c->geometry() == RectI{100 + 5 + 80 + 5, 0, 120, 100}));
}

TEST(a_handle_reports_a_range_to_ui_automation_and_a_set_moves_it_or_is_refused) {
    SplitRig r;
    r.s->setSizes({100, 195});
    r.b->setMinimumWidth(50);
    r.s->setChildrenCollapsible(false);
    r.a->setMinimumWidth(40);
    const AccessibleRange a = r.s->handle(0)->accessibleRange();
    CHECK(a.valid && a.value == 100 && a.minimum == 40 && a.maximum == 100 + 195 - 50 && a.small_step == 10 && a.large_step == 50);
    CHECK(r.s->handle(0)->accessibleSetRangeValue(150));
    CHECK((r.s->sizes() == std::vector<int>{150, 145}));
    CHECK(!r.s->handle(0)->accessibleSetRangeValue(20) && !r.s->handle(0)->accessibleSetRangeValue(500));
    CHECK((r.s->sizes() == std::vector<int>{150, 145}));  // refused, never clamped
}

// -- the scroll area -------------------------------------------------------------------------------------------------

namespace {

struct AreaRig {
    Host h;
    ScrollArea* a;
    Page* content;
    explicit AreaRig(SizeF content_hint = {150, 300}, bool frame = true, bool resizable = false) {
        a = h.root.addChild<ScrollArea>();
        a->setFrame(frame);
        auto p = std::make_unique<Page>(content_hint);
        content = p.get();
        a->setWidget(std::move(p));
        a->setWidgetResizable(resizable);
        a->setGeometry({10, 10, 200, 100});
    }
};

}  // namespace

TEST(a_scroll_area_shows_one_content_widget_with_a_bar_when_it_does_not_fit) {
    AreaRig r;
    CHECK(r.a->widget() == r.content);
    CHECK(r.a->verticalScrollBar()->isVisibleSelf() && !r.a->horizontalScrollBar()->isVisibleSelf());
    CHECK((r.a->viewportRect() == RectF{1, 1, 184, 98}));  // the frame and the bar take 2 and 14 of the width
    CHECK((r.content->geometry() == RectI{0, 0, 150, 300}));
    CHECK(r.a->verticalScrollBar()->maximum() == 202 && r.a->verticalScrollBar()->pageStep() == 98 && r.a->verticalScrollBar()->singleStep() == 20);
    r.a->verticalScrollBar()->setValue(60);
    CHECK(r.content->geometry().y == -60);
    CHECK(r.a->scrollY() == 60 && r.a->scrollX() == 0);
}

TEST(the_wheel_scrolls_three_steps_of_20_a_notch_and_a_content_that_fits_ignores_it) {
    AreaRig r;
    r.h.router.mouse(wheel(50, 50, -1));
    CHECK(r.a->scrollY() == 60);
    r.h.router.mouse(wheel(50, 50, 2));
    CHECK(r.a->scrollY() == 0);
    AreaRig fits({100, 50});
    CHECK(!fits.a->verticalScrollBar()->isVisibleSelf());
    fits.h.router.mouse(wheel(50, 50, -1));
    CHECK(fits.a->scrollY() == 0);
}

TEST(a_resizable_content_is_as_wide_as_the_viewport_and_only_scrolls_down) {
    AreaRig r({150, 300}, true, true);
    CHECK(r.a->widgetResizable());
    CHECK((r.content->geometry() == RectI{0, 0, 184, 300}));  // as wide as the viewport (less the bar)
    CHECK(!r.a->horizontalScrollBar()->isVisibleSelf());
    r.a->setGeometry({10, 10, 300, 100});
    CHECK(r.content->geometry().width == 284);
    AreaRig tall({150, 60}, true, true);  // shorter than the viewport: it fills it, no bar
    CHECK((tall.content->geometry() == RectI{0, 0, 198, 98}) && !tall.a->verticalScrollBar()->isVisibleSelf());
}

TEST(a_wide_content_that_is_not_resizable_gets_a_sideways_bar_too) {
    AreaRig r({400, 300});
    CHECK(r.a->verticalScrollBar()->isVisibleSelf() && r.a->horizontalScrollBar()->isVisibleSelf());
    CHECK((r.a->viewportRect() == RectF{1, 1, 184, 84}));
    CHECK(r.a->horizontalScrollBar()->maximum() == 400 - 184 && r.a->verticalScrollBar()->maximum() == 300 - 84);
    r.a->horizontalScrollBar()->setValue(50);
    CHECK(r.content->geometry().x == -50);
}

TEST(a_scroll_area_without_a_frame_uses_the_whole_widget) {
    AreaRig r({150, 300}, false);
    CHECK((r.a->viewportRect() == RectF{0, 0, 186, 100}));
    r.a->setFrame(true);
    CHECK((r.a->viewportRect() == RectF{1, 1, 184, 98}));
}

TEST(ensure_visible_scrolls_the_least_that_shows_a_point_or_a_widget) {
    AreaRig r;
    r.a->ensureVisible(10, 250);
    CHECK(r.a->scrollY() == 250 - 98);
    r.a->ensureVisible(10, 20);
    CHECK(r.a->scrollY() == 20);
    r.a->ensureVisible(10, 60);  // already inside [20, 118]
    CHECK(r.a->scrollY() == 20);
    r.a->ensureVisible(10, 250, 10);
    CHECK(r.a->scrollY() == 250 + 10 - 98);
    auto* child = r.content->addChild<Page>(SizeF{50, 20});
    child->setGeometry({10, 100, 50, 20});
    r.a->verticalScrollBar()->setValue(0);
    r.a->ensureWidgetVisible(child);
    CHECK(r.a->scrollY() == 120 - 98);  // its bottom edge at the viewport's
    r.a->ensureWidgetVisible(child, 5);
    CHECK(r.a->scrollY() == 120 + 5 - 98);
    r.a->ensureWidgetVisible(nullptr);
}

TEST(moving_the_focus_into_the_content_scrolls_the_field_into_view) {
    AreaRig r;
    auto* near_field = r.content->addChild<FocusPage>();
    near_field->setGeometry({10, 10, 50, 20});
    auto* far_field = r.content->addChild<FocusPage>();
    far_field->setGeometry({10, 270, 50, 20});
    auto* outside = r.h.root.addChild<FocusPage>();
    outside->setGeometry({300, 10, 50, 20});
    near_field->setFocus(FocusReason::Tab);
    CHECK(r.a->scrollY() == 0);
    far_field->setFocus(FocusReason::Tab);  // Tab to a field below the fold
    CHECK(r.a->scrollY() == 290 + 6 - 98);  // its bottom plus the 6-DIP margin
    near_field->setFocus(FocusReason::Tab);
    CHECK(r.a->scrollY() == 4);  // 10 - 6
    r.a->verticalScrollBar()->setValue(100);
    outside->setFocus(FocusReason::Tab);  // a widget outside the content: the area does not move
    CHECK(r.a->scrollY() == 100);
}

TEST(replacing_the_content_releases_the_old_one_and_resets_the_scroll) {
    AreaRig r;
    r.a->verticalScrollBar()->setValue(100);
    auto next = std::make_unique<Page>(SizeF{100, 500});
    Widget* n = next.get();
    CHECK(r.a->setWidget(std::move(next)) == n && r.a->widget() == n);
    CHECK(r.a->scrollY() == 0 && r.a->verticalScrollBar()->maximum() == 500 - 98);
    CHECK(r.a->setWidget(nullptr) == nullptr && r.a->widget() == nullptr && !r.a->verticalScrollBar()->isVisibleSelf());
    CHECK(r.a->sizeHint() == (SizeF{256, 192}));
}

TEST(the_size_hint_follows_the_content_but_is_capped) {
    AreaRig r({150, 300});
    CHECK(r.a->sizeHint() == (SizeF{152, 302}));
    r.a->setWidget(std::make_unique<Page>(SizeF{900, 2000}));
    CHECK(r.a->sizeHint() == (SizeF{362, 402}));  // Qt does not ask for the screen
    CHECK(r.a->minimumSizeHint() == (SizeF{48, 48}));
}

TEST(a_scroll_area_reports_its_position_to_ui_automation_and_scrolls_on_request) {
    AreaRig r;
    AccessibleScroll s = r.a->accessibleScroll();
    CHECK(s.valid && s.vertical && !s.horizontal && s.v_percent == 0);
    CHECK(s.v_view > 32 && s.v_view < 33);  // 98 of 300
    r.a->accessibleScrollBy(0, 2);  // a page
    CHECK(r.a->scrollY() == 98);
    r.a->accessibleScrollBy(0, -1);  // a small step: 20
    CHECK(r.a->scrollY() == 78);
    r.a->accessibleSetScrollPercent(-1, 100);
    CHECK(r.a->scrollY() == 202 && r.a->accessibleScroll().v_percent == 100);
    r.a->accessibleSetScrollPercent(50, -1);  // sideways: not scrollable, ignored
    CHECK(r.a->scrollX() == 0);
    CHECK(r.a->accessibleRole() == Role::Pane);
}

TEST(a_scroll_area_destroyed_with_a_focus_observer_is_safe) {
    auto host = std::make_unique<Host>();
    auto* a = host->root.addChild<ScrollArea>();
    a->setWidget(std::make_unique<Page>(SizeF{100, 300}));
    a->setGeometry({0, 0, 200, 100});
    auto* other = host->root.addChild<FocusPage>();
    other->setGeometry({300, 0, 20, 20});
    host->root.release(a);  // the observer goes with it
    other->setFocus(FocusReason::Tab);
    CHECK(host->router.focusWidget() == other);
}

// -- the message box's content ------------------------------------------------------------------------------------------

namespace {

struct BoxRig {
    Host h;
    MessageBoxContent* c;
    std::vector<StandardButton> done;
    explicit BoxRig(std::vector<StandardButton> buttons, StandardButton def = StandardButton::None, StandardButton esc = StandardButton::None) {
        MessageBoxSpec spec;
        spec.title = "Title";
        spec.text = "Body text";
        spec.buttons = std::move(buttons);
        spec.default_button = def;
        spec.escape_button = esc;
        c = h.root.addChild<MessageBoxContent>(spec);
        c->setGeometry({0, 0, 400, 150});
        c->on_finished = [this](StandardButton b) { done.push_back(b); };
    }
    bool key(int vk, Mod m = Mod::None) { return c->keyEvent(::tcad::ui::fake::key(vk, m)); }
};

}  // namespace

TEST(the_escape_button_is_the_named_one_else_cancel_else_the_only_one_else_no_else_close) {
    using S = StandardButton;
    CHECK(BoxRig({S::Ok}).c->escapeButtonId() == S::Ok);
    CHECK(BoxRig({S::Save, S::Discard, S::Cancel}).c->escapeButtonId() == S::Cancel);
    CHECK(BoxRig({S::Yes, S::No}).c->escapeButtonId() == S::No);
    CHECK(BoxRig({S::Yes, S::No, S::Cancel}).c->escapeButtonId() == S::Cancel);
    CHECK(BoxRig({S::Save, S::Close}).c->escapeButtonId() == S::Close);
    CHECK(BoxRig({S::Save, S::Discard}).c->escapeButtonId() == S::None);
    CHECK(BoxRig({S::Save, S::Discard, S::Cancel}, S::None, S::Discard).c->escapeButtonId() == S::Discard);
    CHECK(BoxRig({S::Save, S::Discard}, S::None, S::Cancel).c->escapeButtonId() == S::None);  // a named button that is not there
}

TEST(the_default_button_is_the_named_one_else_the_first_and_enter_and_escape_press_the_right_ones) {
    using S = StandardButton;
    BoxRig a({S::Save, S::Discard, S::Cancel});
    CHECK(a.c->defaultButtonId() == S::Save && a.c->defaultButton()->isDefault() && !a.c->button(S::Discard)->isDefault());
    BoxRig b({S::Save, S::Discard, S::Cancel}, S::Discard);
    CHECK(b.c->defaultButtonId() == S::Discard && b.c->button(S::Discard)->isDefault() && !b.c->button(S::Save)->isDefault());
    BoxRig c({S::Save, S::Discard}, S::Cancel);  // not there: the first
    CHECK(c.c->defaultButtonId() == S::Save);
    CHECK(b.key(keys::Return) && b.done == std::vector<S>{S::Discard});
    BoxRig d({S::Save, S::Discard, S::Cancel});
    CHECK(d.key(keys::Escape) && d.done == std::vector<S>{S::Cancel});
    BoxRig e({S::Save, S::Discard});  // no escape button: Escape is not used
    CHECK(!e.key(keys::Escape) && e.done.empty());
}

TEST(a_box_answers_once_whatever_comes_after) {
    using S = StandardButton;
    BoxRig r({S::Save, S::Discard, S::Cancel});
    r.c->press(S::Discard);
    r.c->press(S::Save);
    r.key(keys::Return);
    r.key(keys::Escape);
    CHECK(r.done == std::vector<S>{S::Discard} && r.c->finished());
}

TEST(left_and_right_move_the_focus_between_the_buttons_and_wrap) {
    using S = StandardButton;
    BoxRig r({S::Save, S::Discard, S::Cancel});
    CHECK(r.key(keys::Right));  // none focused: the first
    CHECK(r.c->button(S::Save)->hasFocus());
    r.key(keys::Right);
    CHECK(r.c->button(S::Discard)->hasFocus());
    r.key(keys::Right);
    r.key(keys::Right);  // wraps to the first
    CHECK(r.c->button(S::Save)->hasFocus());
    r.key(keys::Left);  // back wraps to the last
    CHECK(r.c->button(S::Cancel)->hasFocus());
    r.key(keys::Left);
    CHECK(r.c->button(S::Discard)->hasFocus());
}

TEST(the_boxes_copy_text_is_in_windows_message_box_form) {
    BoxRig r({StandardButton::Yes, StandardButton::No});
    const std::string line = "---------------------------\n";
    CHECK(r.c->copyText() == line + "Title\n" + line + "Body text\n" + line + "Yes   No\n" + line);
}

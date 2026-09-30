// Unit tests of the portable UI core (N2b, NATIVE-DESKTOP-PLAN.md 27.7): the distribution rule, Box/Form/Grid/Stack
// layouts, the widget tree (ownership, visibility, enabled, invalidation) and painting into the RecordingPainter.
// No Win32, no GPU: built by CMake (tcad_ui_core_tests) and runnable with any C++23 compiler. Every expected number
// is worked out by hand from the rule in layout.hpp -- they are the rule's specification, not a recording.
#include "mini_test.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/core/widget.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace tcad::ui;

namespace {

class FakeText final : public TextEngine {
public:
    // 0.5 em per UTF-8 byte, 1.25 em line height: simple, exact
    SizeF measure(std::string_view s, const TextStyle& st) override {
        return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size};
    }
};

class FakeHost final : public UiHost {
public:
    double s = 1.0;
    std::vector<RectI> invalidated;
    int layouts = 0;
    FakeText text;
    void invalidate(const RectI& r) override { invalidated.push_back(r); }
    void scheduleLayout() override { ++layouts; }
    TextEngine& textEngine() override { return text; }
    double scale() const override { return s; }
};

// A widget with a given hint (DIPs), minimum hint and policy, painting one filled rectangle.
class Box final : public Widget {
public:
    Box(SizeF hint, Policy p = {}, SizeF min_hint = {0, 0}) : hint_(hint), min_(min_hint) { setSizePolicy(p); }
    SizeF sizeHint() const override { return hint_; }
    SizeF minimumSizeHint() const override { return min_; }
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({0, 0, s.width, s.height}, Color::rgb(0x336699));
    }

private:
    SizeF hint_, min_;
};

constexpr Policy kFixed{SizePolicy::Fixed, SizePolicy::Fixed};
constexpr Policy kExpH{SizePolicy::Expanding, SizePolicy::Fixed};
constexpr Policy kPrefFixedV{SizePolicy::Preferred, SizePolicy::Fixed};

struct Root {
    FakeHost host;
    Widget w;
    Root(double scale, int wpx, int hpx) {
        host.s = scale;
        w.setHost(&host);
        w.setGeometry({0, 0, wpx, hpx});
    }
    void relayout() { w.setGeometry(w.geometry()); }
};

std::vector<int> sizes(std::initializer_list<AxisItem> items, int space) { return distribute(items, space); }

}  // namespace

// -- distribute(): the rule, case by case ----------------------------------------------------------------------

TEST(distribute_growable_items_share_extra_equally) {
    CHECK((sizes({{0, 10}, {0, 10}, {0, 10}}, 60) == std::vector<int>{20, 20, 20}));
}

TEST(distribute_stretch_factors_split_in_proportion) {
    CHECK((sizes({{0, 0, kMaxPx, 1}, {0, 0, kMaxPx, 2}}, 90) == std::vector<int>{30, 60}));
}

TEST(distribute_expanding_takes_the_extra_from_preferred) {
    CHECK((sizes({{0, 10}, {0, 10, kMaxPx, 0, true}}, 50) == std::vector<int>{10, 40}));
}

TEST(distribute_stretch_beats_expanding) {
    CHECK((sizes({{0, 10, kMaxPx, 0, true}, {0, 10, kMaxPx, 1}}, 50) == std::vector<int>{10, 40}));
}

TEST(distribute_a_capped_item_passes_its_share_on) {
    CHECK((sizes({{0, 0, 20, 1}, {0, 0, kMaxPx, 1}}, 100) == std::vector<int>{20, 80}));
    // capped stretch items, then the Expanding stage takes the rest
    CHECK((sizes({{0, 0, 20, 1}, {0, 0, kMaxPx, 0, true}}, 100) == std::vector<int>{20, 80}));
}

TEST(distribute_shrinks_in_proportion_to_room_above_minimum) {
    // deficit 30 from room 40 and 20: 20 and 10
    CHECK((sizes({{10, 50}, {30, 50}}, 70) == std::vector<int>{30, 40}));
}

TEST(distribute_below_the_minimums_gives_the_minimums) {
    CHECK((sizes({{10, 50}, {30, 50}}, 30) == std::vector<int>{10, 30}));
}

TEST(distribute_rounding_pixels_go_to_the_first_items) {
    CHECK((sizes({{0, 0}, {0, 0}, {0, 0}}, 10) == std::vector<int>{4, 3, 3}));
}

TEST(distribute_every_rounding_pixel_is_handed_out) {
    CHECK((sizes({{0, 0}, {0, 0}, {0, 0}, {0, 0}}, 10) == std::vector<int>{3, 3, 2, 2}));
}

TEST(fixed_and_minimum_items_never_shrink_below_their_hint) {
    Root r(1.0, 18 + 6 + 6 + 70, 40);  // content 76 - 2 spacings: 70 for hints of 50 + 50 + 30
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    auto* f = box->add<Box>(SizeF{50, 10}, kFixed);
    auto* m = box->add<Box>(SizeF{30, 10}, Policy{SizePolicy::Minimum, SizePolicy::Fixed});
    auto* p = box->add<Box>(SizeF{50, 10}, kPrefFixedV, SizeF{10, 10});
    r.relayout();
    // 70 px for hints 50 + 30 + 50, below even the minimums (50 + 30 + 10 = 90): every item at its minimum -- the
    // Fixed and Minimum ones at their hints, the Preferred one at its minimum hint -- and the row overflows
    CHECK(f->geometry().width == 50 && m->geometry().width == 30);
    CHECK(p->geometry().width == 10);
}

TEST(distribute_fixed_items_leave_space_unused) {
    CHECK((sizes({{10, 10, 10}, {10, 10, 10}}, 50) == std::vector<int>{10, 10}));
}

// -- BoxLayout ---------------------------------------------------------------------------------------------------

TEST(hbox_places_items_with_margins_spacing_and_a_stretch) {
    Root r(1.0, 200, 100);
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    auto* a = box->add<Box>(SizeF{50, 20});
    auto* b = box->add<Box>(SizeF{50, 20});
    auto* c = box->add<Box>(SizeF{50, 20}, kPrefFixedV);
    box->addStretch();
    r.relayout();
    // content 9..191 (182 wide, 82 high); 2 spacings of 6; the spacer takes the 20 extra
    CHECK((a->geometry() == RectI{9, 9, 50, 82}));   // Preferred vertically: fills the height
    CHECK((b->geometry() == RectI{65, 9, 50, 82}));
    CHECK((c->geometry() == RectI{121, 40, 50, 20}));  // Fixed vertically: its hint, centred (9 + (82-20)/2)
    CHECK((box->sizeHintPx() == SizeI{9 + 50 * 3 + 12 + 9, 9 + 20 + 9}));
}

TEST(hbox_at_150_percent_rounds_margins_to_nearest_and_hints_up) {
    Root r(1.5, 300, 100);
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    auto* a = box->add<Box>(SizeF{33, 20}, kFixed);  // 49.5 px -> 50
    box->addStretch();
    r.relayout();
    // margin 9 DIP = 13.5 px -> 14 (round half away from zero)
    CHECK((a->geometry() == RectI{14, 14 + (100 - 28 - 30) / 2, 50, 30}));
}

TEST(hidden_widgets_take_no_space_and_no_spacing) {
    Root r(1.0, 200, 60);
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    auto* a = box->add<Box>(SizeF{50, 20}, kFixed);
    auto* b = box->add<Box>(SizeF{50, 20}, kFixed);
    auto* c = box->add<Box>(SizeF{50, 20}, kFixed);
    box->addStretch();
    b->hide();
    r.relayout();
    CHECK(a->geometry().x == 9);
    CHECK(c->geometry().x == 9 + 50 + 6);
    CHECK((box->sizeHintPx() == SizeI{9 + 100 + 6 + 9, 38}));
}

TEST(hbox_stretch_factors_on_widgets) {
    Root r(1.0, 18 + 6 + 90, 40);  // content 90 after one spacing
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    Widget* a = r.w.addChild<Box>(SizeF{0, 10});
    Widget* b = r.w.addChild<Box>(SizeF{0, 10});
    box->addWidget(a, 1);
    box->addWidget(b, 2);
    r.relayout();
    CHECK(a->geometry().width == 30 && b->geometry().width == 60);
    CHECK(b->geometry().x == 9 + 30 + 6);
}

TEST(nested_boxes_have_no_margins_and_share_the_host) {
    Root r(1.0, 120, 200);
    auto* col = r.w.setLayout<BoxLayout>(Orientation::Vertical);
    auto* top = col->add<Box>(SizeF{40, 30}, kFixed);
    auto* row = col->addBox(Orientation::Horizontal);
    auto* x = row->add<Box>(SizeF{20, 20}, kFixed);
    auto* y = row->add<Box>(SizeF{20, 20}, kFixed);
    col->addStretch(1);
    r.relayout();
    CHECK((row->contentsMargins() == Margins{}));
    CHECK((top->geometry() == RectI{9, 9, 40, 30}));
    CHECK((x->geometry() == RectI{9, 9 + 30 + 6, 20, 20}));
    CHECK((y->geometry() == RectI{9 + 20 + 6, 45, 20, 20}));
    CHECK(x->parent() == &r.w);  // items of a nested layout are children of the host
}

TEST(explicit_minimum_size_overrides_a_smaller_hint) {
    Root r(1.0, 100, 300);
    auto* col = r.w.setLayout<BoxLayout>(Orientation::Vertical);
    auto* a = col->add<Box>(SizeF{50, 20});
    a->setMinimumHeight(160);  // telemetry_panel's plot_->setMinimumHeight(160)
    CHECK(col->minimumSizePx().height == 9 + 160 + 9);
    r.w.setGeometry({0, 0, 100, 100});  // too small: the minimum still holds (clipped)
    CHECK(a->geometry().height == 160);
}

// -- FormLayout --------------------------------------------------------------------------------------------------

TEST(form_aligns_labels_in_a_column_and_grows_fields) {
    Root r(1.0, 300, 200);
    auto* form = r.w.setLayout<FormLayout>();
    Widget* l1 = r.w.addChild<Box>(SizeF{40, 16}, kFixed);
    Widget* f1 = r.w.addChild<Box>(SizeF{100, 20}, kPrefFixedV);
    Widget* l2 = r.w.addChild<Box>(SizeF{60, 16}, kFixed);
    Widget* f2 = r.w.addChild<Box>(SizeF{80, 20}, kFixed);  // a Fixed field keeps its width
    form->addRow(l1, f1);
    form->addRow(l2, f2);
    r.relayout();
    const int fx = 9 + 60 + 6;
    CHECK((l1->geometry() == RectI{9, 9 + 2, 40, 16}));    // centred in the 20-high row
    CHECK((f1->geometry() == RectI{fx, 9, 300 - 9 - fx, 20}));
    CHECK((l2->geometry() == RectI{9, 9 + 20 + 6 + 2, 60, 16}));
    CHECK((f2->geometry() == RectI{fx, 35, 80, 20}));
    CHECK((form->sizeHintPx() == SizeI{9 + 60 + 6 + 100 + 9, 9 + 20 + 6 + 20 + 9}));
}

TEST(form_skips_hidden_rows_and_spans_a_single_widget) {
    Root r(1.0, 200, 200);
    auto* form = r.w.setLayout<FormLayout>();
    Widget* l1 = r.w.addChild<Box>(SizeF{30, 16}, kFixed);
    Widget* f1 = r.w.addChild<Box>(SizeF{50, 20}, kPrefFixedV);
    Widget* span = r.w.addChild<Box>(SizeF{50, 30}, kPrefFixedV);
    Widget* l3 = r.w.addChild<Box>(SizeF{30, 16}, kFixed);
    Widget* f3 = r.w.addChild<Box>(SizeF{50, 20}, kPrefFixedV);
    form->addRow(l1, f1);
    form->addRow(span);
    form->addRow(l3, f3);
    l1->hide();
    f1->hide();
    r.relayout();
    CHECK((span->geometry() == RectI{9, 9, 182, 30}));
    CHECK(f3->geometry().y == 9 + 30 + 6);
    form->insertRow(0, nullptr, r.w.addChild<Box>(SizeF{10, 10}, kFixed));
    CHECK(form->rowCount() == 4);
}

// -- GridLayout --------------------------------------------------------------------------------------------------

TEST(grid_sizes_columns_by_their_widest_cell_and_spans_cells) {
    // view3d_panel's crop grid: a label, two spin boxes per row, a reset button across all three columns
    Root r(1.0, 18 + 12 + 30 + 60 + 60, 200);
    auto* grid = r.w.setLayout<GridLayout>();
    std::vector<Widget*> cells;
    for (int row = 0; row < 2; ++row) {
        grid->addWidget(cells.emplace_back(r.w.addChild<Box>(SizeF{30, 20}, kFixed)), row, 0);
        grid->addWidget(cells.emplace_back(r.w.addChild<Box>(SizeF{60, 20}, kPrefFixedV)), row, 1);
        grid->addWidget(cells.emplace_back(r.w.addChild<Box>(SizeF{60, 20}, kPrefFixedV)), row, 2);
    }
    Widget* reset = r.w.addChild<Box>(SizeF{80, 24}, kPrefFixedV);
    grid->addWidget(reset, 2, 0, 1, 3);
    r.relayout();
    CHECK((cells[0]->geometry() == RectI{9, 9, 30, 20}));
    CHECK((cells[1]->geometry() == RectI{9 + 30 + 6, 9, 60, 20}));
    CHECK((cells[5]->geometry() == RectI{9 + 30 + 6 + 60 + 6, 9 + 20 + 6, 60, 20}));
    CHECK((reset->geometry() == RectI{9, 9 + 2 * 26, 30 + 60 + 60 + 12, 24}));
    // wider: the growable columns (1, 2) share the extra; the Fixed column 0 keeps 30
    r.w.setGeometry({0, 0, 18 + 12 + 30 + 60 + 60 + 40, 200});
    CHECK(cells[0]->geometry().width == 30 && cells[1]->geometry().width == 80 && cells[2]->geometry().width == 80);
}

TEST(grid_a_spanning_cell_widens_the_columns_it_needs) {
    Root r(1.0, 400, 100);
    auto* grid = r.w.setLayout<GridLayout>();
    grid->addWidget(r.w.addChild<Box>(SizeF{10, 10}, kFixed), 0, 0);
    grid->addWidget(r.w.addChild<Box>(SizeF{10, 10}, kFixed), 0, 1);
    grid->addWidget(r.w.addChild<Box>(SizeF{101, 10}, kFixed), 1, 0, 1, 2);
    // columns 10 + 6 + 10 = 26 < 101: 75 more, 38 and 37
    CHECK(grid->sizeHintPx().width == 9 + 48 + 6 + 47 + 9);
}

// -- StackLayout -------------------------------------------------------------------------------------------------

TEST(stack_shows_one_page_and_sizes_for_all) {
    Root r(1.0, 200, 150);
    auto* stack = r.w.setLayout<StackLayout>();
    auto* p0 = r.w.addChild<Box>(SizeF{50, 40});
    auto* p1 = r.w.addChild<Box>(SizeF{120, 30});
    stack->addWidget(p0);
    stack->addWidget(p1);
    r.relayout();
    CHECK(p0->isVisible() && !p1->isVisible());
    CHECK((stack->sizeHintPx() == SizeI{9 + 120 + 9, 9 + 40 + 9}));
    stack->setCurrentIndex(1);
    CHECK(!p0->isVisible() && p1->isVisible() && stack->currentWidget() == p1);
    CHECK((stack->sizeHintPx() == SizeI{138, 58}));  // unchanged by switching
    CHECK((p1->geometry() == RectI{9, 9, 182, 132}));
}

// -- the tree ----------------------------------------------------------------------------------------------------

TEST(tree_owns_children_and_finds_them_by_name) {
    Widget root;
    auto* a = root.addChild<Box>(SizeF{1, 1});
    a->name = "a";
    auto* b = a->addChild<Box>(SizeF{1, 1});
    b->name = "b";
    CHECK(root.findChild("b") == b && b->root() == &root && b->parent() == a);
    auto owned = root.release(a);
    CHECK(owned.get() == a && a->parent() == nullptr && root.children().empty());
    CHECK(root.release(b) == nullptr);  // not a direct child
}

TEST(visibility_and_enabled_are_effective_through_ancestors) {
    Widget root;
    auto* a = root.addChild<Box>(SizeF{1, 1});
    auto* b = a->addChild<Box>(SizeF{1, 1});
    a->hide();
    CHECK(b->isVisibleSelf() && !b->isVisible());
    a->show();
    CHECK(b->isVisible());
    a->setEnabled(false);
    CHECK(!b->isEnabled());
}

TEST(updates_and_geometry_changes_reach_the_host) {
    Root r(1.0, 200, 100);
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    auto* a = box->add<Box>(SizeF{50, 20}, kFixed);
    r.relayout();
    r.host.invalidated.clear();
    const int layouts = r.host.layouts;
    a->update();
    CHECK(r.host.invalidated.size() == 1 && (r.host.invalidated[0] == a->windowRect()));
    a->setMinimumSize({60, 20});
    CHECK(r.host.layouts > layouts);
    r.host.invalidated.clear();
    a->hide();
    CHECK(!r.host.invalidated.empty());  // the area it leaves is repainted
    r.host.invalidated.clear();
    a->update();                         // hidden: nothing to repaint
    CHECK(r.host.invalidated.empty());
}

TEST(paint_tree_translates_clips_and_skips_hidden_widgets) {
    Root r(2.0, 200, 100);
    auto* box = r.w.setLayout<BoxLayout>(Orientation::Horizontal);
    box->setContentsMargins(0, 0, 0, 0);
    auto* a = box->add<Box>(SizeF{20, 10}, kFixed);
    auto* b = box->add<Box>(SizeF{20, 10}, kFixed);
    box->addStretch();
    r.relayout();
    RecordingPainter p(2.0, {100, 50});
    r.w.paintTree(p);
    CHECK(p.ops().size() == 2);
    // b at px (40 + 12 spacing, centred (100 - 20) / 2 = 40): DIPs (26, 20); clip = its own rect
    CHECK(p.ops()[1].str() == "fillRect 26,20 20x10 #336699ff");
    CHECK((p.ops()[1].clip == RectF{26, 20, 20, 10}));
    CHECK(p.depth() == 0);  // every save restored
    b->hide();
    RecordingPainter q(2.0, {100, 50});
    r.w.paintTree(q);
    CHECK(q.ops().size() == 1 && q.ops()[0].rect.x == 0.0f);
    (void)a;
}

TEST(crisp_strokes_land_on_whole_pixels) {
    RecordingPainter p(1.5);
    const auto c = p.crisp({0, 0, 20, 10}, 1.0f);  // 1 DIP = 1.5 px -> 2 px
    auto px = [](float dip, float want) { return std::abs(dip * 1.5f - want) < 1e-4f; };
    CHECK(px(c.width, 2.0f));
    CHECK(px(c.rect.x, 1.0f) && px(c.rect.y, 1.0f));             // centre of a 2-px line at px 0..2
    CHECK(px(c.rect.width, 28.0f) && px(c.rect.height, 13.0f));  // 30 - 2, 15 - 2
}

int main(int argc, char** argv) { return minitest::runAll(argc, argv); }

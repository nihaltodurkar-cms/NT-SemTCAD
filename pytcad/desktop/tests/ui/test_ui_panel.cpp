// The N2b gate on real windows (NATIVE-DESKTOP-PLAN.md 27.7): a sample panel built from every layout feature of the
// measured Qt subset (Form with labels, a Fixed field and a spanning row; a Box row with a stretch; a Grid with a
// 1x3 span, as view3d_panel's crop grid; a Stack showing its second page), drawn through UiWindow + D2DPainter on
// WARP. Goldens at 100/150/200%, the Direct2D pixels checked against the RecordingPainter's display list,
// invalidation coalescing into one layout pass and one frame, DPI changes re-laying out in pixels, and update()
// reaching a real WM_PAINT. Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/core/style.hpp"
#include "ui/win32/ui_window.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;

namespace {

constexpr float kPanelW = 360, kPanelH = 400;
constexpr Policy kFixed{SizePolicy::Fixed, SizePolicy::Fixed};
constexpr Policy kGrowH{SizePolicy::Preferred, SizePolicy::Fixed};

// A coloured block with a crisp 1-DIP border: stands in for the N3 widgets (buttons, fields) at their hint sizes.
class Swatch final : public Widget {
public:
    Swatch(T fill, SizeF hint, Policy p) : fill_(fill), hint_(hint) { setSizePolicy(p); }
    SizeF sizeHint() const override { return hint_; }
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({0, 0, s.width, s.height}, token(fill_));
        const auto c = p.crisp({0, 0, s.width, s.height}, 1.0f);
        p.strokeRect(c.rect, token(T::BorderStrong), c.width);
    }
    T fill() const { return fill_; }

private:
    T fill_;
    SizeF hint_;
};

// One line of text sized by the text engine (the N3 Label's core).
class Caption final : public Widget {
public:
    explicit Caption(std::string text, float size = 12.0f, bool bold = false) : text_(std::move(text)) {
        style_.size = size;
        style_.bold = bold;
        style_.color = token(T::Text);
    }
    SizeF sizeHint() const override {
        TextEngine* te = textEngine();
        const SizeF m = te ? te->measure(text_, style_) : SizeF{0, 0};
        return {std::ceil(m.width) + 2, std::ceil(m.height)};
    }
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.drawText({1, 0, s.width - 1, s.height}, text_, style_);
    }

private:
    std::string text_;
    TextStyle style_;
};

struct Panel {
    Swatch* fixed_button = nullptr;
    Swatch* temperature = nullptr;
    Caption* title = nullptr;
    StackLayout* stack = nullptr;
};

Panel buildPanel(Widget& root) {
    Panel out;
    auto* col = root.setLayout<BoxLayout>(Orientation::Vertical);
    out.title = col->add<Caption>("N2b sample panel", 14.0f, true);

    auto* form = col->addForm();
    out.temperature = root.addChild<Swatch>(T::Base, SizeF{120, 22}, kGrowH);
    form->addRow(root.addChild<Caption>("Temperature [C]"), out.temperature);
    form->addRow(root.addChild<Caption>("Time [s]"), root.addChild<Swatch>(T::Base, SizeF{120, 22}, kGrowH));
    form->addRow(root.addChild<Caption>("Material"), root.addChild<Swatch>(T::AlternateBase, SizeF{80, 22}, kFixed));
    form->addRow(root.addChild<Caption>("A spanning row: \xC2\xB5m, \xE2\x88\x87\xC2\xB7J = 0"));

    auto* row = col->addBox(Orientation::Horizontal);
    out.fixed_button = row->add<Swatch>(T::Accent, SizeF{64, 24}, kFixed);
    row->add<Swatch>(T::Base, SizeF{64, 24}, kFixed);
    row->addStretch();
    row->add<Swatch>(T::Ok, SizeF{24, 24}, kFixed);

    auto* grid = col->addGrid();
    const char* axes[] = {"X", "Y", "Z"};
    for (int a = 0; a < 3; ++a) {
        grid->addWidget(root.addChild<Caption>(axes[a]), a, 0);
        grid->addWidget(root.addChild<Swatch>(T::Base, SizeF{60, 22}, kGrowH), a, 1);
        grid->addWidget(root.addChild<Swatch>(T::Base, SizeF{60, 22}, kGrowH), a, 2);
    }
    grid->addWidget(root.addChild<Swatch>(T::AccentSoft, SizeF{80, 24}, kGrowH), 3, 0, 1, 3);

    auto* pages = col->add<Widget>();
    out.stack = pages->setLayout<StackLayout>();
    out.stack->setContentsMargins(0, 0, 0, 0);
    out.stack->addWidget(pages->addChild<Swatch>(T::Error, SizeF{100, 40}, kGrowH));
    out.stack->addWidget(pages->addChild<Swatch>(T::Selection, SizeF{100, 40}, kGrowH));
    out.stack->setCurrentIndex(1);
    col->addStretch(1);
    return out;
}

std::unique_ptr<UiWindow> panelWindow(double scale, Panel* panel = nullptr) {
    auto w = UiWindow::create(warp(), {.title = L"tcad_ui_panel_tests", .width = 360, .height = 400, .scale_override = scale});
    if (!w) {
        std::printf("  UiWindow::create: %s\n", w.error().c_str());
        return nullptr;
    }
    (*w)->resizeClient(px(kPanelW, scale), px(kPanelH, scale));
    Panel p = buildPanel((*w)->root());
    if (panel) *panel = p;
    return std::move(*w);
}

}  // namespace

TEST(panel_matches_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        auto w = panelWindow(pct / 100.0);
        CHECK(w != nullptr);
        if (!w) return;
        Image img;
        CHECK(w->renderNow(&img) == FrameStatus::Presented);
        CHECK(img.width == px(kPanelW, pct / 100.0) && img.height == px(kPanelH, pct / 100.0));
        const GoldenResult r = checkGolden(*warp(), "n2b_panel@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}

TEST(direct2d_draws_what_the_display_list_says) {
    Panel panel;
    auto w = panelWindow(1.0, &panel);
    CHECK(w != nullptr);
    if (!w) return;
    Image img;
    CHECK(w->renderNow(&img) == FrameStatus::Presented);
    RecordingPainter rec(1.0, {kPanelW, kPanelH});
    w->root().paintTree(rec);
    const auto& ops = rec.ops();
    int checked = 0, wrong = 0, texts = 0;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (ops[i].kind == "text") ++texts;
        if (ops[i].kind != "fillRect" || ops[i].rect.width < 8 || ops[i].rect.height < 8) continue;
        const int cx = static_cast<int>(ops[i].rect.x + ops[i].rect.width / 2), cy = static_cast<int>(ops[i].rect.y + ops[i].rect.height / 2);
        bool covered = false;  // a later op drawing over this centre would change the pixel
        for (std::size_t j = i + 1; j < ops.size() && !covered; ++j) {
            if (ops[j].kind.starts_with("stroke") || ops[j].kind == "line") continue;  // outlines leave the centre alone
            const RectF& r = ops[j].rect;
            covered = cx >= r.x && cx < r.right() && cy >= r.y && cy < r.bottom();
        }
        if (covered) continue;
        const Color c = ops[i].color;
        const std::uint32_t want = 0xFF000000u | (static_cast<std::uint32_t>(std::lround(c.r * 255)) << 16) |
                                   (static_cast<std::uint32_t>(std::lround(c.g * 255)) << 8) |
                                   static_cast<std::uint32_t>(std::lround(c.b * 255));
        ++checked;
        if (img.pixel(cx, cy) != want) {
            ++wrong;
            std::printf("  %s: pixel at (%d,%d) is %08x\n", ops[i].str().c_str(), cx, cy, img.pixel(cx, cy));
        }
    }
    std::printf("  %zu ops, %d fill centres checked against the pixels, %d wrong; %d text ops\n", ops.size(), checked, wrong, texts);
    // every swatch fill (all fillRects but the root background, whose centre the panel covers) was compared
    const auto fills = std::count_if(ops.begin(), ops.end(), [](const PaintOp& o) { return o.kind == "fillRect"; });
    CHECK(checked == fills - 1 && wrong == 0);
    CHECK_EQ(texts, 8);  // title + 3 form labels + the spanning row + X/Y/Z
    // the hidden stack page (Error) is not drawn at all
    const Color err = token(T::Error);
    for (const auto& op : ops) CHECK(!(op.kind == "fillRect" && op.color == err));
}

TEST(updates_coalesce_into_one_layout_pass_and_one_frame) {
    Panel panel;
    auto w = panelWindow(1.0, &panel);
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(w->renderNow() == FrameStatus::Presented);
    CHECK(!w->layoutPending() && w->dirty().empty());
    // a plain repaint: exactly the widget's rectangle, no layout
    panel.temperature->update();
    CHECK((w->dirty() == panel.temperature->windowRect()));
    CHECK(!w->layoutPending());
    const int passes = w->layoutPasses(), frames = w->frames();
    // three geometry changes in a row: one layout pass, one frame
    panel.temperature->setMinimumHeight(30);
    panel.fixed_button->hide();
    panel.stack->setCurrentIndex(0);
    CHECK(w->layoutPending());
    CHECK(w->renderNow() == FrameStatus::Presented);
    CHECK_EQ(w->layoutPasses(), passes + 1);
    CHECK_EQ(w->frames(), frames + 1);
    CHECK(panel.temperature->geometry().height == 30);
    CHECK(w->dirty().empty());
}

TEST(a_dpi_change_lays_the_tree_out_again_in_pixels) {
    Panel panel;
    auto w = panelWindow(1.0, &panel);
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(w->renderNow() == FrameStatus::Presented);
    CHECK(panel.fixed_button->geometry().width == 64 && panel.fixed_button->geometry().height == 24);
    for (double s : {1.25, 1.5, 2.0}) {
        w->setScale(s);
        w->resizeClient(px(kPanelW, s), px(kPanelH, s));
        CHECK(w->renderNow() == FrameStatus::Presented);
        const RectI g = panel.fixed_button->geometry();
        std::printf("  %.0f%%: Fixed 64x24-DIP button is %dx%d px at (%d,%d)\n", s * 100, g.width, g.height, g.x, g.y);
        CHECK(g.width == static_cast<int>(std::ceil(64 * s)) && g.height == static_cast<int>(std::ceil(24 * s)));
        CHECK(g.x == px(9, s));  // the panel's margin, rounded to nearest
    }
}

TEST(an_update_reaches_wm_paint_and_draws_one_frame) {
    Panel panel;
    auto w = panelWindow(1.0, &panel);
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(w->renderNow() == FrameStatus::Presented);
    // Windows keeps no update region for a HIDDEN window (InvalidateRect is a no-op there), so the request count
    // is checked here and WM_PAINT is delivered through the window procedure by hand; the visible-window path is
    // tcad_ui_demo's.
    const int frames = w->frames(), requests = w->paintRequests();
    panel.temperature->update();
    panel.title->update();  // coalesced: still one paint request
    CHECK_EQ(w->paintRequests(), requests + 1);
    SendMessageW(w->window().hwnd(), WM_PAINT, 0, 0);  // the window procedure -> on_paint -> renderNow
    CHECK_EQ(w->frames(), frames + 1);
    CHECK(w->dirty().empty());
    panel.title->update();  // after the frame, a new request
    CHECK_EQ(w->paintRequests(), requests + 2);
}

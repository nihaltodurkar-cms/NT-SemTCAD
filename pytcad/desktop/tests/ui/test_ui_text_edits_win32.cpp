// N3d on real windows (NATIVE-DESKTOP-PLAN.md 27.8.5): the completed LineEdit (validator, placeholder, editing-finished
// and commit rules, the double click, the inactive selection), the multi-line PlainTextEdit (lines, scroll bars, following
// the end, a capped log, keys by visual line, wrapping, the mouse across lines, the clipboard), the selectable label, the
// text engine's selection rectangles (bidirectional text), goldens at 100/150/200% in the light theme and in high contrast,
// and UI Automation through the real client (Text pattern by line, the scroll bars' RangeValue). Everything is driven by
// REAL window messages. Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/validator.hpp"
#include "ui/win32/clipboard.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/plain_text_edit.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;
using tcad::platform::Mod;
using tcad::platform::MouseButton;

namespace {

constexpr float kW = 420, kH = 430;

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

// The window's scene for goldens and structure: edits (one focused with a selection, one with a placeholder, one with a
// validator, one with an inactive selection), a selectable label with a selection, a styled monospace console that
// scrolls both ways with a selection across two lines, and an empty view with a placeholder.
struct TextWindow {
    std::unique_ptr<UiWindow> w;
    LineEdit *name = nullptr, *filter = nullptr, *range = nullptr, *other = nullptr;
    Label* status = nullptr;
    PlainTextEdit *console = nullptr, *netlist = nullptr;

    explicit TextWindow(double scale = 1.0) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_text_edit_tests", .width = 420, .height = 430, .scale_override = scale});
        if (!r) {
            std::printf("  UiWindow::create: %s\n", r.error().c_str());
            return;
        }
        w = std::move(*r);
        w->resizeClient(px(kW, scale), px(kH, scale));
        w->router().setAlwaysShowCues(false);
        Widget& root = w->root();
        HWND hwnd = w->window().hwnd();
        auto* col = root.setLayout<BoxLayout>(Orientation::Vertical);
        auto* form = col->addForm();
        name = root.addChild<LineEdit>(hwnd);
        name->name = "name";
        name->setText("Gate oxide");
        form->addRow("&Name", name);
        filter = root.addChild<LineEdit>(hwnd);
        filter->name = "filter";
        filter->setPlaceholderText("a DeviceSpec .json or a project file");
        form->addRow("&File", filter);
        range = root.addChild<LineEdit>(hwnd);
        range->name = "range";
        range->setValidator(std::make_shared<DoubleValidator>());
        range->setText("1e17");
        form->addRow("&Min", range);
        other = root.addChild<LineEdit>(hwnd);
        other->name = "other";
        other->setText("Selected text here");
        form->addRow("&Other", other);
        status = root.addChild<Label>("Elapsed 12.5 s, 3 of 8 steps");
        status->name = "status";
        status->setSelectable(true);
        form->addRow("Status", status);
        console = root.addChild<PlainTextEdit>(hwnd);
        console->name = "console";
        console->setReadOnly(true);
        console->setMonospace(true);
        console->setWordWrap(false);
        console->setFollowEnd(true);
        col->addWidget(console, 3);
        netlist = root.addChild<PlainTextEdit>(hwnd);
        netlist->name = "netlist";
        netlist->setReadOnly(true);
        netlist->setPlaceholderText("The fitted SPICE netlist appears here after extraction.");
        col->addWidget(netlist, 1);
        for (LineEdit* e : {name, filter, range, other}) e->setCaretBlinking(false);
        console->setCaretBlinking(false);
        netlist->setCaretBlinking(false);
        const LineFormat stage{T::Text, true, false}, dim{T::TextDim, false, false}, note{T::TextDim, false, true}, err{T::Error, false, false};
        console->appendLine("[1/3] Meshing the device", stage);
        console->appendLine("  nodes: 4096, elements: 7938", {});
        console->appendLine("  warning: coarse mesh near the gate edge", dim);
        console->appendLine("note: the solver will refine adaptively", note);
        console->appendLine("[2/3] Solving the equilibrium", stage);
        console->appendLine("  Newton 1: |dx| = 3.2e-1", {});
        console->appendLine("  Newton 2: |dx| = 4.1e-3", {});
        console->appendLine("  Newton 3: |dx| = 5.0e-9, converged", {});
        console->appendLine("error: bias step 4 did not converge after 25 iterations (residual 1.3e-2, the continuation step was cut 6 times)", err);
        console->appendLine("[3/3] Writing the result", stage);
        console->appendLine("  result: build/out/run_0007/result.npz", {});
        console->appendLine("  elapsed: 12.5 s", {});
        w->renderNow(nullptr, false);
        // selections, set once the layout has given everything its size
        name->setFocus(FocusReason::Tab);
        name->model().setSelection(0, 4);  // "Gate", the focused edit: an ACTIVE selection
        other->model().setSelection(0, 8);  // "Selected": the same, unfocused: an INACTIVE one
        status->setSelection(0, 7);         // "Elapsed"
        console->model().setSelection(console->model().lineStart(60) + 2, console->model().lineStart(100) + 8);
        console->scrollToStart();
    }
    PointF at(Widget* x, double dx, double dy) const {
        const RectI r = x->windowRect();
        return {static_cast<float>(r.x / w->scale() + dx), static_cast<float>(r.y / w->scale() + dy)};
    }
};

// One multi-line edit alone in a window, with a single-line edit under it to move the focus to.
struct EditorWindow {
    std::unique_ptr<UiWindow> w;
    PlainTextEdit* edit = nullptr;
    LineEdit* other = nullptr;
    explicit EditorWindow(float width = 280, float height = 120, double scale = 1.0) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_text_editor_tests", .width = 320, .height = 220, .scale_override = scale});
        if (!r) return;
        w = std::move(*r);
        w->resizeClient(px(320, scale), px(220, scale));
        Widget& root = w->root();
        edit = root.addChild<PlainTextEdit>(w->window().hwnd());
        edit->name = "editor";
        edit->setGeometry({px(10, scale), px(10, scale), px(width, scale), px(height, scale)});
        other = root.addChild<LineEdit>(w->window().hwnd());
        other->setGeometry({px(10, scale), px(150, scale), px(200, scale), px(24, scale)});
        edit->setCaretBlinking(false);
        other->setCaretBlinking(false);
        w->renderNow(nullptr, false);
    }
    PointF at(double dx, double dy) const {  // DIPs inside the edit
        const RectI r = edit->windowRect();
        return {static_cast<float>(r.x / w->scale() + dx), static_cast<float>(r.y / w->scale() + dy)};
    }
    // The widget-local point of byte `offset` (the caret's), in window DIPs, a little inside the line.
    PointF atOffset(std::size_t offset) const {
        const std::size_t anchor = edit->model().anchor(), caret = edit->model().caret();  // the selection is the test's: put back
        edit->model().setSelection(offset, offset);
        const RectF r = edit->caretRectInWidget();
        edit->model().setSelection(anchor, caret);
        return at(r.x + 1, r.y + r.height / 2);
    }
    static std::string lines(int n, const char* stem = "line ") {
        std::string s;
        for (int i = 1; i <= n; ++i) s += (i > 1 ? "\n" : "") + std::string(stem) + std::to_string(i);
        return s;
    }
};

}  // namespace

// -- LineEdit -------------------------------------------------------------------------------------------------

TEST(a_validator_refuses_invalid_keys_and_pastes_and_gates_editing_finished) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    int returned = 0, finished = 0, committed = 0;
    f.range->on_return_pressed = [&] { ++returned; };
    f.range->on_editing_finished = [&] { ++finished; };
    f.range->on_commit = [&] { ++committed; };
    d.click(f.at(f.range, 30, 10));
    d.key('A', Mod::Ctrl);
    d.type(u"1e5");
    CHECK_STR(f.range->text(), "1e5");
    d.type(u"x");  // 1e5x can never be a number: the key does nothing
    CHECK_STR(f.range->text(), "1e5");
    d.type(u"-");  // nor can 1e5-
    CHECK_STR(f.range->text(), "1e5");
    CHECK(setClipboardText(nullptr, "9x"));
    d.key('V', Mod::Ctrl);  // a paste that would make it invalid is refused whole
    CHECK_STR(f.range->text(), "1e5");
    CHECK(setClipboardText(nullptr, "7"));
    d.key('V', Mod::Ctrl);
    CHECK_STR(f.range->text(), "1e57");
    d.key(VK_BACK);
    d.key(VK_BACK);  // "1e": unfinished, intermediate
    CHECK_STR(f.range->text(), "1e");
    CHECK(!f.range->hasAcceptableInput());
    d.key(VK_RETURN);  // an Intermediate text is not finished: nothing is reported
    CHECK(returned == 0 && finished == 0 && committed == 0);
    d.type(u"3");
    CHECK(f.range->hasAcceptableInput());
    d.key(VK_RETURN);
    CHECK(returned == 1 && finished == 1 && committed == 1);
    // setText bypasses the validator (QLineEdit::setText) -- and becomes the baseline of "changed"
    f.range->setText("not a number");
    CHECK_STR(f.range->text(), "not a number");
    CHECK(!f.range->hasAcceptableInput());
    f.range->setValidator(nullptr);
    CHECK(f.range->hasAcceptableInput());
    d.type(u"!");
    CHECK_STR(f.range->text(), "not a number!");  // no validator: everything goes
}

TEST(a_validator_with_a_range_lets_an_out_of_range_number_be_typed_but_not_finished) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.range->setValidator(std::make_shared<DoubleValidator>(0.0, 10.0));
    f.range->setText("5");
    int finished = 0;
    f.range->on_editing_finished = [&] { ++finished; };
    d.click(f.at(f.range, 30, 10));
    d.key(VK_END);
    d.type(u"0");  // 50: out of range, Intermediate, typed
    CHECK_STR(f.range->text(), "50");
    d.key(VK_RETURN);
    CHECK(finished == 0);
    d.key(VK_BACK);
    d.key(VK_RETURN);
    CHECK(finished == 1);
}

TEST(commit_fires_once_per_change_and_editing_finished_keeps_qts_rule) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    int returned = 0, finished = 0, committed = 0;
    f.name->on_return_pressed = [&] { ++returned; };
    f.name->on_editing_finished = [&] { ++finished; };
    f.name->on_commit = [&] { ++committed; };
    f.name->setFocus(FocusReason::Tab);
    d.key(VK_RETURN);  // nothing changed: Qt's editingFinished and returnPressed fire; commit does not
    CHECK(returned == 1 && finished == 1 && committed == 0);
    d.key(VK_END);
    d.type(u"!");
    d.key(VK_RETURN);
    CHECK(returned == 2 && finished == 2 && committed == 1);
    d.key(VK_RETURN);  // again, unchanged
    CHECK(committed == 1 && finished == 3);
    d.type(u"?");
    f.other->setFocus(FocusReason::Tab);  // the focus leaves with a change pending
    CHECK(finished == 4 && committed == 2 && returned == 3);
    f.name->setFocus(FocusReason::Tab);
    f.other->setFocus(FocusReason::Tab);  // leaves unchanged
    CHECK(finished == 5 && committed == 2);
    f.name->setText("Gate oxide");  // the program's text is the new baseline: loading a document is no edit
    f.name->setFocus(FocusReason::Tab);
    f.other->setFocus(FocusReason::Tab);
    CHECK(committed == 2);
    f.name->setFocus(FocusReason::Tab);
    d.type(u"x");
    d.key(VK_BACK);  // typed and taken back: the same text as the baseline
    f.other->setFocus(FocusReason::Tab);
    CHECK(committed == 2);
}

TEST(the_placeholder_shows_while_the_text_is_empty_and_only_then) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    auto placeholderOps = [&](LineEdit* e) {
        RecordingPainter p(f.w->scale());
        e->paint(p);
        int n = 0;
        for (const auto& op : p.ops())
            if (op.kind == "text" && op.text == e->placeholderText()) n += op.color.g == token(T::TextFaint).g ? 1 : 100;  // 100: the wrong colour
        return n;
    };
    CHECK(placeholderOps(f.filter) == 1);
    f.filter->setFocus(FocusReason::Tab);
    CHECK(placeholderOps(f.filter) == 1);  // still there while focused and empty (Qt's)
    Driver d(*f.w);
    d.type(u"a");
    CHECK(placeholderOps(f.filter) == 0);
    d.key(VK_BACK);
    CHECK(placeholderOps(f.filter) == 1);
    CHECK(placeholderOps(f.name) == 0);  // an edit with text never shows one
}

TEST(a_double_click_selects_a_word_and_a_triple_one_is_not_built) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    TextStyle st;
    const float x = f.w->text().caretRect("Gate oxide", st, 0, 7).x;  // inside "oxide"
    const PointF p = f.at(f.name, LineEdit::kPadding + x + 1, 10);
    d.press(p);
    d.release(p);
    d.press(p, MouseButton::Left, true);  // the second press of a double click arrives as WM_LBUTTONDBLCLK
    d.release(p);
    CHECK_STR(f.name->model().selectedText(), "oxide");
    const float x2 = f.w->text().caretRect("Gate oxide", st, 0, 2).x;
    const PointF q = f.at(f.name, LineEdit::kPadding + x2 + 1, 10);
    d.press(q);
    d.release(q);
    d.press(q, MouseButton::Left, true);
    d.release(q);
    CHECK_STR(f.name->model().selectedText(), "Gate");
}

TEST(the_selection_stays_when_the_focus_leaves_and_is_drawn_inactive) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    auto selectionFill = [&](LineEdit* e) {  // the colour of the first selection-sized fill
        RecordingPainter p(f.w->scale());
        e->paint(p);
        for (const auto& op : p.ops())
            if (op.kind == "fillRect" && op.rect.x > 3 && op.rect.width > 5 && op.rect.width < 100 && op.rect.height > 8) return op.color;
        return Color{-1, -1, -1, -1};
    };
    const Color active = selectionFill(f.name), inactive = selectionFill(f.other);
    CHECK(active.b == token(T::Selection).b && inactive.b == token(T::AlternateBase).b);
    f.other->setFocus(FocusReason::Tab);
    CHECK_STR(f.name->model().selectedText(), "Gate");  // still selected...
    CHECK(selectionFill(f.name).b == token(T::AlternateBase).b);  // ...and drawn as inactive now
    CHECK(selectionFill(f.other).b == token(T::Selection).b);
}

TEST(in_high_contrast_the_inactive_selection_is_an_outline_and_the_active_one_uses_highlight_text) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    const HighContrast saved = highContrast();
    nightSky();
    RecordingPainter inactive(f.w->scale());
    f.other->paint(inactive);  // unfocused, selection [0, 8)
    int outlines = 0, fills = 0;
    for (const auto& op : inactive.ops()) {
        if (op.kind == "strokeRect" && op.rect.width > 5 && op.rect.width < 100 && op.rect.height > 8 && op.rect.height < 24 && op.rect.x > 3) ++outlines;
        if (op.kind == "fillRect" && op.rect.width > 5 && op.rect.width < 100 && op.rect.height > 8 && op.rect.height < 24 && op.rect.x > 3) ++fills;
    }
    CHECK(outlines == 1 && fills == 0);  // an alternate-base fill would be the window colour: invisible
    RecordingPainter active(f.w->scale());
    f.name->paint(active);  // focused, selection [0, 4)
    int redraws = 0;
    for (const auto& op : active.ops())
        if (op.kind == "text" && op.text == "Gate oxide" && op.color.r == 0.0f && op.color.g == 0.0f) ++redraws;  // black: highlight text
    CHECK(redraws == 1);
    highContrast() = saved;
}

// -- PlainTextEdit: lines ------------------------------------------------------------------------------------

TEST(a_plain_text_edit_is_lines_and_counts_them_like_qt) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    CHECK(f.edit->lineCount() == 1 && f.edit->lineText(0).empty());  // an empty document has one block
    f.edit->setPlainText("a\nbb\n\nccc");
    CHECK(f.edit->lineCount() == 4);
    CHECK_STR(f.edit->lineText(0), "a");
    CHECK_STR(f.edit->lineText(2), "");
    CHECK_STR(f.edit->lineText(3), "ccc");
    CHECK_STR(f.edit->lineText(9), "");  // out of range
    CHECK_STR(f.edit->toPlainText(), "a\nbb\n\nccc");
    f.edit->setPlainText("");
    CHECK(f.edit->lineCount() == 1);
    f.edit->setPlainText("x\n");
    CHECK(f.edit->lineCount() == 2 && f.edit->lineText(1).empty());  // a trailing line feed is an empty last line
    f.edit->clear();
    f.edit->appendLine("");  // the first line fills the empty view: this one is an empty line
    f.edit->appendLine("b");
    CHECK(f.edit->lineCount() == 2 && f.edit->lineText(0).empty() && f.edit->lineText(1) == "b");
    CHECK_STR(f.edit->toPlainText(), "\nb");
    f.edit->clear();
    f.edit->appendLine("first");
    f.edit->appendLine("second");
    CHECK_STR(f.edit->toPlainText(), "first\nsecond");
}

TEST(per_line_formats_follow_their_lines_and_a_user_edit_that_changes_the_count_resets_them) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.edit->appendLine("plain");
    f.edit->appendLine("loud", {T::Error, true, false});
    f.edit->appendLine("soft", {T::TextDim, false, true});
    CHECK(f.edit->lineFormat(1) == (LineFormat{T::Error, true, false}));
    CHECK(f.edit->lineFormat(2).italic && !f.edit->lineFormat(0).bold);
    CHECK(f.edit->lineFormat(7) == LineFormat{});  // out of range: the default
    // the text is drawn per line in its format
    RecordingPainter p(f.w->scale());
    f.edit->paint(p);
    bool bold = false, italic = false;
    for (const auto& op : p.ops()) {
        if (op.kind == "text" && op.text == "loud") bold = op.style.bold && op.style.color.r == token(T::Error).r;
        if (op.kind == "text" && op.text == "soft") italic = op.style.italic;
    }
    CHECK(bold && italic);
    f.edit->setReadOnly(false);
    Driver d(*f.w);
    d.click(f.at(30, 8));
    d.key(VK_END);
    d.key(VK_RETURN);  // a new line: the formats are for logs and go back to the default
    CHECK(f.edit->lineCount() == 4 && f.edit->lineFormat(1) == LineFormat{});
}

TEST(a_maximum_line_count_drops_the_oldest_lines_and_counts_them) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.edit->setReadOnly(true);
    f.edit->setMaximumLineCount(5);
    for (int i = 1; i <= 8; ++i) f.edit->appendLine("L" + std::to_string(i), {i % 2 ? T::Text : T::Error, false, false});
    CHECK(f.edit->lineCount() == 5 && f.edit->droppedLines() == 3);
    CHECK_STR(f.edit->lineText(0), "L4");
    CHECK_STR(f.edit->lineText(4), "L8");
    CHECK(f.edit->lineFormat(0).color == T::Error && f.edit->lineFormat(1).color == T::Text);  // the formats moved with the lines
    f.edit->setMaximumLineCount(2);
    CHECK(f.edit->lineCount() == 2 && f.edit->droppedLines() == 6 && f.edit->lineText(0) == "L7");
    f.edit->setPlainText("p\nq\nr\ns");  // setPlainText resets the count, then caps
    CHECK(f.edit->lineCount() == 2 && f.edit->droppedLines() == 2 && f.edit->lineText(0) == "r");
    f.edit->setMaximumLineCount(0);
    f.edit->setPlainText("p\nq\nr\ns");
    CHECK(f.edit->lineCount() == 4 && f.edit->droppedLines() == 0);
}

TEST(trimming_keeps_the_selection_on_its_text_and_the_view_where_it_was) {
    EditorWindow f(280, 60);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.edit->setReadOnly(true);
    f.edit->setMaximumLineCount(30);
    for (int i = 1; i <= 30; ++i) f.edit->appendLine("row " + std::to_string(i));
    const std::size_t at = f.edit->toPlainText().find("row 20");
    f.edit->model().setSelection(at, at + 6);
    f.edit->scrollToStart();
    f.edit->appendLine("row 31");  // drops "row 1": everything moves up a line
    CHECK_STR(f.edit->model().selectedText(), "row 20");
    CHECK(f.edit->scrollY() == 0);  // the reader was at the top: not dragged anywhere
    CHECK(f.edit->droppedLines() == 1);
}

// -- PlainTextEdit: scrolling --------------------------------------------------------------------------------

TEST(scroll_bars_appear_when_the_content_does_not_fit_and_the_horizontal_one_only_without_wrapping) {
    EditorWindow f(200, 100);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.edit->setPlainText("one\ntwo\nthree");
    CHECK(!f.edit->verticalScrollBar()->isVisibleSelf() && !f.edit->horizontalScrollBar()->isVisibleSelf());
    f.edit->setPlainText(EditorWindow::lines(30));
    ScrollBar* v = f.edit->verticalScrollBar();
    CHECK(v->isVisibleSelf() && v->isNeeded() && !f.edit->horizontalScrollBar()->isVisibleSelf());
    CHECK(v->maximum() > 0 && v->pageStep() > 50);
    // the bar sits on the right edge, inside the frame, and the text's viewport is narrower by its width
    const RectI g = f.edit->geometry(), vg = v->geometry();
    CHECK(vg.right() <= g.width && vg.right() >= g.width - 2 && vg.x > g.width / 2);
    std::string words;
    for (int i = 0; i < 60; ++i) words += "word ";
    f.edit->setPlainText(words + "\nshort");  // a long word would overflow instead of wrapping, so many short ones
    CHECK(f.edit->wordWrap() && !f.edit->horizontalScrollBar()->isVisibleSelf());  // wrapped: it fits across, more lines
    const float wrapped_h = f.edit->contentHeight();
    f.edit->setWordWrap(false);
    CHECK(f.edit->horizontalScrollBar()->isVisibleSelf() && f.edit->horizontalScrollBar()->maximum() > 100);
    CHECK(f.edit->contentHeight() < wrapped_h);  // two lines now, not several
    f.edit->setWordWrap(true);
    CHECK(!f.edit->horizontalScrollBar()->isVisibleSelf() && f.edit->contentHeight() == wrapped_h);
    f.edit->setPlainText(std::string(300, 'w'));  // one word wider than the view: it overflows, and still gets no bar when wrapping
    CHECK(!f.edit->horizontalScrollBar()->isVisibleSelf());
}

TEST(the_wheel_scrolls_three_lines_a_notch_and_stops_at_the_ends) {
    EditorWindow f(200, 100);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText(EditorWindow::lines(40));
    f.edit->setReadOnly(true);
    const int line = f.edit->verticalScrollBar()->singleStep();
    CHECK(line >= 12 && line <= 20);
    d.wheel(f.at(50, 30), -1);  // toward the user: down
    CHECK(f.edit->scrollY() == 3 * line);
    d.wheel(f.at(50, 30), -2);
    CHECK(f.edit->scrollY() == 9 * line);
    d.wheel(f.at(50, 30), 1);
    CHECK(f.edit->scrollY() == 6 * line);
    d.wheel(f.at(50, 30), 50);
    CHECK(f.edit->scrollY() == 0);
    d.wheel(f.at(50, 30), -200);  // (a notch count past 273 would overflow the message's short)
    CHECK(f.edit->atEnd() && f.edit->scrollY() == f.edit->verticalScrollBar()->maximum());
    EditorWindow g(200, 100);  // nothing to scroll: the wheel is not used (it goes to whatever is behind)
    g.edit->setPlainText("a\nb");
    Driver dg(*g.w);
    dg.wheel(g.at(50, 30), -1);
    CHECK(g.edit->scrollY() == 0);
}

TEST(following_the_end_stays_at_the_end_only_while_the_reader_is_there) {
    EditorWindow f(240, 90);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.edit->setReadOnly(true);
    f.edit->setFollowEnd(true);
    for (int i = 1; i <= 30; ++i) f.edit->appendLine("row " + std::to_string(i));
    CHECK(f.edit->atEnd() && f.edit->scrollY() == f.edit->verticalScrollBar()->maximum() && f.edit->scrollY() > 0);
    const int before = f.edit->scrollY();
    f.edit->appendLine("row 31");
    CHECK(f.edit->atEnd() && f.edit->scrollY() > before);  // followed
    f.edit->verticalScrollBar()->setValue(0);  // the reader scrolls back to look at something
    for (int i = 32; i <= 40; ++i) f.edit->appendLine("row " + std::to_string(i));
    CHECK(f.edit->scrollY() == 0 && !f.edit->atEnd());  // not dragged away
    f.edit->scrollToEnd();
    f.edit->appendLine("row 41");
    CHECK(f.edit->atEnd());  // back at the end: following again
    f.edit->setFollowEnd(false);
    f.edit->verticalScrollBar()->setValue(f.edit->verticalScrollBar()->maximum());
    const int at = f.edit->scrollY();
    f.edit->appendLine("row 42");
    CHECK(f.edit->scrollY() == at);  // not following: the view stays, even at the end
}

TEST(the_scroll_bar_thumb_and_track_work_with_real_mouse_messages) {
    EditorWindow f(200, 100);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText(EditorWindow::lines(60));
    f.edit->setReadOnly(true);
    ScrollBar* v = f.edit->verticalScrollBar();
    const RectI vg = v->windowRect();
    const double s = f.w->scale();
    auto barPoint = [&](double along) { return PointF{static_cast<float>((vg.x + vg.width / 2.0) / s), static_cast<float>(vg.y / s + along)}; };
    const RectF thumb = v->thumbRect();
    d.press(barPoint(thumb.y + thumb.height / 2));
    d.move(barPoint(thumb.y + thumb.height / 2 + 30));
    d.release(barPoint(thumb.y + thumb.height / 2 + 30));
    CHECK(f.edit->scrollY() > 0);
    const int dragged = f.edit->scrollY();
    f.edit->scrollToStart();
    d.press(barPoint(v->trackRect().height - 3));  // on the track, below the thumb: one page
    d.release(barPoint(v->trackRect().height - 3));
    CHECK(f.edit->scrollY() == v->pageStep());
    CHECK(dragged > 0);
}

// -- PlainTextEdit: keys -------------------------------------------------------------------------------------

TEST(enter_types_a_line_feed_and_a_read_only_edit_leaves_the_key_alone) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.at(30, 8));
    d.type(u"ab");
    d.key(VK_RETURN);
    d.type(u"cd");
    CHECK_STR(f.edit->text(), "ab\ncd");
    CHECK(f.edit->lineCount() == 2);
    d.key('Z', Mod::Ctrl);  // undo the typing, then the line feed
    d.key('Z', Mod::Ctrl);
    CHECK_STR(f.edit->text(), "ab");
    f.edit->setReadOnly(true);
    d.type(u"zz");
    d.key(VK_RETURN);
    d.key(VK_BACK);
    CHECK_STR(f.edit->text(), "ab");  // read only: nothing changes
    CHECK(!f.edit->keyEvent({VK_RETURN, Mod::None, true, false}));  // and Enter is not taken, so a dialog could use it
}

TEST(up_and_down_keep_the_column_and_the_ends_of_the_text_are_the_limits) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText("line one\nline two is longer\nthree");
    d.click(f.at(30, 8));
    d.key(VK_HOME, Mod::Ctrl);
    CHECK(f.edit->model().caret() == 0);
    for (int i = 0; i < 5; ++i) d.key(VK_RIGHT);
    const float x0 = f.edit->caretRectInWidget().x;
    d.key(VK_DOWN);
    const std::size_t on_two = f.edit->model().caret();
    CHECK(on_two >= 9 && on_two <= 27);  // somewhere in "line two is longer"
    CHECK(std::fabs(f.edit->caretRectInWidget().x - x0) < 8);  // the same column, to within a letter
    const float y2 = f.edit->caretRectInWidget().y;
    d.key(VK_DOWN);
    CHECK(f.edit->model().caret() >= 28 && f.edit->caretRectInWidget().y > y2);
    d.key(VK_DOWN);  // the last line: Qt goes to the end of the text
    CHECK(f.edit->model().caret() == f.edit->text().size());
    d.key(VK_UP);
    d.key(VK_UP);
    CHECK(f.edit->model().caret() <= 8);
    d.key(VK_UP);  // the first line: to the start
    CHECK(f.edit->model().caret() == 0);
    d.key(VK_DOWN, Mod::Shift);  // extends the selection by a line
    CHECK(f.edit->model().anchor() == 0 && f.edit->model().caret() >= 9);
}

TEST(home_and_end_go_by_line_and_ctrl_home_and_end_by_document) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText("line one\nline two is longer\nthree");
    d.click(f.at(30, 8));
    d.key(VK_HOME, Mod::Ctrl);
    d.key(VK_END);
    CHECK(f.edit->model().caret() == 8);
    d.key(VK_DOWN);
    d.key(VK_HOME);
    CHECK(f.edit->model().caret() == 9);
    d.key(VK_END);
    CHECK(f.edit->model().caret() == 27);
    d.key(VK_END, Mod::Ctrl);
    CHECK(f.edit->model().caret() == f.edit->text().size());
    d.key(VK_HOME, Mod::Ctrl | Mod::Shift);
    CHECK(f.edit->model().anchor() == f.edit->text().size() && f.edit->model().caret() == 0);
}

TEST(page_down_moves_the_caret_and_the_view_by_a_view) {
    EditorWindow f(200, 100);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText(EditorWindow::lines(60));
    d.click(f.at(30, 8));
    d.key(VK_HOME, Mod::Ctrl);
    const RectF c0 = f.edit->caretRectInWidget();
    d.key(VK_NEXT);
    const RectF c1 = f.edit->caretRectInWidget();
    CHECK(f.edit->scrollY() > 0);
    CHECK(f.edit->model().caret() > 30);  // about a view of lines further down
    CHECK(c1.y >= 0 && c1.y < f.edit->viewportRect().height);  // and still on screen
    (void)c0;
    d.key(VK_PRIOR);
    CHECK(f.edit->model().caret() < 12 && f.edit->scrollY() == 0);
}

TEST(a_wrapped_line_is_several_visual_lines_for_up_down_home_and_end) {
    EditorWindow f(150, 100);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma");
    CHECK(f.edit->wordWrap() && f.edit->contentHeight() > 3 * 14);
    d.click(f.at(30, 8));
    d.key(VK_HOME, Mod::Ctrl);
    d.key(VK_END);  // the end of the FIRST visual line, not of the paragraph
    const std::size_t end1 = f.edit->model().caret();
    CHECK(end1 > 5 && end1 < f.edit->text().size() - 10);
    const float y1 = f.edit->caretRectInWidget().y;
    d.key(VK_DOWN);
    CHECK(f.edit->caretRectInWidget().y > y1 && f.edit->model().caret() > end1);
    d.key(VK_HOME);  // the start of the second visual line
    const std::size_t start2 = f.edit->model().caret();
    CHECK(start2 >= end1 - 1 && start2 <= end1 + 1 && f.edit->caretRectInWidget().x < 8);
    d.key(VK_UP);
    CHECK(f.edit->caretRectInWidget().y == y1);
    // a hard line's own range, for UI Automation, is the whole paragraph
    CHECK(f.edit->lineRangeAt(5) == (std::pair<std::size_t, std::size_t>{0, f.edit->text().size()}));
}

// -- PlainTextEdit: the mouse and the clipboard --------------------------------------------------------------

TEST(a_click_puts_the_caret_in_the_clicked_line_and_a_drag_selects_across_lines) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText("first line\nsecond line\nthird line");
    d.press(f.atOffset(14));  // inside "second"
    CHECK(f.edit->model().caret() >= 12 && f.edit->model().caret() <= 16);
    d.move(f.atOffset(26));  // into "third"
    d.release(f.atOffset(26));
    CHECK(f.edit->model().selection().first == f.edit->model().anchor());
    CHECK(f.edit->model().selectedText().find('\n') != std::string::npos);
    CHECK(f.edit->model().selectedText().size() > 10);
    // a click past the end of a line goes to that line's end; below the last line, to the end of the text
    d.press(f.at(250, 8));
    d.release(f.at(250, 8));
    CHECK(f.edit->model().caret() == 10);
    d.press(f.at(100, 110));
    d.release(f.at(100, 110));
    CHECK(f.edit->model().caret() == f.edit->text().size());
}

TEST(a_double_click_selects_a_word_in_a_line_and_a_multi_line_selection_copies_as_lines) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    const auto saved = clipboardText(nullptr);
    Driver d(*f.w);
    f.edit->setPlainText("alpha beta\ngamma delta");
    const PointF p = f.atOffset(13);  // inside "gamma"
    d.press(p);
    d.release(p);
    d.press(p, MouseButton::Left, true);
    d.release(p);
    CHECK_STR(f.edit->model().selectedText(), "gamma");
    f.edit->model().setSelection(6, 16);  // "beta\ngamma"
    d.key('C', Mod::Ctrl);
    CHECK(clipboardText(nullptr) == std::optional<std::string>("beta\ngamma"));
    // on the real clipboard it is CR LF, as Notepad and Excel expect, and comes back as one line feed
    if (OpenClipboard(nullptr)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            const auto* ws = static_cast<const wchar_t*>(GlobalLock(h));
            CHECK(ws != nullptr && std::wstring(ws) == L"beta\r\ngamma");
            GlobalUnlock(h);
        }
        CloseClipboard();
    }
    CHECK(setClipboardText(nullptr, "x\r\ny\rz"));  // CR LF and a lone CR are both line breaks
    CHECK(clipboardText(nullptr) == std::optional<std::string>("x\ny\nz"));
    f.edit->setReadOnly(false);
    f.edit->model().setSelection(0, f.edit->text().size());
    d.key('V', Mod::Ctrl);
    CHECK_STR(f.edit->text(), "x\ny\nz");
    CHECK(f.edit->lineCount() == 3);
    if (saved) setClipboardText(nullptr, *saved);
    else if (OpenClipboard(nullptr)) EmptyClipboard(), CloseClipboard();
}

TEST(dragging_below_the_view_scrolls_and_keeps_selecting) {
    EditorWindow f(200, 90);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setPlainText(EditorWindow::lines(60));
    f.edit->setReadOnly(true);
    d.press(f.at(20, 10));
    CHECK(f.edit->scrollY() == 0);
    const PointF below = f.at(20, 90 + 40);  // below the edit, button held
    d.move(below);  // one move: the caret goes to the line past the view and the view follows it by a line or so
    const int after_move = f.edit->scrollY();
    pumpFor(400);  // and the pointer, resting there, keeps it going on the timer
    CHECK(f.edit->scrollY() >= after_move + 3 * f.edit->verticalScrollBar()->singleStep());
    d.release(below);
    CHECK(f.edit->scrollY() > 0);
    CHECK(f.edit->model().caret() > 30 && f.edit->model().selectedText().size() > 30);
    // the timer stopped with the button
    const int y = f.edit->scrollY();
    pumpFor(200);
    CHECK(f.edit->scrollY() == y);
}

TEST(a_read_only_edit_selects_and_copies_but_never_changes) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    const auto saved = clipboardText(nullptr);
    Driver d(*f.w);
    f.edit->setPlainText("keep this text");
    f.edit->setReadOnly(true);
    d.click(f.at(30, 8));
    d.key('A', Mod::Ctrl);
    CHECK_STR(f.edit->model().selectedText(), "keep this text");
    d.key('C', Mod::Ctrl);
    CHECK(clipboardText(nullptr) == std::optional<std::string>("keep this text"));
    CHECK(setClipboardText(nullptr, "other"));
    d.key('X', Mod::Ctrl);
    d.key('V', Mod::Ctrl);
    d.key(VK_DELETE);
    d.type(u"q");
    CHECK_STR(f.edit->text(), "keep this text");
    CHECK(clipboardText(nullptr) == std::optional<std::string>("other"));  // cut did not touch it either
    CHECK(f.edit->accessibleReadOnly() && !f.edit->accessibleSetValue("nope") && f.edit->text() == "keep this text");
    if (saved) setClipboardText(nullptr, *saved);
    else if (OpenClipboard(nullptr)) EmptyClipboard(), CloseClipboard();
}

TEST(the_insert_and_delete_chords_copy_paste_and_cut_as_windows_edits_do) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    const auto saved = clipboardText(nullptr);
    Driver d(*f.w);
    f.edit->setPlainText("one two");
    d.click(f.at(30, 8));
    f.edit->model().setSelection(0, 3);
    d.key(VK_INSERT, Mod::Ctrl);  // Ctrl+Insert: copy
    CHECK(clipboardText(nullptr) == std::optional<std::string>("one"));
    f.edit->model().setSelection(7, 7);
    d.key(VK_INSERT, Mod::Shift);  // Shift+Insert: paste
    CHECK_STR(f.edit->text(), "one twoone");
    f.edit->model().setSelection(3, 7);
    d.key(VK_DELETE, Mod::Shift);  // Shift+Delete: cut
    CHECK_STR(f.edit->text(), "oneone");
    CHECK(clipboardText(nullptr) == std::optional<std::string>(" two"));
    d.key(VK_DELETE);  // a plain Delete does not touch the clipboard
    CHECK(clipboardText(nullptr) == std::optional<std::string>(" two"));
    if (saved) setClipboardText(nullptr, *saved);
    else if (OpenClipboard(nullptr)) EmptyClipboard(), CloseClipboard();
}

TEST(tab_moves_the_focus_out_of_a_plain_text_edit) {
    EditorWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setFocus(FocusReason::Tab);
    CHECK(!f.edit->wantsTab());
    d.key(VK_TAB);
    CHECK(f.other->hasFocus() && f.edit->text().empty());
}

// -- the selectable label ------------------------------------------------------------------------------------

TEST(a_selectable_label_selects_and_copies_with_real_messages) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    const auto saved = clipboardText(nullptr);
    Driver d(*f.w);
    const PointF a = f.at(f.status, 1, 8), b = f.at(f.status, 60, 8);
    d.press(a);
    d.move(b);
    d.release(b);
    CHECK(f.status->hasFocus());
    const std::string got = f.status->selectedText();
    CHECK(got.size() >= 6 && std::string("Elapsed 12.5 s, 3 of 8 steps").find(got) == 0);
    d.key('C', Mod::Ctrl);
    CHECK(clipboardText(nullptr) == std::optional<std::string>(got));
    d.press(f.at(f.status, 5, 8), MouseButton::Left, true);  // a double click: the word
    d.release(f.at(f.status, 5, 8));
    CHECK_STR(f.status->selectedText(), "Elapsed");
    if (saved) setClipboardText(nullptr, *saved);
    else if (OpenClipboard(nullptr)) EmptyClipboard(), CloseClipboard();
}

// -- the engine's selection rectangles ------------------------------------------------------------------------

TEST(selection_rectangles_follow_the_visual_runs_of_bidirectional_text) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    DWriteTextEngine& te = f.w->text();
    TextStyle st;
    // "abc " then three Hebrew letters, then " def": an LTR paragraph with an RTL run inside it
    const std::string s = "abc \xD7\x90\xD7\x91\xD7\x92 def";
    const float total = te.measure(s, st).width;
    // a range inside the RTL run: ONE rectangle, and it lies where the three letters are drawn, not between the logical ends
    const auto inner = te.selectionRects(s, st, 0, 4, 10);
    CHECK(inner.size() == 1);
    if (!inner.empty()) {
        // visually: "abc " first, then the three letters of the RTL run, then " def"
        const float lead = te.measure("abc ", st).width, run = te.measure("\xD7\x90\xD7\x91\xD7\x92", st).width;
        CHECK(std::fabs(inner[0].width - run) < 0.5f);
        CHECK(std::fabs(inner[0].x - lead) < 0.5f && inner[0].right() <= lead + run + 0.5f);
    }
    // a range from the LTR text into the RTL run: two pieces (the visual order is not the logical one)
    const auto across = te.selectionRects(s, st, 0, 2, 8);
    CHECK(across.size() >= 2);
    float sum = 0;
    for (const RectF& r : across) {
        CHECK(r.x >= -0.5f && r.right() <= total + 0.5f && r.height > 8);
        sum += r.width;
    }
    CHECK(std::fabs(sum - te.measure(s.substr(2, 6), st).width) < 1.5f);
    // plain left-to-right text: one rectangle between the two caret positions, as the edits drew it before
    const auto plain = te.selectionRects("hello world", st, 0, 2, 7);
    CHECK(plain.size() == 1);
    if (!plain.empty()) {
        CHECK(std::fabs(plain[0].x - te.caretRect("hello world", st, 0, 2).x) < 0.01f);
        CHECK(std::fabs(plain[0].right() - te.caretRect("hello world", st, 0, 7).x) < 0.01f);
    }
    // reversed and empty ranges
    CHECK(te.selectionRects("hello world", st, 0, 7, 2).size() == 1);
    CHECK(te.selectionRects("hello world", st, 0, 4, 4).empty());
    // two lines: one rectangle per line
    const auto two = te.selectionRects("ab\ncd", st, 0, 1, 4);
    CHECK(two.size() >= 2 && two.back().y > two.front().y);
}

TEST(italic_text_is_measured_and_cached_apart_from_upright_text) {
    TextWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    DWriteTextEngine& te = f.w->text();
    TextStyle up, it;
    it.italic = true;
    const auto before = te.stats();
    te.measure("Italic text", up);
    te.measure("Italic text", it);
    te.measure("Italic text", it);
    const auto after = te.stats();
    CHECK(after.misses == before.misses + 2 && after.hits >= before.hits + 1);  // two layouts, the third a cache hit
    CHECK(te.format(up) != te.format(it));
    CHECK(te.format(it)->GetFontStyle() == DWRITE_FONT_STYLE_ITALIC && te.format(up)->GetFontStyle() == DWRITE_FONT_STYLE_NORMAL);
}

// -- goldens ---------------------------------------------------------------------------------------------------

TEST(text_widgets_match_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        TextWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) return;
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        CHECK(img.width == px(kW, pct / 100.0) && img.height == px(kH, pct / 100.0));
        const GoldenResult r = checkGolden(*warp(), "n3d_text@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}

TEST(text_widgets_in_high_contrast_match_the_goldens) {
    const HighContrast saved = highContrast();
    for (int pct : {100, 150, 200}) {
        TextWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) break;
        nightSky();  // after the window exists: UiWindow::create reads the system's own state
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n3d_text_hc@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
    highContrast() = saved;
}

// -- UI Automation, through the real client --------------------------------------------------------------------

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

std::string rangeText(IUIAutomationTextRange* r) {
    BSTR b = nullptr;
    r->GetText(-1, &b);
    return str(b);
}

}  // namespace

TEST(uia_reads_a_multi_line_edit_by_line_and_reports_a_rectangle_per_line) {
    EditorWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.edit->setPlainText("line one\nline two is longer\nthree");
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto e = byId(a.Get(), win.Get(), "editor");
    CHECK(e != nullptr);
    if (!e) return;
    CONTROLTYPEID ct = 0;
    e->get_CurrentControlType(&ct);
    CHECK(ct == UIA_EditControlTypeId);
    ComPtr<IUIAutomationValuePattern> val;
    CHECK(SUCCEEDED(e->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&val))) && val);
    if (val) {
        BSTR b = nullptr;
        val->get_CurrentValue(&b);
        CHECK_STR(str(b), "line one\nline two is longer\nthree");
    }
    ComPtr<IUIAutomationTextPattern> text;
    CHECK(SUCCEEDED(e->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text);
    if (!text) return;
    ComPtr<IUIAutomationTextRange> r;
    text->get_DocumentRange(&r);
    r->MoveEndpointByRange(TextPatternRangeEndpoint_End, r.Get(), TextPatternRangeEndpoint_Start);  // collapse to the start
    r->ExpandToEnclosingUnit(TextUnit_Line);
    CHECK_STR(rangeText(r.Get()), "line one");
    int moved = 0;
    r->Move(TextUnit_Line, 1, &moved);
    CHECK(moved == 1);
    CHECK_STR(rangeText(r.Get()), "line two is longer");
    r->Move(TextUnit_Paragraph, 1, &moved);
    CHECK_STR(rangeText(r.Get()), "three");
    r->Move(TextUnit_Line, 1, &moved);
    CHECK(moved == 0);  // at the last one
    r->Move(TextUnit_Line, -2, &moved);
    CHECK(moved == -2);
    CHECK_STR(rangeText(r.Get()), "line one");
    // a range over two lines has a bounding rectangle per line
    ComPtr<IUIAutomationTextRange> two;
    text->get_DocumentRange(&two);
    SAFEARRAY* rects = nullptr;
    CHECK(SUCCEEDED(two->GetBoundingRectangles(&rects)) && rects);
    if (rects) {
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(rects, 1, &lo);
        SafeArrayGetUBound(rects, 1, &hi);
        CHECK((hi - lo + 1) % 4 == 0 && (hi - lo + 1) >= 12);  // four numbers a rectangle, three lines at least
        SafeArrayDestroy(rects);
    }
}

TEST(uia_exposes_the_scroll_bars_with_a_range_and_scrolls_through_them) {
    EditorWindow f(200, 100);
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.edit->setPlainText(EditorWindow::lines(60));
    f.edit->setReadOnly(true);
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto e = byId(a.Get(), win.Get(), "editor");
    CHECK(e != nullptr);
    if (!e) return;
    auto bar = byId(a.Get(), e.Get(), "vertical_scroll_bar");
    CHECK(bar != nullptr);
    if (!bar) return;
    CONTROLTYPEID ct = 0;
    bar->get_CurrentControlType(&ct);
    CHECK(ct == UIA_ScrollBarControlTypeId);
    CHECK(byId(a.Get(), e.Get(), "horizontal_scroll_bar") == nullptr);  // hidden: not in the tree
    ComPtr<IUIAutomationRangeValuePattern> rv;
    CHECK(SUCCEEDED(bar->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&rv))) && rv);
    if (!rv) return;
    double v = -1, mx = -1, large = -1;
    rv->get_CurrentValue(&v);
    rv->get_CurrentMaximum(&mx);
    rv->get_CurrentLargeChange(&large);
    CHECK(v == 0 && mx == f.edit->verticalScrollBar()->maximum() && large == f.edit->verticalScrollBar()->pageStep());
    CHECK(SUCCEEDED(rv->SetValue(40)));
    CHECK(f.edit->scrollY() == 40);
    CHECK(rv->SetValue(mx + 1) == E_INVALIDARG);  // refused, never clamped
    CHECK(f.edit->scrollY() == 40);
}

TEST(a_selectable_label_and_the_edits_have_names_a_screen_reader_can_say) {
    TextWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto status = byId(a.Get(), win.Get(), "status");
    CHECK(status != nullptr);
    if (status) {
        BSTR b = nullptr;
        status->get_CurrentName(&b);
        CHECK_STR(str(b), "Elapsed 12.5 s, 3 of 8 steps");
    }
    auto name = byId(a.Get(), win.Get(), "name");
    CHECK(name != nullptr);
    if (name) {
        BSTR b = nullptr;
        name->get_CurrentName(&b);
        CHECK_STR(str(b), "Name");  // from its form label, as in N3a
    }
}

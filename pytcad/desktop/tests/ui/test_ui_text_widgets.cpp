// Portable tests of N3d (NATIVE-DESKTOP-PLAN.md 27.8.5): the DoubleValidator, the EditModel's veto and the program's
// edit, the ScrollBar, the selectable Label, and the selection painting every text widget shares. The edits
// themselves (TSF, the clipboard, DirectWrite) are tested in test_ui_text_edits_win32.cpp. Part of tcad_ui_core_tests:
// no Win32. Every number is worked out by hand from the fake text engine: 6 DIPs a byte at the 12-DIP UI font, a
// 15-DIP line.
#include "mini_test.hpp"

#include "ui/core/clipboard.hpp"
#include "ui/core/edit_model.hpp"
#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/core/selection.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/scroll_bar.hpp"
#include "ui/widgets/validator.hpp"

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

namespace {

// A text engine that can hit-test and place a caret: every byte is 6 DIPs wide, a line 15 high.
class FakeText final : public TextEngine {
public:
    SizeF measure(std::string_view s, const TextStyle& st) override { return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size}; }
    TextHit hitTest(std::string_view s, const TextStyle& st, float, PointF p) override {
        const float w = 0.5f * st.size;
        const long k = std::clamp(std::lround(p.x / w), 0L, static_cast<long>(s.size()));
        return {static_cast<std::size_t>(k), p.x >= 0 && p.x < w * static_cast<float>(s.size())};
    }
    RectF caretRect(std::string_view, const TextStyle& st, float, std::size_t o) override {
        return {0.5f * st.size * static_cast<float>(o), 0, 0, 1.25f * st.size};
    }
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

class Host final : public UiHost {
public:
    FakeText text;
    FakeTimers clock;
    FakeClipboard board;
    Widget root;
    InputRouter router{root};
    Host() {
        root.setHost(this);
        root.setGeometry({0, 0, 400, 300});
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
    void widgetGone(Widget* w) override { router.widgetGone(w); }
};

constexpr unsigned kLeftBit = 1u << static_cast<unsigned>(MouseButton::Left);

MouseEvent mouse(MouseType t, float x, float y, unsigned held = 0, Mod mods = Mod::None) {
    MouseEvent e;
    e.type = t;
    e.button = t == MouseType::Move ? MouseButton::None : MouseButton::Left;
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

using S = Validator::State;
bool states(const DoubleValidator& v, std::string_view text, S want) {
    const S got = v.validate(text);
    if (got != want)
        std::printf("  validate [%.*s]: %d, wanted %d (0 invalid, 1 intermediate, 2 acceptable)\n", static_cast<int>(text.size()), text.data(),
                    static_cast<int>(got), static_cast<int>(want));
    return got == want;
}

}  // namespace

// -- the validator ----------------------------------------------------------------------------------------------

TEST(double_validator_accepts_complete_scientific_numbers) {
    const DoubleValidator v;
    for (const char* t : {"0", "1", "-2.5", "+3", ".5", "5.", "1e17", "1E17", "-1.5e-3", "1e+5", "007", "-0.0", "+.5e2"})
        CHECK(states(v, t, S::Acceptable));
}

TEST(double_validator_calls_an_unfinished_number_intermediate) {
    const DoubleValidator v;
    for (const char* t : {"", "-", "+", ".", "-.", "1e", "1E", "1e-", "1e+", "-.e"}) {
        const S want = std::string_view(t) == "-.e" ? S::Invalid : S::Intermediate;  // an exponent needs digits before it
        CHECK(states(v, t, want));
    }
}

TEST(double_validator_refuses_what_can_never_be_a_number) {
    const DoubleValidator v;
    for (const char* t : {"abc", "1a", "1e5e", "1.2.3", "--1", "1e5.5", "1 ", " 1", "1,5", "e5", "1e5x", "+-1", "1e--5", "inf", "nan", "0x10"})
        CHECK(states(v, t, S::Invalid));
}

TEST(double_validator_range_is_intermediate_outside_never_invalid) {
    const DoubleValidator v(0, 10);
    CHECK(states(v, "5", S::Acceptable));
    CHECK(states(v, "10", S::Acceptable));
    CHECK(states(v, "0", S::Acceptable));
    CHECK(states(v, "11", S::Intermediate));
    CHECK(states(v, "-1", S::Intermediate));
    CHECK(states(v, "1e3", S::Intermediate));  // scientific notation: more typing could bring it in range (Qt)
    CHECK(states(v, "1e-3", S::Acceptable));
    CHECK(states(v, "1e999", S::Intermediate));  // does not fit a double
}

TEST(double_validator_limits_the_decimals_of_the_mantissa) {
    const DoubleValidator v(-1e9, 1e9, 2);
    CHECK(states(v, "1.25", S::Acceptable));
    CHECK(states(v, "1.255", S::Invalid));
    CHECK(states(v, "1.25e5", S::Acceptable));
    CHECK(states(v, "1.", S::Acceptable));
    CHECK(states(v, "12345.", S::Acceptable));
}

// -- the edit model's veto and the program's edit ---------------------------------------------------------------

TEST(a_filter_refuses_edits_it_vetoes_and_nothing_changes) {
    EditModel m;
    DoubleValidator v;
    m.setFilter([&](const std::string& s) { return v.validate(s) != S::Invalid; });
    CHECK(m.insert("1"));
    CHECK(m.insert("e"));
    const int rev = m.revision();
    CHECK(!m.insert("x"));  // "1ex" can never be a number
    CHECK(m.text() == "1e" && m.revision() == rev && m.caret() == 2);
    CHECK(m.insert("5"));
    CHECK(m.text() == "1e5");
    CHECK(!m.replaceRange(0, 1, "z"));  // every route to an edit goes through replaceRange
    CHECK(m.backspace(false));  // removing "5" leaves "1e": intermediate, allowed
    CHECK(m.text() == "1e");
}

TEST(a_filter_judges_the_whole_result_of_a_paste) {
    EditModel m;
    DoubleValidator v;
    m.setFilter([&](const std::string& s) { return v.validate(s) != S::Invalid; });
    m.insert("12");
    CHECK(!m.insert("3x4"));  // a paste with one bad character is refused whole, not trimmed
    CHECK(m.text() == "12");
    CHECK(m.insert("34"));
    CHECK(m.text() == "1234");
    m.setSelection(1, 3);
    CHECK(m.insert("."));  // a selection replaced: the candidate is "1.4"
    CHECK(m.text() == "1.4");
}

TEST(a_filter_does_not_touch_the_programs_text_or_undo) {
    EditModel m;
    m.setFilter([](const std::string& s) { return s.find('x') == std::string::npos; });
    m.setText("has an x");  // QLineEdit::setText bypasses the validator
    CHECK(m.text() == "has an x");
    CHECK(!m.insert("x"));
    m.setFilter({});
    CHECK(m.insert("y"));
    m.setFilter([](const std::string& s) { return s.find('y') == std::string::npos; });  // now "y" text would be vetoed...
    CHECK(m.undo());                                                                     // ...but undo puts back accepted text
    CHECK(m.text() == "has an x");
    CHECK(m.redo());
    CHECK(m.text() == "has an xy");
}

TEST(force_replace_edits_a_read_only_model_without_undo_and_keeps_the_selection) {
    EditModel m({}, true);
    m.setText("one\ntwo\nthree");
    m.setReadOnly(true);
    m.setSelection(4, 7);  // "two"
    CHECK(!m.insert("x"));
    const int rev = m.revision();
    m.forceReplace(m.text().size(), m.text().size(), "\nfour");  // an append, after the selection
    CHECK(m.text() == "one\ntwo\nthree\nfour" && m.revision() == rev + 1);
    CHECK(m.anchor() == 4 && m.caret() == 7);
    m.forceReplace(0, 4, "");  // the first line is dropped: the selection moves with its text
    CHECK(m.text() == "two\nthree\nfour" && m.anchor() == 0 && m.caret() == 3);
    CHECK(!m.canUndo() && !m.canRedo());
    m.forceReplace(0, 14, "z");  // a range that holds the selection: it goes to the range's start
    CHECK(m.text() == "z" && m.caret() == 0);
    m.setText("a b");
    m.setReadOnly(false);
    m.insert("!");
    CHECK(m.canUndo());
    m.forceReplace(0, 0, ">");
    CHECK(!m.canUndo());  // the recorded steps pointed at text that moved
}

TEST(force_replace_in_a_single_line_model_turns_line_breaks_into_spaces) {
    EditModel m;
    m.forceReplace(0, 0, "a\nb");
    CHECK(m.text() == "a b");
}

TEST(line_start_and_end_find_the_hard_line) {
    EditModel m({}, true);
    m.setText("ab\n\ncde");
    CHECK(m.lineStart(0) == 0 && m.lineEnd(0) == 2);
    CHECK(m.lineStart(2) == 0 && m.lineEnd(2) == 2);  // at the line feed: still that line
    CHECK(m.lineStart(3) == 3 && m.lineEnd(3) == 3);  // the empty line
    CHECK(m.lineStart(5) == 4 && m.lineEnd(5) == 7);
    CHECK(m.lineStart(7) == 4 && m.lineEnd(7) == 7);
    CHECK(m.lineStart(99) == 4 && m.lineEnd(99) == 7);  // clamped
}

// -- the scroll bar ---------------------------------------------------------------------------------------------

namespace {

struct BarRig {
    Host h;
    ScrollBar* b;
    std::vector<int> log;
    explicit BarRig(Orientation o = Orientation::Vertical) {
        b = h.root.addChild<ScrollBar>(o);
        b->setGeometry(o == Orientation::Vertical ? RectI{100, 20, 14, 200} : RectI{20, 100, 200, 14});
        b->on_value_changed = [this](int v) { log.push_back(v); };
    }
    float P(float along) const { return b->orientation() == Orientation::Vertical ? 100 + 7 : 20 + along; }  // window x
    float Q(float along) const { return b->orientation() == Orientation::Vertical ? 20 + along : 100 + 7; }  // window y
    void press(float along) { h.router.mouse(mouse(MouseType::Down, P(along), Q(along), kLeftBit)); }
    void drag(float along) { h.router.mouse(mouse(MouseType::Move, P(along), Q(along), kLeftBit)); }
    void release(float along) { h.router.mouse(mouse(MouseType::Up, P(along), Q(along), 0)); }
};

}  // namespace

TEST(scroll_bar_hints_and_policy) {
    BarRig v, hz(Orientation::Horizontal);
    CHECK(v.b->sizeHint() == (SizeF{14, 72}) && hz.b->sizeHint() == (SizeF{72, 14}));
    CHECK(v.b->sizePolicy().horizontal == SizePolicy::Fixed && v.b->sizePolicy().vertical == SizePolicy::Expanding);
    CHECK(hz.b->sizePolicy().horizontal == SizePolicy::Expanding && hz.b->sizePolicy().vertical == SizePolicy::Fixed);
    CHECK(v.b->accessibleRole() == Role::ScrollBar);
}

TEST(scroll_bar_thumb_is_the_page_s_share_of_the_track) {
    BarRig r;
    CHECK(!r.b->isNeeded() && r.b->thumbRect() == (RectF{0, 0, 0, 0}));  // nothing to scroll
    r.b->setRange(0, 100);
    r.b->setPageStep(100);  // the page is half of the whole (range + page = 200): a 100-DIP thumb on 200
    CHECK(r.b->isNeeded());
    CHECK(r.b->thumbRect() == (RectF{0, 0, 14, 100}));
    r.b->setValue(50);
    CHECK(r.b->thumbRect() == (RectF{0, 50, 14, 100}));  // half of the 100 of travel
    r.b->setValue(100);
    CHECK(r.b->thumbRect() == (RectF{0, 100, 14, 100}) && r.b->atEnd());
    r.b->setRange(0, 10000);
    r.b->setPageStep(10);  // 200 * 10 / 10010 = 0.2: never shorter than 24
    CHECK(r.b->thumbRect().height == 24);
    BarRig hz(Orientation::Horizontal);
    hz.b->setRange(0, 300);
    hz.b->setPageStep(100);
    CHECK(hz.b->thumbRect() == (RectF{0, 0, 50, 14}));  // 200 * 100 / 400
    hz.b->setValue(300);
    CHECK(hz.b->thumbRect() == (RectF{150, 0, 50, 14}));
}

TEST(scroll_bar_value_is_clamped_and_reported_like_qts) {
    BarRig r;
    r.b->setRange(0, 50);
    r.b->setValue(20);
    r.b->setValue(20);  // no change: silent
    r.b->setValue(90);  // clamped
    CHECK((r.log == std::vector<int>{20, 50}));
    r.log.clear();
    r.b->setRange(0, 30);  // the value no longer fits: clamped and reported
    CHECK(r.b->value() == 30 && (r.log == std::vector<int>{30}));
    r.log.clear();
    r.b->setValueSilent(5);
    CHECK(r.b->value() == 5 && r.log.empty());
    r.b->setRange(7, 3);  // maximum below minimum becomes the minimum
    CHECK(r.b->minimum() == 7 && r.b->maximum() == 7 && r.b->value() == 7);
}

TEST(scroll_bar_thumb_drag_keeps_the_grab_offset) {
    BarRig r;
    r.b->setRange(0, 100);
    r.b->setPageStep(100);  // thumb 0..100, travel 100
    r.press(30);            // 30 into the thumb
    CHECK(r.b->isSliderDown() && r.b->value() == 0 && r.log.empty());  // a press on the thumb moves nothing
    r.drag(80);  // the thumb's start is now at 50
    CHECK(r.b->value() == 50);
    r.drag(500);  // far past the end, still held
    CHECK(r.b->value() == 100);
    r.drag(-300);
    CHECK(r.b->value() == 0);
    r.release(-300);
    CHECK(!r.b->isSliderDown());
    r.drag(60);  // released: the pointer does nothing
    CHECK(r.b->value() == 0);
}

TEST(scroll_bar_track_press_pages_toward_the_pointer_and_repeats) {
    BarRig r;
    r.b->setRange(0, 400);
    r.b->setPageStep(100);  // thumb 40 long, travel 160
    r.press(190);           // on the track, beyond the thumb
    CHECK(r.b->value() == 100 && !r.b->isSliderDown());
    r.h.clock.advance(499);
    CHECK(r.b->value() == 100);  // not yet
    r.h.clock.advance(2);        // 500 ms: the first repeat
    CHECK(r.b->value() == 200);
    r.h.clock.advance(75);
    CHECK(r.b->value() == 300);
    r.h.clock.advance(75);
    CHECK(r.b->value() == 400);  // the thumb (160..200) now holds the pointer at 190
    r.h.clock.advance(300);
    CHECK(r.b->value() == 400 && r.h.clock.live.empty());  // the repeat stopped by itself
    r.release(190);
    r.b->setValue(400);
    r.press(2);  // above the thumb: pages up
    CHECK(r.b->value() == 300);
    r.release(2);
    r.h.clock.advance(2000);
    CHECK(r.b->value() == 300 && r.h.clock.live.empty());  // a release stops the repeat
}

TEST(scroll_bar_track_repeat_follows_the_pointer_and_stops_at_the_thumb) {
    BarRig r;
    r.b->setRange(0, 1000);
    r.b->setPageStep(100);
    r.press(190);
    CHECK(r.b->value() == 100);
    r.h.router.mouse(mouse(MouseType::Move, r.P(190), r.Q(5), kLeftBit));  // the pointer moves to the top while held
    r.h.clock.advance(500);
    CHECK(r.b->value() == 0);  // paged up now, toward the pointer
    r.release(5);
}

TEST(scroll_bar_wheel_scrolls_three_single_steps_and_needs_no_focus) {
    BarRig r;
    r.b->setRange(0, 100);
    r.b->setPageStep(20);
    r.b->setSingleStep(4);
    r.b->setValue(50);
    r.log.clear();
    r.h.router.mouse(wheel(r.P(10), r.Q(10), -1));  // toward the user: down
    CHECK(r.b->value() == 62);
    r.h.router.mouse(wheel(r.P(10), r.Q(10), 2));  // away: up
    CHECK(r.b->value() == 38);
    CHECK((r.log == std::vector<int>{62, 38}));
}

TEST(scroll_bar_disabled_and_unneeded_do_not_scroll) {
    BarRig r;
    r.press(100);  // nothing to scroll: the press is taken, nothing moves
    r.release(100);
    CHECK(r.b->value() == 0 && r.log.empty());
    r.b->setRange(0, 100);
    r.b->setPageStep(10);
    r.b->setEnabled(false);
    r.press(190);
    r.release(190);
    r.h.router.mouse(wheel(r.P(10), r.Q(10), -1));
    CHECK(r.b->value() == 0 && r.log.empty());
}

TEST(scroll_bar_reports_a_range_to_ui_automation_and_refuses_bad_sets) {
    BarRig r;
    r.b->setRange(0, 80);
    r.b->setPageStep(25);
    r.b->setSingleStep(2);
    r.b->setValue(30);
    const AccessibleRange a = r.b->accessibleRange();
    CHECK(a.valid && a.value == 30 && a.minimum == 0 && a.maximum == 80 && a.small_step == 2 && a.large_step == 25);
    CHECK(r.b->accessibleSetRangeValue(40) && r.b->value() == 40);
    CHECK(!r.b->accessibleSetRangeValue(81) && !r.b->accessibleSetRangeValue(-1) && !r.b->accessibleSetRangeValue(2.5));
    CHECK(r.b->value() == 40);  // refused, never clamped
    int notified = 0;
    r.b->accessible_range_changed = [&] { ++notified; };
    r.b->setValue(41);
    r.b->setValueSilent(42);
    CHECK(notified == 2);  // the screen reader hears silent changes too
}

TEST(scroll_bar_paints_a_track_and_a_thumb_only_when_needed) {
    BarRig r;
    RecordingPainter none;
    r.b->paint(none);
    int fills = 0, rounded = 0;
    for (const auto& op : none.ops()) fills += op.kind == "fillRect", rounded += op.kind == "fillRoundedRect";
    CHECK(fills == 1 && rounded == 0);
    r.b->setRange(0, 100);
    r.b->setPageStep(100);
    RecordingPainter p;
    r.b->paint(p);
    const PaintOp* thumb = nullptr;
    for (const auto& op : p.ops())
        if (op.kind == "fillRoundedRect") thumb = &op;
    CHECK(thumb != nullptr);
    if (thumb) CHECK(thumb->rect == (RectF{2, 2, 10, 96}));  // inset 2 inside the track: 14 - 4 wide, 100 - 4 long
}

// -- the selectable label ---------------------------------------------------------------------------------------

namespace {

struct LabelRig {
    Host h;
    Label* l;
    LabelRig() {
        l = h.root.addChild<Label>("hello world");
        l->setGeometry({10, 10, 120, 15});
    }
    void press(float x, bool dbl = false, Mod mods = Mod::None) {
        MouseEvent e = mouse(dbl ? MouseType::DoubleClick : MouseType::Down, 10 + x, 17, kLeftBit, mods);
        h.router.mouse(e);
    }
    void drag(float x) { h.router.mouse(mouse(MouseType::Move, 10 + x, 17, kLeftBit)); }
    void release(float x) { h.router.mouse(mouse(MouseType::Up, 10 + x, 17, 0)); }
};

}  // namespace

TEST(a_label_is_not_selectable_unless_asked) {
    LabelRig r;
    CHECK(!r.l->selectable());
    r.press(10);
    r.release(10);
    CHECK(r.h.router.focusWidget() == nullptr);
    CHECK(r.l->selectedText().empty() && !r.l->copy());
    CHECK(r.l->focusPolicy() == FocusPolicy::None && r.l->cursor() == Cursor::Inherit);
}

TEST(a_selectable_label_selects_with_the_mouse_and_takes_focus_on_a_click_only) {
    LabelRig r;
    r.l->setSelectable(true);
    CHECK(r.l->focusPolicy() == FocusPolicy::Click && r.l->cursor() == Cursor::IBeam);
    CHECK(!r.l->acceptsFocus(FocusReason::Tab) && r.l->acceptsFocus(FocusReason::Mouse));  // Qt's flag is the mouse's
    r.press(6);   // between "h" and "e"
    r.drag(36);   // 5 characters on
    CHECK(r.h.router.focusWidget() == r.l);
    CHECK(r.l->selectedText() == "ello ");
    r.release(36);
    CHECK((r.l->selection() == std::pair<std::size_t, std::size_t>{1, 6}));
    r.h.router.mouse(mouse(MouseType::Move, 10 + 100, 17, 0));  // released: moving selects nothing
    CHECK(r.l->selectedText() == "ello ");
    r.press(60);  // a new press starts over
    r.release(60);
    CHECK(r.l->selectedText().empty());
}

TEST(a_selectable_label_double_click_selects_a_word_and_shift_click_extends) {
    LabelRig r;
    r.l->setSelectable(true);
    r.press(48, true);  // inside "world" (bytes 6..11 at 36..66)
    CHECK(r.l->selectedText() == "world");
    r.release(48);
    r.press(8, true);
    CHECK(r.l->selectedText() == "hello");
    r.release(8);
    r.press(18);  // the caret after "hel"
    r.release(18);
    r.press(48, false, Mod::Shift);
    CHECK(r.l->selectedText() == "lo wo");  // the caret was after "hel" (3); the click is at 48 / 6 = 8
    r.release(48);
}

TEST(a_selectable_label_copies_through_the_hosts_clipboard) {
    LabelRig r;
    r.l->setSelectable(true);
    r.press(0);
    r.drag(30);
    r.release(30);
    CHECK(r.h.router.key(key('C', Mod::Ctrl)));
    CHECK(r.h.board.value == std::optional<std::string>("hello"));
    r.h.board.value.reset();
    CHECK(r.h.router.key(key(tcad::ui::keys::Insert, Mod::Ctrl)));
    CHECK(r.h.board.value == std::optional<std::string>("hello"));
    CHECK(r.h.router.key(key('A', Mod::Ctrl)));
    CHECK(r.l->selectedText() == "hello world");
    CHECK(r.l->copy() && r.h.board.value == std::optional<std::string>("hello world"));
    r.l->setSelection(3, 3);
    r.h.board.value.reset();
    CHECK(!r.l->copy() && !r.h.board.value);  // nothing selected: the clipboard is left alone
    // the chord belongs to the label ahead of window shortcuts only while it is focused and selectable
    LabelRig plain;
    CHECK(!plain.l->overridesShortcut(key('C', Mod::Ctrl)));
    CHECK(r.l->overridesShortcut(key('C', Mod::Ctrl)) && !r.l->overridesShortcut(key('V', Mod::Ctrl)));
}

TEST(a_selectable_label_drops_the_selection_when_its_text_changes) {
    LabelRig r;
    r.l->setSelectable(true);
    r.l->selectAll();
    CHECK(r.l->selectedText() == "hello world");
    r.l->setText("other");
    CHECK(r.l->selectedText().empty());
    r.l->selectAll();
    r.l->setSelectable(false);
    CHECK(!r.l->selectable() && r.l->selectedText().empty() && r.l->focusPolicy() == FocusPolicy::None);
}

TEST(a_selectable_label_ignores_the_mouse_when_disabled) {
    LabelRig r;
    r.l->setSelectable(true);
    r.l->setEnabled(false);
    r.press(6);
    r.drag(30);
    r.release(30);
    CHECK(r.l->selectedText().empty());
}

TEST(a_selectable_label_paints_its_selection_under_the_text) {
    LabelRig r;
    r.l->setSelectable(true);
    r.press(6);
    r.drag(36);
    r.release(36);  // focused by the click; selection [1, 6) = x 6..36
    RecordingPainter p;
    p.translate(10, 10);
    r.l->paint(p);
    int sel_at = -1, text_at = -1, i = 0;
    for (const auto& op : p.ops()) {
        if (op.kind == "fillRect" && op.rect == (RectF{16, 10, 30, 15})) sel_at = i;
        if (op.kind == "text") text_at = i;
        ++i;
    }
    CHECK(sel_at >= 0 && text_at > sel_at);  // filled first, the text on top
    if (sel_at >= 0) CHECK(p.ops()[static_cast<std::size_t>(sel_at)].color.r == token(T::Selection).r);
}

// -- the shared selection painting ------------------------------------------------------------------------------

namespace {

struct HcGuard {
    HighContrast saved = highContrast();
    explicit HcGuard(bool on) {
        highContrast() = {on, Color::rgb(0x000000), Color::rgb(0xFFFFFF), Color::rgb(0x1AEBFF), Color::rgb(0x000000), Color::rgb(0x3FF23F)};
    }
    ~HcGuard() { highContrast() = saved; }
};

}  // namespace

TEST(an_active_selection_is_the_selection_fill_and_an_inactive_one_the_alternate_base) {
    const std::vector<RectF> rects{{10, 5, 30, 15}, {0, 20, 12, 15}};
    RecordingPainter a, i;
    fillSelection(a, rects, true);
    fillSelection(i, rects, false);
    CHECK(a.ops().size() == 2 && i.ops().size() == 2);
    CHECK(a.ops()[0].kind == "fillRect" && a.ops()[0].rect == rects[0] && a.ops()[0].color.b == token(T::Selection).b);
    CHECK(i.ops()[1].kind == "fillRect" && i.ops()[1].rect == rects[1] && i.ops()[1].color.b == token(T::AlternateBase).b);
    int redraws = 0;
    redrawSelectedText(a, rects, true, [&](Color) { ++redraws; });
    CHECK(redraws == 0);  // outside high contrast the selected text keeps its colour
}

TEST(in_high_contrast_an_inactive_selection_is_an_outline_and_an_active_one_redraws_its_text) {
    HcGuard hc(true);
    const std::vector<RectF> rects{{10, 5, 30, 15}, {0, 20, 12, 15}};
    RecordingPainter i;
    fillSelection(i, rects, false);
    CHECK(i.ops().size() == 2 && i.ops()[0].kind == "strokeRect" && i.ops()[1].kind == "strokeRect");  // a fill would vanish
    CHECK(i.ops()[0].color.r == 1.0f);  // the window-text colour
    RecordingPainter a;
    fillSelection(a, rects, true);
    CHECK(a.ops()[0].kind == "fillRect" && a.ops()[0].color.g == Color::rgb(0x1AEBFF).g);  // the system highlight
    std::vector<float> reds;
    RecordingPainter r;
    redrawSelectedText(r, rects, true, [&](Color c) { reds.push_back(c.r); });
    CHECK(reds.size() == 2 && reds[0] == 0.0f);  // each rectangle, in the highlight-text colour (black here)
    reds.clear();
    redrawSelectedText(r, rects, false, [&](Color c) { reds.push_back(c.r); });
    CHECK(reds.empty());  // an inactive selection is only an outline
}

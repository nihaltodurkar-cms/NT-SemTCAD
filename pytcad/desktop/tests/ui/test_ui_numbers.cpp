// Portable tests of N3b (NATIVE-DESKTOP-PLAN.md 27.8.3): the number rules every number field shares -- parsing
// scientific and engineering input with a unit, displaying a value, stepping it -- and the Slider's behaviour (handle
// geometry, press/drag/jump, keys, wheel, range, UI Automation range). The spin boxes themselves sit on a LineEdit
// (Win32) and are tested in test_ui_numbers_win32.cpp. Part of tcad_ui_core_tests: no Win32. Every expected number
// is worked out by hand.
#include "mini_test.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/widgets/numeric.hpp"
#include "ui/widgets/slider.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace tcad::ui;
using tcad::platform::KeyEvent;
using tcad::platform::Mod;
using tcad::platform::MouseButton;
using tcad::platform::MouseEvent;
using tcad::platform::MouseType;

namespace {

// Both the value and the status, for one line per case.
bool parsesTo(std::string_view text, double want, std::string_view unit = {}) {
    const Parsed p = parseQuantity(text, unit);
    const bool ok = p.status == ParseStatus::Ok && std::fabs(p.value - want) <= 1e-12 * std::max(1.0, std::fabs(want));
    if (!ok) std::printf("  parse [%.*s] unit [%.*s]: status %d value %.17g, wanted %.17g\n", static_cast<int>(text.size()), text.data(),
                         static_cast<int>(unit.size()), unit.data(), static_cast<int>(p.status), p.value, want);
    return ok;
}
bool parseStatusIs(std::string_view text, ParseStatus s, std::string_view unit = {}) {
    const Parsed p = parseQuantity(text, unit);
    if (p.status != s) std::printf("  parse [%.*s]: status %d, wanted %d\n", static_cast<int>(text.size()), text.data(), static_cast<int>(p.status), static_cast<int>(s));
    return p.status == s;
}
bool formatsTo(double v, int decimals, std::string_view unit, std::string_view want, Notation n = Notation::Auto) {
    const std::string got = formatQuantity(v, decimals, unit, n);
    if (got != want) std::printf("  format %.17g (%d): got [%s] wanted [%.*s]\n", v, decimals, got.c_str(), static_cast<int>(want.size()), want.data());
    return got == want;
}

}  // namespace

// -- parsing ----------------------------------------------------------------------------------------------------

TEST(parse_plain_and_scientific_numbers) {
    CHECK(parsesTo("0", 0));
    CHECK(parsesTo("12", 12));
    CHECK(parsesTo("-2.5", -2.5));
    CHECK(parsesTo("+4", 4));
    CHECK(parsesTo(".5", 0.5));
    CHECK(parsesTo("3.", 3));
    CHECK(parsesTo("1e17", 1e17));
    CHECK(parsesTo("1E17", 1e17));
    CHECK(parsesTo("-2.5E-3", -2.5e-3));
    CHECK(parsesTo("1.5e+3", 1500));
    CHECK(parsesTo("  7  ", 7));  // surrounding blanks
}

TEST(parse_si_prefixes) {
    CHECK(parsesTo("5k", 5000));
    CHECK(parsesTo("2.5 u", 2.5e-6));
    CHECK(parsesTo("3 f", 3e-15));
    CHECK(parsesTo("3p", 3e-12));
    CHECK(parsesTo("3n", 3e-9));
    CHECK(parsesTo("3m", 3e-3));
    CHECK(parsesTo("3M", 3e6));  // m and M are different
    CHECK(parsesTo("7G", 7e9));
    CHECK(parsesTo("1.5T", 1.5e12));
    CHECK(parsesTo("4\xC2\xB5", 4e-6));  // U+00B5 micro sign
    CHECK(parsesTo("4\xCE\xBC", 4e-6));  // U+03BC Greek mu
    CHECK(parsesTo("0.1k", 100));        // tidied: not 100.00000000000001
}

TEST(parse_with_a_unit) {
    CHECK(parsesTo("5 V", 5, "V"));
    CHECK(parsesTo("5V", 5, "V"));
    CHECK(parsesTo("5", 5, "V"));  // the unit may be left out
    CHECK(parsesTo("5 kV", 5000, "V"));
    CHECK(parsesTo("5k", 5000, "V"));  // and a prefix without it
    CHECK(parsesTo("2.5 nm", 2.5e-9, "m"));
    CHECK(parsesTo("2 nm", 2, " nm"));  // a unit's own leading blank is not needed in the text
    CHECK(parsesTo("1e-3 V", 1e-3, "V"));
    CHECK(parsesTo("300 K", 300, "K"));
}

TEST(parse_a_text_that_is_exactly_the_unit_is_the_unit_not_a_prefix) {
    CHECK(parsesTo("5 m", 5, "m"));   // five metres, not five milli-
    CHECK(parsesTo("5m", 5, "m"));
    CHECK(parsesTo("5 mm", 5e-3, "m"));
    CHECK(parsesTo("5 m", 5e-3, ""));  // in a unitless field m is milli
    CHECK(parsesTo("5 mV", 5, "mV"));  // a field in millivolts: the unit as written
}

TEST(parse_refuses_what_is_not_a_quantity) {
    CHECK(parseStatusIs("abc", ParseStatus::Invalid));
    CHECK(parseStatusIs("1e3k", ParseStatus::Invalid));  // an exponent and a prefix
    CHECK(parseStatusIs("5 x", ParseStatus::Invalid));
    CHECK(parseStatusIs("1.2.3", ParseStatus::Invalid));
    CHECK(parseStatusIs("--1", ParseStatus::Invalid));
    CHECK(parseStatusIs("1 2", ParseStatus::Invalid));
    CHECK(parseStatusIs("inf", ParseStatus::Invalid));
    CHECK(parseStatusIs("nan", ParseStatus::Invalid));
    CHECK(parseStatusIs("5 kW", ParseStatus::Invalid, "V"));  // the wrong unit
    CHECK(parseStatusIs("1e999", ParseStatus::Invalid));      // does not fit a double
    CHECK(parseStatusIs("1e3e3", ParseStatus::Invalid));
    CHECK(parseStatusIs("e5", ParseStatus::Invalid));
    CHECK(parseStatusIs("m", ParseStatus::Invalid));  // a prefix with no number
}

TEST(parse_tells_unfinished_and_empty_from_wrong) {
    CHECK(parseStatusIs("", ParseStatus::Empty));
    CHECK(parseStatusIs("   ", ParseStatus::Empty));
    CHECK(parseStatusIs("V", ParseStatus::Empty, "V"));  // only the unit
    CHECK(parseStatusIs("-", ParseStatus::Incomplete));
    CHECK(parseStatusIs("+", ParseStatus::Incomplete));
    CHECK(parseStatusIs(".", ParseStatus::Incomplete));
    CHECK(parseStatusIs("-.", ParseStatus::Incomplete));
    CHECK(parseStatusIs("1e", ParseStatus::Incomplete));
    CHECK(parseStatusIs("1e-", ParseStatus::Incomplete));
    CHECK(parseStatusIs("1e+", ParseStatus::Incomplete));
}

// -- display ----------------------------------------------------------------------------------------------------

TEST(format_is_qts_text_while_that_shows_the_value_exactly) {
    CHECK(formatsTo(3.3, 2, "", "3.30"));
    CHECK(formatsTo(1.5, 2, " V", "1.50 V"));
    CHECK(formatsTo(0, 2, "", "0.00"));
    CHECK(formatsTo(1234.5, 1, "", "1234.5"));
    CHECK(formatsTo(-0.25, 2, "", "-0.25"));
    CHECK(formatsTo(123456789, 0, "", "123456789"));
    CHECK(formatsTo(1e-9, 9, "", "0.000000001"));
    CHECK(formatsTo(0.5, 9, "", "0.500000000"));
}

TEST(format_goes_scientific_rather_than_show_a_value_as_something_else) {
    CHECK(formatsTo(1e17, 0, "", "1e17"));
    CHECK(formatsTo(1e14, 0, "", "1e14"));
    CHECK(formatsTo(1e9, 0, "", "1e9"));  // fixed stops below 1e9
    CHECK(formatsTo(-1e21, 0, "", "-1e21"));
    CHECK(formatsTo(2.5e-10, 9, "", "2.5e-10"));  // 0.000000000 would read as zero
    CHECK(formatsTo(-0.001, 2, "", "-0.001"));  // more places, between 1e-3 and 1e9
    CHECK(formatsTo(1234.56789, 2, "", "1234.56789"));
    CHECK(formatsTo(9.9e-4, 2, "", "9.9e-4"));  // below 1e-3: scientific
    CHECK(formatsTo(1.5e-7, 2, " V", "1.5e-7 V"));
    CHECK(formatsTo(1.23456789e17, 0, "", "1.23456789e17"));
    CHECK(formatsTo(1.5, 0, "", "1.5"));  // not "2": a value is never shown as something else
}

TEST(format_fixed_notation_is_always_decimals_places) {
    CHECK(formatsTo(2.5e-10, 2, "", "0.00", Notation::Fixed));
    CHECK(formatsTo(1e17, 0, "", "100000000000000000", Notation::Fixed));
    CHECK(formatsTo(-0.0, 2, "", "0.00", Notation::Fixed));  // never "-0.00"
    CHECK(formatsTo(-0.001, 2, "", "0.00", Notation::Fixed));
    CHECK(formatsTo(7, 0, "", "7", Notation::Fixed));
}

TEST(what_format_shows_parse_reads_back) {
    const double values[] = {0, 1, -1, 3.3, 0.5, 1e14, 1e17, -1e21, 2.5e-10, 1.23456789e17, 4.2e-19, 123456.789, 1e-9};
    for (double v : values) {
        for (int decimals : {0, 2, 9}) {
            const std::string t = formatQuantity(v, decimals, " V");
            const Parsed p = parseQuantity(t, "V");
            const bool ok = p.status == ParseStatus::Ok && std::fabs(p.value - v) <= 1e-9 * std::fabs(v);
            if (!ok) std::printf("  %.17g (%d) -> [%s] -> %.17g\n", v, decimals, t.c_str(), p.value);
            CHECK(ok);
        }
    }
}

// -- stepping ---------------------------------------------------------------------------------------------------

TEST(linear_steps_stay_readable) {
    CHECK(stepLinear(0.1, 2, 0.1) == 0.3);  // not 0.30000000000000004
    CHECK(stepLinear(1.0, -3, 0.25) == 0.25);
    CHECK(stepLinear(0, 1, 1e14) == 1e14);
    CHECK(tidy(0.1 + 0.2) == 0.3);
}

TEST(decade_steps_multiply_and_divide_by_ten) {
    CHECK(stepDecade(1e14, 1, 1e14) == 1e15);
    CHECK(stepDecade(1e15, -1, 1e14) == 1e14);
    CHECK(stepDecade(5, 1, 1) == 50);
    CHECK(stepDecade(1, 3, 1) == 1000);
    CHECK(stepDecade(3e14, 1, 1e14) == 3e15);  // multiplicative, not snapped to a decade
    CHECK(stepDecade(1e-3, -2, 1e-6) == 1e-5);
}

TEST(decade_steps_pass_through_zero_and_negatives) {
    CHECK(stepDecade(0, 1, 1e14) == 1e14);
    CHECK(stepDecade(0, -1, 1e14) == -1e14);
    CHECK(stepDecade(1e14, -1, 1e14) == 0);    // below the seed: zero
    CHECK(stepDecade(-1e14, 1, 1e14) == 0);    // a negative value moves toward zero going up
    CHECK(stepDecade(-1e15, 1, 1e14) == -1e14);
    CHECK(stepDecade(-1e14, -1, 1e14) == -1e15);
    CHECK(stepDecade(0.3, -1, 0.1) == 0);
}

// -- Slider -----------------------------------------------------------------------------------------------------

namespace {

class NoText final : public TextEngine {
public:
    SizeF measure(std::string_view s, const TextStyle& st) override { return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size}; }
};

class Host final : public UiHost {
public:
    NoText text;
    Widget root;
    InputRouter router{root};
    Host() {
        root.setHost(this);
        root.setGeometry({0, 0, 300, 100});
    }
    ~Host() override { root.setHost(nullptr); }
    void invalidate(const RectI&) override {}
    void scheduleLayout() override {}
    TextEngine& textEngine() override { return text; }
    double scale() const override { return 1.0; }
    InputRouter* input() override { return &router; }
};

constexpr unsigned kLeftBit = 1u << static_cast<unsigned>(MouseButton::Left);

MouseEvent mouse(MouseType t, float x, float y, unsigned held = 0) {
    MouseEvent e;
    e.type = t;
    e.button = t == MouseType::Move ? MouseButton::None : MouseButton::Left;
    e.x = x;
    e.y = y;
    e.buttons_down = held;
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

// A 0..100 slider, 116 DIPs wide: the handle (16 wide) travels 100, so value v has its handle at x = v.
struct Rig {
    Host h;
    Slider* s;
    std::vector<int> log;
    Rig() {
        s = h.root.addChild<Slider>();
        s->setRange(0, 100);
        s->setGeometry({10, 10, 116, 20});
        s->on_value_changed = [this](int v) { log.push_back(v); };
    }
    // window x of widget x
    static float X(float x) { return 10 + x; }
};

}  // namespace

TEST(slider_hints_and_policy_are_qts) {
    Host h;
    auto* s = h.root.addChild<Slider>();
    CHECK(s->sizeHint() == (SizeF{84, 20}));
    CHECK(s->minimumSizeHint() == (SizeF{32, 20}));
    CHECK(s->sizePolicy().horizontal == SizePolicy::Expanding && s->sizePolicy().vertical == SizePolicy::Fixed);
    CHECK(s->minimum() == 0 && s->maximum() == 99 && s->value() == 0 && s->singleStep() == 1 && s->pageStep() == 10);
    CHECK(s->accessibleRole() == Role::Slider);
}

TEST(slider_value_is_clamped_and_every_change_is_reported) {
    Rig r;
    r.s->setValue(40);
    r.s->setValue(40);   // no change, no signal
    r.s->setValue(250);  // clamped
    r.s->setValue(-5);
    CHECK((r.log == std::vector<int>{40, 100, 0}));
    r.s->setValue(60);
    r.log.clear();
    r.s->setRange(0, 50);  // the value no longer fits: clamped, and that is reported
    CHECK(r.s->value() == 50);
    CHECK((r.log == std::vector<int>{50}));
    r.s->setRange(9, 3);  // maximum below minimum becomes the minimum (Qt)
    CHECK(r.s->minimum() == 9 && r.s->maximum() == 9 && r.s->value() == 9);
}

TEST(slider_silent_set_does_not_report) {
    Rig r;
    r.s->setValueSilent(30);
    CHECK(r.s->value() == 30);
    CHECK(r.log.empty());
    r.s->setValueSilent(500);
    CHECK(r.s->value() == 100);
    CHECK(r.log.empty());
}

TEST(slider_handle_follows_the_value) {
    Rig r;
    CHECK(r.s->handleRect() == (RectF{0, 2, 16, 16}));
    r.s->setValue(50);
    CHECK(r.s->handleRect() == (RectF{50, 2, 16, 16}));
    r.s->setValue(100);
    CHECK(r.s->handleRect() == (RectF{100, 2, 16, 16}));
    r.s->setRange(5, 5);  // no range: the handle stays at the start
    CHECK(r.s->handleRect().x == 0);
}

TEST(slider_value_at_x_rounds_and_clamps) {
    Rig r;
    CHECK(r.s->valueAtX(58) == 50);   // a handle centred at 58 is at x = 50
    CHECK(r.s->valueAtX(8) == 0);
    CHECK(r.s->valueAtX(-30) == 0);
    CHECK(r.s->valueAtX(900) == 100);
    CHECK(r.s->valueAtX(8.4f) == 0);
    CHECK(r.s->valueAtX(8.6f) == 1);
    r.s->setRange(0, 4);  // 4 values over 100 DIPs
    CHECK(r.s->valueAtX(8 + 37) == 1);  // 37/100 of 4 = 1.48
    CHECK(r.s->valueAtX(8 + 38) == 2);  // 1.52
}

TEST(slider_press_on_the_handle_grabs_it_without_moving_it) {
    Rig r;
    r.s->setValue(50);
    r.log.clear();
    // the handle spans 50..66, its centre 58; press 4 right of the centre
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(62), 20, kLeftBit));
    CHECK(r.s->isSliderDown());
    CHECK(r.s->value() == 50);
    CHECK(r.log.empty());
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(82), 20, kLeftBit));  // 20 right: the handle too
    CHECK(r.s->value() == 70);
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(250), 20, kLeftBit));  // off the widget, still held
    CHECK(r.s->value() == 100);
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(250), 20, 0));
    CHECK(!r.s->isSliderDown());
    CHECK((r.log == std::vector<int>{70, 100}));
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(30), 20, 0));  // not held: nothing
    CHECK(r.s->value() == 100);
}

TEST(slider_press_on_the_groove_jumps_there_and_keeps_dragging) {
    Rig r;
    r.s->setValue(50);
    r.log.clear();
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(33), 20, kLeftBit));  // centre 33 -> x = 25
    CHECK(r.s->value() == 25);
    CHECK(r.s->isSliderDown());
    r.h.router.mouse(mouse(MouseType::Move, Rig::X(43), 20, kLeftBit));
    CHECK(r.s->value() == 35);
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(43), 20, 0));
    CHECK((r.log == std::vector<int>{25, 35}));
}

TEST(slider_disabled_ignores_the_mouse_keys_and_wheel) {
    Rig r;
    r.s->setValue(20);
    r.s->setEnabled(false);
    r.log.clear();
    r.h.router.mouse(mouse(MouseType::Down, Rig::X(80), 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, Rig::X(80), 20, 0));
    CHECK(r.s->value() == 20);
    CHECK(!r.s->keyEvent(key(tcad::ui::keys::Right)));
    CHECK(r.s->value() == 20);
    CHECK(r.log.empty());
}

TEST(slider_keys_step_page_and_jump) {
    Rig r;
    r.s->setValue(50);
    r.s->setFocus(FocusReason::Tab);
    r.log.clear();
    r.h.router.key(key(tcad::ui::keys::Right));
    r.h.router.key(key(tcad::ui::keys::Up));
    CHECK(r.s->value() == 52);
    r.h.router.key(key(tcad::ui::keys::Left));
    r.h.router.key(key(tcad::ui::keys::Down));
    CHECK(r.s->value() == 50);
    r.h.router.key(key(tcad::ui::keys::PageUp));
    CHECK(r.s->value() == 60);
    r.h.router.key(key(tcad::ui::keys::PageDown));
    r.h.router.key(key(tcad::ui::keys::PageDown));
    CHECK(r.s->value() == 40);
    r.h.router.key(key(tcad::ui::keys::Home));
    CHECK(r.s->value() == 0);
    r.h.router.key(key(tcad::ui::keys::Left));  // at the end of the range: nothing
    CHECK(r.s->value() == 0);
    r.h.router.key(key(tcad::ui::keys::End));
    CHECK(r.s->value() == 100);
    r.s->setSingleStep(5);
    r.h.router.key(key(tcad::ui::keys::Left));
    CHECK(r.s->value() == 95);
    r.log.clear();
    r.h.router.key(key(tcad::ui::keys::Left, Mod::Ctrl));  // a shortcut chord is not the slider's
    CHECK(r.s->value() == 95);
    CHECK(r.log.empty());
}

TEST(slider_wheel_steps_three_at_most_a_page_and_only_with_the_focus) {
    Rig r;
    r.s->setValue(50);
    r.h.router.mouse(wheel(Rig::X(50), 20, 1));  // over it, but not focused: a wheel over a form passes by
    CHECK(r.s->value() == 50);
    r.s->setFocus(FocusReason::Tab);
    r.h.router.mouse(wheel(Rig::X(50), 20, 1));
    CHECK(r.s->value() == 53);  // 3 single steps
    r.h.router.mouse(wheel(Rig::X(50), 20, -2));
    CHECK(r.s->value() == 47);
    r.s->setSingleStep(5);  // 15 would exceed the page of 10
    r.h.router.mouse(wheel(Rig::X(50), 20, 1));
    CHECK(r.s->value() == 57);
}

TEST(slider_reports_a_range_to_ui_automation) {
    Rig r;
    r.s->setValue(30);
    r.s->setSingleStep(2);
    const AccessibleRange a = r.s->accessibleRange();
    CHECK(a.valid && a.value == 30 && a.minimum == 0 && a.maximum == 100 && a.small_step == 2 && a.large_step == 10 && !a.read_only);
    r.log.clear();
    CHECK(r.s->accessibleSetRangeValue(64));
    CHECK(r.s->value() == 64);
    CHECK(!r.s->accessibleSetRangeValue(101));  // refused, never clamped
    CHECK(!r.s->accessibleSetRangeValue(-1));
    CHECK(!r.s->accessibleSetRangeValue(7.5));  // an integer slider
    CHECK(!r.s->accessibleSetRangeValue(std::nan("")));
    CHECK(r.s->value() == 64);
    CHECK((r.log == std::vector<int>{64}));
}

TEST(slider_tells_the_ui_automation_host_when_its_range_value_changes) {
    Rig r;
    int events = 0;
    r.s->accessible_range_changed = [&] { ++events; };
    r.s->setValue(10);
    r.s->setValue(10);  // unchanged
    r.s->setValueSilent(20);  // silent for the program, not for a screen reader
    r.s->setFocus(FocusReason::Tab);
    r.h.router.key(key(tcad::ui::keys::Right));
    CHECK(events == 3);
}

TEST(slider_paints_a_groove_and_a_handle) {
    Rig r;
    RecordingPainter p;
    r.s->paint(p);
    int rounded = 0;
    for (const auto& op : p.ops()) rounded += op.kind == "fillRoundedRect";
    CHECK(rounded >= 2);  // the groove and the handle (a filled part of the groove appears once the value is above 0)
}

TEST(a_focus_proxy_takes_the_focus_its_widget_is_given) {
    Host h;
    auto* outer = h.root.addChild<Widget>();  // a composite: takes no focus itself
    auto* inner = outer->addChild<Slider>();
    outer->setFocusProxy(inner);
    CHECK(!outer->hasFocus());
    outer->setFocus(FocusReason::Mnemonic);
    CHECK(h.router.focusWidget() == inner);
    CHECK(outer->hasFocus() && inner->hasFocus());
    outer->setFocusProxy(outer);  // itself: no proxy
    CHECK(outer->focusProxy() == nullptr);
    CHECK(!outer->hasFocus());
}

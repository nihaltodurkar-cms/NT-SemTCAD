// N3b on real windows (NATIVE-DESKTOP-PLAN.md 27.8.3): the spin boxes and the slider drawn through UiWindow +
// Direct2D on WARP -- goldens at 100/150/200% in the light theme and in high contrast -- and driven by REAL window
// messages: typing scientific input with a unit, Enter and focus loss, rejected text, stepping by arrows, wheel and
// the auto-repeating buttons, decade stepping, the slider's mouse; and UI Automation through the real client API
// (Spinner with RangeValue and Value, the Slider's RangeValue, a form label naming the spin box and its edit).
// Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/win32/spin_box.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::platform::Mod;

namespace {

constexpr float kW = 400, kH = 330;

std::string g(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
const char* why(Reject r) { return r == Reject::NotANumber ? "not a number" : r == Reject::OutOfRange ? "out of range" : "not whole"; }

struct NumbersWindow {
    std::unique_ptr<UiWindow> w;
    DoubleSpinBox *doping = nullptr, *vg = nullptr, *bad = nullptr, *off = nullptr;
    SpinBox *nx = nullptr, *nz = nullptr;
    Slider *frame = nullptr, *frame_off = nullptr;
    std::vector<std::string> log;

    explicit NumbersWindow(double scale = 1.0) {
        app();  // widget timers (the buttons' auto-repeat) run on N1's Application
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_numbers_tests", .width = 400, .height = 330, .scale_override = scale});
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
        doping = root.addChild<DoubleSpinBox>(hwnd);
        doping->name = "doping";
        doping->setDecimals(0);
        doping->setRange(-1e21, 1e21);
        doping->setSingleStep(1e14);
        doping->setDecadeStep(true);
        doping->setValue(1e17);
        form->addRow("&Doping [cm^-3]", doping);
        vg = root.addChild<DoubleSpinBox>(hwnd);
        vg->name = "vg";
        vg->setRange(-10, 10);
        vg->setSingleStep(0.1);
        vg->setSuffix(" V");
        vg->setValue(1.5);
        form->addRow("Gate &voltage", vg);
        nx = root.addChild<SpinBox>(hwnd);
        nx->name = "nx";
        nx->setRange(2, 2000);
        nx->setValue(64);
        form->addRow("Mesh &nodes (x)", nx);
        nz = root.addChild<SpinBox>(hwnd);
        nz->name = "nz";
        nz->setRange(0, 500);
        nz->setSpecialValueText("(2D)");
        nz->setValue(0);
        form->addRow("Nodes (&z)", nz);
        bad = root.addChild<DoubleSpinBox>(hwnd);
        bad->name = "bad";
        bad->setRange(0, 1000);
        bad->setSuffix(" nm");
        bad->setValue(12);
        form->addRow("&Thickness", bad);
        off = root.addChild<DoubleSpinBox>(hwnd);
        off->name = "off";
        off->setRange(0, 5000);
        off->setSuffix(" K");
        off->setValue(300);
        off->setEnabled(false);
        form->addRow("Temperature", off);
        frame = root.addChild<Slider>();
        frame->name = "frame";
        frame->setRange(0, 100);
        frame->setValue(40);
        form->addRow("&Frame", frame);
        frame_off = root.addChild<Slider>();
        frame_off->name = "frame_off";
        frame_off->setRange(0, 100);
        frame_off->setValue(70);
        frame_off->setEnabled(false);
        form->addRow("Playback", frame_off);
        col->addStretch(1);
        for (DoubleSpinBox* d : {doping, vg, bad, off}) d->edit()->setCaretBlinking(false);
        for (SpinBox* s : {nx, nz}) s->edit()->setCaretBlinking(false);
        doping->on_value_changed = [this](double v) { log.push_back("doping=" + g(v)); };
        doping->on_editing_finished = [this] { log.push_back("doping done"); };
        doping->on_rejected = [this](const std::string&, Reject r) { log.push_back(std::string("doping rejected: ") + why(r)); };
        vg->on_value_changed = [this](double v) { log.push_back("vg=" + g(v)); };
        vg->on_editing_finished = [this] { log.push_back("vg done"); };
        vg->on_rejected = [this](const std::string&, Reject r) { log.push_back(std::string("vg rejected: ") + why(r)); };
        nx->on_value_changed = [this](int v) { log.push_back("nx=" + std::to_string(v)); };
        nx->on_editing_finished = [this] { log.push_back("nx done"); };
        nx->on_rejected = [this](const std::string&, Reject r) { log.push_back(std::string("nx rejected: ") + why(r)); };
        nz->on_value_changed = [this](int v) { log.push_back("nz=" + std::to_string(v)); };
        nz->on_editing_finished = [this] { log.push_back("nz done"); };
        nz->on_rejected = [this](const std::string&, Reject r) { log.push_back(std::string("nz rejected: ") + why(r)); };
        frame->on_value_changed = [this](int v) { log.push_back("frame=" + std::to_string(v)); };
        w->renderNow(nullptr, false);
    }
    PointF dips(double px_x, double px_y) const { return {static_cast<float>(px_x / w->scale()), static_cast<float>(px_y / w->scale())}; }
    PointF centre(Widget* x) const {
        const RectI r = x->windowRect();
        return dips(r.x + r.width / 2.0, r.y + r.height / 2.0);
    }
    PointF editPoint(NumberField* n) const {  // inside its edit, left of the buttons
        const RectI r = n->windowRect();
        return dips(r.x + 20 * w->scale(), r.y + r.height / 2.0);
    }
    PointF button(NumberField* n, bool up) const {
        const RectI r = n->windowRect();
        return dips(r.right() - NumberField::kButtonWidth * w->scale() / 2, r.y + r.height * (up ? 0.25 : 0.75));
    }
    void select(Driver& d, NumberField* n, const std::u16string& text) {  // click in, select all, type
        d.click(editPoint(n));
        d.key('A', Mod::Ctrl);
        d.type(text);
    }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
};

void nightSky() {
    HighContrast& hc = highContrast();
    hc.on = true;
    hc.window = Color::rgb(0x000000);
    hc.window_text = Color::rgb(0xFFFFFF);
    hc.highlight = Color::rgb(0x1AEBFF);
    hc.highlight_text = Color::rgb(0x000000);
    hc.gray_text = Color::rgb(0x3FF23F);
}

bool sameStr(const std::string& got, const std::string& want) {
    if (got != want) std::printf("  got      [%s]%c  expected [%s]%c", got.c_str(), 10, want.c_str(), 10);
    return got == want;
}
#define CHECK_STR(actual, expected) CHECK(sameStr((actual), (expected)))

bool closeTo(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b)); }

void pumpFor(int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        pumpTimers();
        Sleep(5);
    }
}

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

ComPtr<IUIAutomationElement> find(IUIAutomation* a, IUIAutomationElement* from, const std::string& id) {
    for (auto& c : kids(a, from)) {
        BSTR b = nullptr;
        c->get_CurrentAutomationId(&b);
        if (str(b) == id) return c;
        if (auto f = find(a, c.Get(), id)) return f;
    }
    return nullptr;
}

struct Bstr {
    BSTR b;
    explicit Bstr(const wchar_t* w) : b(SysAllocString(w)) {}
    ~Bstr() { SysFreeString(b); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
};

std::string nameOf(IUIAutomationElement* e) {
    BSTR b = nullptr;
    e->get_CurrentName(&b);
    return str(b);
}

}  // namespace

TEST(numbers_match_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        NumbersWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) return;
        f.vg->edit()->setFocus(FocusReason::Tab);
        f.bad->edit()->setText("abc");  // wrong text: the error frame
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        CHECK(img.width == px(kW, pct / 100.0) && img.height == px(kH, pct / 100.0));
        const GoldenResult r = checkGolden(*warp(), "n3b_numbers@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}

TEST(numbers_in_high_contrast_match_the_goldens) {
    const HighContrast saved = highContrast();
    for (int pct : {100, 150, 200}) {
        NumbersWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) break;
        nightSky();  // after the window exists: UiWindow::create reads the system's own state
        f.vg->edit()->setFocus(FocusReason::Tab);
        f.bad->edit()->setText("abc");
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n3b_numbers_hc@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
    highContrast() = saved;
}

TEST(the_fields_show_what_they_hold) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    CHECK_STR(f.doping->text(), "1e17");  // never "100000000000000000"
    CHECK_STR(f.vg->text(), "1.50 V");
    CHECK_STR(f.nx->text(), "64");
    CHECK_STR(f.nz->text(), "(2D)");  // the special value text at the minimum
    CHECK_STR(f.bad->text(), "12.00 nm");
    CHECK_STR(f.off->text(), "300.00 K");
    f.vg->setValueSilent(2.25);
    CHECK_STR(f.vg->text(), "2.25 V");
    CHECK(f.log.empty());  // and a silent set reports nothing
    CHECK(!f.vg->setValue(99));  // clamped, and it says so
    CHECK(f.vg->value() == 10);
}

TEST(scientific_input_with_a_unit_is_read_and_enter_reports_the_change_once) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    f.select(d, f.doping, u"2.5e17");
    CHECK(f.doping->isInputValid());
    CHECK(f.log.empty());  // typing alone changes nothing
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "doping=2.5e+17, doping done");
    CHECK_STR(f.doping->text(), "2.5e17");
    d.key(VK_RETURN);  // again: nothing changed
    CHECK_STR(f.take(), "");
    f.select(d, f.doping, u"5k");  // a prefix: 5000 -- a doping field in cm^-3 has no unit
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "doping=5000, doping done");
    CHECK_STR(f.doping->text(), "5000");
    f.select(d, f.vg, u"2.5 m");  // milli, with the unit left out
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "vg=0.0025, vg done");
    CHECK_STR(f.vg->text(), "0.0025 V");
    f.select(d, f.vg, u"-3 V");
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "vg=-3, vg done");
    CHECK_STR(f.vg->text(), "-3.00 V");
}

TEST(wrong_text_is_refused_out_loud_and_put_back) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    f.select(d, f.vg, u"5k");  // 5000 V: outside -10..10. Qt would clamp it to 10 without a word.
    CHECK(!f.vg->isInputValid());  // the frame says so as it is typed
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "vg rejected: out of range");
    CHECK_STR(f.vg->text(), "1.50 V");
    CHECK(f.vg->value() == 1.5);
    CHECK(f.vg->isInputValid());
    f.select(d, f.vg, u"abc");
    CHECK(!f.vg->isInputValid());
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "vg rejected: not a number");
    CHECK_STR(f.vg->text(), "1.50 V");
    f.select(d, f.vg, u"1e");  // unfinished: no complaint while it is typed ...
    CHECK(f.vg->isInputValid());
    d.key(VK_RETURN);  // ... but it cannot be applied
    CHECK_STR(f.take(), "vg rejected: not a number");
    f.select(d, f.vg, u"");
    d.key(VK_BACK);  // cleared
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "vg rejected: not a number");
    CHECK_STR(f.vg->text(), "1.50 V");
    // a whole-number field
    f.select(d, f.nx, u"12.5");
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nx rejected: not whole");
    CHECK_STR(f.nx->text(), "64");
    f.select(d, f.nx, u"1e2");  // whole, in scientific form
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nx=100, nx done");
    f.select(d, f.nx, u"3k");  // 3000 > 2000
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nx rejected: out of range");
    f.select(d, f.nx, u"1");  // 1 < 2
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nx rejected: out of range");
    CHECK(f.nx->value() == 100);
}

TEST(the_special_value_text_is_the_minimum) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    f.select(d, f.nz, u"8");
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nz=8, nz done");
    CHECK_STR(f.nz->text(), "8");
    f.select(d, f.nz, u"(2D)");
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nz=0, nz done");
    CHECK_STR(f.nz->text(), "(2D)");
    f.select(d, f.nz, u"8");
    d.key(VK_RETURN);
    f.take();
    f.select(d, f.nz, u"0");  // typing the minimum shows it as the special text again
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "nz=0, nz done");
    CHECK_STR(f.nz->text(), "(2D)");
}

TEST(focus_loss_reports_only_a_real_change) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    d.click(f.editPoint(f.vg));
    d.key(VK_TAB);  // away again, nothing changed
    CHECK_STR(f.take(), "");
    f.select(d, f.vg, u"1.75");
    d.key(VK_TAB);  // focus loss commits
    CHECK_STR(f.take(), "vg=1.75, vg done");
    f.select(d, f.vg, u"1.750");  // the same value in another spelling
    d.key(VK_TAB);
    CHECK_STR(f.take(), "");
    // a program's setValue is the new baseline: loading a document is no edit
    f.vg->setValue(3);
    f.take();
    d.click(f.editPoint(f.vg));
    d.key(VK_TAB);
    CHECK_STR(f.take(), "");
}

TEST(arrows_page_keys_and_the_wheel_step_and_focus_loss_reports_once) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    d.click(f.editPoint(f.vg));
    d.key(VK_UP);
    CHECK_STR(f.take(), "vg=1.6");
    d.key(VK_PRIOR);  // ten steps
    CHECK_STR(f.take(), "vg=2.6");
    d.key(VK_DOWN);
    d.key(VK_NEXT);
    CHECK_STR(f.take(), "vg=2.5, vg=1.5");
    CHECK_STR(f.vg->text(), "1.50 V");
    d.wheel(f.editPoint(f.vg), 1);
    CHECK_STR(f.take(), "vg=1.6");
    d.wheel(f.editPoint(f.vg), -2);
    CHECK_STR(f.take(), "vg=1.4");
    d.key(VK_TAB);  // the steps end at the focus loss: one report, for a value that differs from the start
    CHECK_STR(f.take(), "vg done");
    // stepping up and back down is no change
    d.click(f.editPoint(f.vg));
    d.key(VK_UP);
    d.key(VK_DOWN);
    f.take();
    d.key(VK_TAB);
    CHECK_STR(f.take(), "");
    // no focus, no wheel: a wheel turning over a form passes by
    d.wheel(f.editPoint(f.nz), 1);  // (the focus is on nx now)
    CHECK_STR(f.take(), "");
    CHECK(f.nz->value() == 0);
    // the range is a wall
    f.vg->setValueSilent(9.95);
    d.click(f.editPoint(f.vg));
    d.key(VK_UP);
    d.key(VK_UP);
    CHECK_STR(f.take(), "vg=10");
    CHECK_STR(f.vg->text(), "10.00 V");
}

TEST(a_typed_value_is_stepped_from_and_escape_takes_an_edit_back) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    f.select(d, f.vg, u"3");  // typed, not yet entered
    d.key(VK_UP);             // steps from the typed 3, not from 1.5
    CHECK_STR(f.take(), "vg=3.1");
    CHECK_STR(f.vg->text(), "3.10 V");
    f.select(d, f.vg, u"9");
    CHECK(f.vg->text() == "9");
    d.key(VK_ESCAPE);  // the pending edit goes
    CHECK_STR(f.vg->text(), "3.10 V");
    CHECK(f.vg->isInputValid());
    CHECK_STR(f.take(), "");
    f.select(d, f.vg, u"zzz");  // an unusable typed value: a step starts from the held value
    d.key(VK_DOWN);
    CHECK_STR(f.take(), "vg=3");
}

TEST(a_decade_field_steps_by_factors_of_ten) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    d.click(f.editPoint(f.doping));
    d.key(VK_UP);
    CHECK_STR(f.take(), "doping=1e+18");
    CHECK_STR(f.doping->text(), "1e18");
    d.key(VK_DOWN);
    d.key(VK_DOWN);
    CHECK_STR(f.take(), "doping=1e+17, doping=1e+16");
    d.key(VK_PRIOR);  // a decade field's page key is one decade too
    CHECK_STR(f.take(), "doping=1e+17");
    f.doping->setValueSilent(1e14);
    d.key(VK_DOWN);  // below the single step: through zero
    CHECK_STR(f.take(), "doping=0");
    d.key(VK_DOWN);
    CHECK_STR(f.take(), "doping=-1e+14");
    CHECK_STR(f.doping->text(), "-1e14");
    d.key(VK_TAB);
    CHECK_STR(f.take(), "doping done");
}

TEST(the_buttons_step_and_repeat_while_held) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    d.click(f.button(f.vg, true));
    CHECK(f.vg->edit()->hasFocus());  // pressing a button focuses the field
    CHECK_STR(f.take(), "vg=1.6");
    d.click(f.button(f.vg, false));
    d.click(f.button(f.vg, false));
    CHECK_STR(f.take(), "vg=1.5, vg=1.4");
    // held: one step at once, more after the repeat delay
    d.press(f.button(f.vg, true));
    CHECK(closeTo(f.vg->value(), 1.5));
    pumpFor(200);
    CHECK(closeTo(f.vg->value(), 1.5));  // before 500 ms: nothing more
    pumpFor(700);                     // to about 900 ms: one at 500, then every 75 ms
    const double held = f.vg->value();
    std::printf("  held for ~0.9 s: %.1f V\n", held);
    CHECK(held >= 1.5 + 0.1 * 3);  // generous bounds: the timers are real
    CHECK(held <= 1.5 + 0.1 * 9);
    d.release(f.button(f.vg, true));
    const double at_release = f.vg->value();
    pumpFor(300);
    CHECK(closeTo(f.vg->value(), at_release));  // released: it stops
    // pressed, the pointer leaves the button: the repeat pauses
    f.vg->setValueSilent(0);
    d.press(f.button(f.vg, true));
    d.move(f.editPoint(f.vg));  // held, off the button
    pumpFor(800);
    CHECK(closeTo(f.vg->value(), 0.1));
    d.release(f.editPoint(f.vg));
    // at a limit the button does nothing more
    f.vg->setValueSilent(10);
    f.take();
    d.click(f.button(f.vg, true));
    CHECK_STR(f.take(), "");
}

TEST(a_mnemonic_focuses_the_field_and_selects_its_text) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.w->router().setAlwaysShowCues(true);
    Driver d(*f.w);
    d.key('V', Mod::Alt);  // "Gate &voltage"
    CHECK(f.vg->edit()->hasFocus());
    CHECK(f.vg->edit()->model().hasSelection());
    CHECK_STR(f.vg->edit()->model().selectedText(), "1.50 V");
    d.key(VK_TAB);  // Tab into the next field selects its text as well
    CHECK(f.nx->edit()->hasFocus() && f.nx->edit()->model().selectedText() == "64");
}

TEST(the_slider_answers_real_mouse_and_key_messages) {
    NumbersWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.take();
    Driver d(*f.w);
    const RectI g = f.frame->windowRect();
    const double s = f.w->scale();
    auto at = [&](int value) {  // the point where a handle for `value` is centred
        const double travel = g.width / s - Slider::kHandle;
        return f.dips(g.x + (Slider::kHandle / 2 + travel * value / 100.0) * s, g.y + g.height / 2.0);
    };
    d.press(at(75));  // on the groove: jumps there and keeps dragging
    CHECK(f.frame->value() == 75);
    CHECK(f.frame->isSliderDown());
    d.move(at(20));
    CHECK(f.frame->value() == 20);
    d.release(at(20));
    CHECK(!f.frame->isSliderDown());
    CHECK(f.frame->hasFocus());  // pressing it focuses it
    CHECK_STR(f.take(), "frame=75, frame=20");
    d.key(VK_RIGHT);
    d.key(VK_NEXT);
    d.key(VK_HOME);
    d.key(VK_END);
    CHECK_STR(f.take(), "frame=21, frame=11, frame=0, frame=100");
    d.wheel(at(50), -1);
    CHECK_STR(f.take(), "frame=97");
    // disabled: no mouse
    d.click(f.centre(f.frame_off));
    CHECK(f.frame_off->value() == 70);
    CHECK(!f.frame_off->hasFocus());
}

TEST(uia_sees_spin_boxes_and_sliders_as_range_values) {
    NumbersWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.take();
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    // the spin box: a Spinner named by its form label (the '&' dropped), RangeValue and Value
    auto doping = find(a.Get(), win.Get(), "doping");
    CHECK(doping != nullptr);
    if (!doping) return;
    CONTROLTYPEID ct = 0;
    doping->get_CurrentControlType(&ct);
    CHECK(ct == UIA_SpinnerControlTypeId);
    CHECK_STR(nameOf(doping.Get()), "Doping [cm^-3]");
    ComPtr<IUIAutomationRangeValuePattern> rv;
    CHECK(SUCCEEDED(doping->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&rv))) && rv);
    if (rv) {
        double v = 0, lo = 0, hi = 0, step1 = 0, stepN = 0;
        BOOL ro = TRUE;
        rv->get_CurrentValue(&v);
        rv->get_CurrentMinimum(&lo);
        rv->get_CurrentMaximum(&hi);
        rv->get_CurrentSmallChange(&step1);
        rv->get_CurrentLargeChange(&stepN);
        rv->get_CurrentIsReadOnly(&ro);
        CHECK(v == 1e17 && lo == -1e21 && hi == 1e21 && step1 == 1e14 && stepN == 1e15 && !ro);
        CHECK(SUCCEEDED(rv->SetValue(3e17)));
        CHECK(f.doping->value() == 3e17);
        CHECK_STR(f.take(), "doping=3e+17, doping done");  // a screen reader's set is an edit, reported as one
        CHECK(FAILED(rv->SetValue(2e22)));                   // outside the range: refused, not clamped
        CHECK(f.doping->value() == 3e17);
        CHECK_STR(f.take(), "");
    }
    ComPtr<IUIAutomationValuePattern> vp;
    CHECK(SUCCEEDED(doping->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&vp))) && vp);
    if (vp) {
        BSTR b = nullptr;
        vp->get_CurrentValue(&b);
        CHECK_STR(str(b), "3e17");
        Bstr good(L"4e17"), wrong(L"not a number");
        CHECK(SUCCEEDED(vp->SetValue(good.b)));  // the text, parsed as if typed
        CHECK(f.doping->value() == 4e17);
        CHECK(FAILED(vp->SetValue(wrong.b)));
        CHECK(f.doping->value() == 4e17);
    }
    // its edit is a child, named as the spin box is
    const auto children = kids(a.Get(), doping.Get());
    CHECK(children.size() == 1);
    if (!children.empty()) {
        CONTROLTYPEID cct = 0;
        children[0]->get_CurrentControlType(&cct);
        CHECK(cct == UIA_EditControlTypeId);
        CHECK_STR(nameOf(children[0].Get()), "Doping [cm^-3]");
    }
    // a whole-number box refuses a fraction
    auto nx = find(a.Get(), win.Get(), "nx");
    ComPtr<IUIAutomationRangeValuePattern> nrv;
    CHECK(nx && SUCCEEDED(nx->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&nrv))) && nrv);
    if (nrv) {
        CHECK(FAILED(nrv->SetValue(12.5)));
        CHECK(f.nx->value() == 64);
        CHECK(SUCCEEDED(nrv->SetValue(128)));
        CHECK(f.nx->value() == 128);
    }
    // the slider: Slider with RangeValue
    auto fr = find(a.Get(), win.Get(), "frame");
    CHECK(fr != nullptr);
    if (fr) {
        fr->get_CurrentControlType(&ct);
        CHECK(ct == UIA_SliderControlTypeId);
        CHECK_STR(nameOf(fr.Get()), "Frame");
        ComPtr<IUIAutomationRangeValuePattern> srv;
        CHECK(SUCCEEDED(fr->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&srv))) && srv);
        if (srv) {
            double v = 0, hi = 0, step1 = 0, stepN = 0;
            srv->get_CurrentValue(&v);
            srv->get_CurrentMaximum(&hi);
            srv->get_CurrentSmallChange(&step1);
            srv->get_CurrentLargeChange(&stepN);
            CHECK(v == 40 && hi == 100 && step1 == 1 && stepN == 10);
            CHECK(SUCCEEDED(srv->SetValue(55)));
            CHECK(f.frame->value() == 55);
            CHECK(FAILED(srv->SetValue(101)));
            CHECK(FAILED(srv->SetValue(7.5)));
            CHECK(f.frame->value() == 55);
        }
    }
    // disabled ones refuse a set
    auto off = find(a.Get(), win.Get(), "off");
    ComPtr<IUIAutomationRangeValuePattern> orv;
    CHECK(off && SUCCEEDED(off->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&orv))) && orv);
    if (orv) {
        CHECK(FAILED(orv->SetValue(400)));
        CHECK(f.off->value() == 300);
    }
}

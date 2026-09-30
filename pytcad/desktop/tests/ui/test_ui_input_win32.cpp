// N2c on real windows (NATIVE-DESKTOP-PLAN.md 27.7): the InputRouter behind UiWindow, driven with real window
// messages through the window procedure (ui_driver.hpp) -- mouse at 150% (coordinate conversion), capture, wheel in
// screen coordinates, leave, Tab/shortcut/mnemonic keys (Alt as WM_SYSKEYDOWN), characters with a surrogate pair,
// the cursor via WM_SETCURSOR, focus across a native CHILD window (the stand-in for the VTK view) and back, and a
// tooltip on N1's real Application timer. Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "platform/app.hpp"
#include "ui/core/layout.hpp"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::platform::Mod;
using tcad::platform::MouseButton;
using tcad::platform::MouseType;

namespace {

std::vector<std::string> g_events;

class Probe final : public Widget {
public:
    SizeF hint{80, 30};
    bool take_mouse = true;
    Probe(std::string n, FocusPolicy fp) {
        name = std::move(n);
        setFocusPolicy(fp);
    }
    SizeF sizeHint() const override { return hint; }
    bool mouseEvent(const UiMouseEvent& e) override {
        if (e.type != MouseType::Move)
            g_events.push_back(name + (e.type == MouseType::Down ? ":down" : e.type == MouseType::Up ? ":up" : ":wheel") + "@" +
                               std::to_string(static_cast<int>(e.pos.x)) + "," + std::to_string(static_cast<int>(e.pos.y)) +
                               (e.type == MouseType::Wheel ? " " + std::to_string(e.wheel_steps) : ""));
        return take_mouse;
    }
    bool keyEvent(const tcad::platform::KeyEvent& e) override {
        if (e.down) g_events.push_back(name + ":key" + std::to_string(e.vk));
        return false;
    }
    bool charEvent(char32_t c) override {
        g_events.push_back(name + ":char" + std::to_string(static_cast<unsigned>(c)));
        return true;
    }
    void focusChanged(bool in, FocusReason) override { g_events.push_back(name + (in ? ":in" : ":out")); }
    void hoverChanged(bool in) override { g_events.push_back(name + (in ? ":enter" : ":leave")); }
};

tcad::platform::Application& app() {  // N1's message loop and timers; one per process
    static tcad::platform::Application a;
    return a;
}

// Run the Application's own messages (its timers and posted calls) and nothing else. Pumping everything would also
// deliver the WM_MOUSELEAVE Windows posts right after a synthetic move -- N1's window asks TrackMouseEvent, which
// looks at the REAL cursor, and that is not over a hidden test window.
void pumpTimers() {
    MSG m;
    while (PeekMessageW(&m, app().dispatcherWindow(), 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

struct Fixture {
    std::unique_ptr<UiWindow> w;
    Probe *a = nullptr, *b = nullptr, *c = nullptr;
    explicit Fixture(double scale) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_input_tests", .width = 300, .height = 200, .scale_override = scale});
        if (!r) {
            std::printf("  UiWindow::create: %s\n", r.error().c_str());
            return;
        }
        w = std::move(*r);
        w->resizeClient(px(300, scale), px(200, scale));
        auto* col = w->root().setLayout<BoxLayout>(Orientation::Vertical);
        a = col->add<Probe>("a", FocusPolicy::Strong);
        b = col->add<Probe>("b", FocusPolicy::Strong);
        c = col->add<Probe>("c", FocusPolicy::Strong);
        col->addStretch(1);
        w->renderNow(nullptr, false);  // lay out
        g_events.clear();
    }
};

// the string check: prints both sides when they differ
bool sameStr(const std::string& got, const std::string& want) {
    if (got != want) std::printf("  got      [%s]%c  expected [%s]%c", got.c_str(), 10, want.c_str(), 10);
    return got == want;
}
#define CHECK_STR(actual, expected) CHECK(sameStr((actual), (expected)))

std::string take() {
    std::string s;
    for (auto& e : g_events) s += e + " ";
    g_events.clear();
    return s;
}

}  // namespace

TEST(mouse_messages_reach_widgets_in_dips_at_150_percent_and_capture_the_mouse) {
    Fixture f(1.5);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    // a is at DIPs (9, 9) .. (289, 39) in a 300x200-DIP window (margin 9, Preferred width)
    d.move({20, 20});
    d.press({20, 20});
    CHECK(GetCapture() == f.w->window().hwnd());  // N1's window captures on the first button
    CHECK(f.w->router().grabber() == f.a);
    d.move({250, 180});                           // leave a while pressed
    d.release({250, 180});
    CHECK(GetCapture() != f.w->window().hwnd());
    CHECK(f.w->router().grabber() == nullptr);
    // a sits at the 9-DIP margin = 13.5 px, rounded to 14 px = 9.33 DIPs: local positions 10.67 and 240.67
    CHECK_STR(take(), "a:enter a:in a:down@10,10 a:up@240,170 a:leave ");
    CHECK(f.a->hasFocus());
}

TEST(wheel_uses_screen_coordinates_and_leave_clears_hover) {
    Fixture f(1.0);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.move({20, 50});  // b: y 45..75
    d.wheel({20, 50}, 2.0);
    CHECK(f.b->isHovered());
    d.leave();
    CHECK(!f.b->isHovered() && f.w->router().hoverWidget() == nullptr);
    CHECK_STR(take(), "b:enter b:wheel@11,5 2.000000 b:leave ");
}

TEST(tab_shortcuts_and_mnemonics_arrive_as_real_key_messages) {
    Fixture f(1.0);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    int saved = 0;
    CHECK(f.w->router().shortcuts().add("Ctrl+S", [&] { ++saved; }));
    f.c->setMnemonic(U'C');
    d.key(VK_TAB);
    d.key(VK_TAB);
    CHECK(f.w->router().focusWidget() == f.b);
    d.key(VK_TAB, Mod::Shift);
    CHECK(f.w->router().focusWidget() == f.a);
    CHECK_EQ(d.key('S', Mod::Ctrl), LRESULT{0});  // handled
    CHECK_EQ(saved, 1);
    d.key('C', Mod::Alt);  // WM_SYSKEYDOWN
    CHECK(f.w->router().focusWidget() == f.c);
    d.key('X');  // nobody takes it: bubbles from c to the root, unhandled
    CHECK_STR(take(), "a:in a:out b:in b:out a:in a:out c:in c:key88 ");
}

TEST(characters_including_a_surrogate_pair_reach_the_focus_widget) {
    Fixture f(1.0);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.b->setFocus();
    take();
    d.type(u"a\U0001F600");
    CHECK_STR(take(), "b:char97 b:char128512 ");
}

TEST(the_cursor_follows_the_widget_under_the_pointer) {
    Fixture f(1.0);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.b->setCursor(Cursor::IBeam);
    d.move({20, 50});
    CHECK(d.setCursorMessage());
    CHECK(GetCursor() == LoadCursorW(nullptr, IDC_IBEAM));
    d.move({20, 20});
    CHECK(d.setCursorMessage());
    CHECK(GetCursor() == LoadCursorW(nullptr, IDC_ARROW));
}

TEST(focus_leaves_for_a_native_child_window_and_comes_back) {
    Fixture f(1.0);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    HWND top = f.w->window().hwnd();
    // Windows gives the keyboard focus only to a visible window: show it without activating anything else visibly
    // far off-screen, and put a child window in it -- the stand-in for the VTK view's HWND.
    SetWindowPos(top, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(top, SW_SHOWNOACTIVATE);
    HWND child = CreateWindowExW(0, L"STATIC", L"vtk stand-in", WS_CHILD | WS_VISIBLE, 0, 150, 100, 40, top, nullptr, nullptr, nullptr);
    CHECK(child != nullptr);
    SetFocus(top);
    const bool have_focus = GetFocus() == top;
    if (!have_focus) {
        std::printf("  SKIP: this session cannot give the test window the keyboard focus (GetFocus=%p)\n", static_cast<void*>(GetFocus()));
        DestroyWindow(child);
        ShowWindow(top, SW_HIDE);
        return;
    }
    f.b->setFocus();
    CHECK(f.b->hasFocus());
    take();
    SetFocus(child);  // the user clicked into the 3D view
    CHECK(GetFocus() == child);
    CHECK(!f.b->hasFocus() && f.w->router().focusWidget() == f.b);  // hidden, remembered
    SetFocus(top);    // and back
    CHECK(f.b->hasFocus());
    CHECK_STR(take(), "b:out b:in ");
    DestroyWindow(child);
    ShowWindow(top, SW_HIDE);
}

TEST(a_tooltip_shows_after_the_delay_on_the_application_timer) {
    Fixture f(1.0);
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.c->toolTip = "c's tooltip";
    Widget* shown = nullptr;
    auto t0 = std::chrono::steady_clock::now();
    double at_ms = -1;
    f.w->router().on_tooltip = [&](Widget* w, PointF) {
        shown = w;
        at_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    t0 = std::chrono::steady_clock::now();
    d.move({20, 85});  // c: y 81..111
    std::printf("  hover %s, timers %s\n", f.w->router().hoverWidget() ? f.w->router().hoverWidget()->name.c_str() : "none",
                f.w->timers() ? "yes" : "no");
    while (!shown && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
        pumpTimers();
        Sleep(5);
    }
    std::printf("  tooltip for %s after %.0f ms (delay 700)\n", shown ? shown->name.c_str() : "nothing", at_ms);
    CHECK(shown == f.c);
    CHECK(at_ms >= 690 && at_ms < 1500);
    d.press({20, 85});
    CHECK(shown == nullptr);  // a press hides it
    d.release({20, 85});
}

TEST(destroying_the_window_stops_widget_timers) {
    int fired = 0;
    {
        Fixture f(1.0);
        CHECK(f.w != nullptr);
        if (!f.w) return;
        f.a->startTimer(10, true, [&] { ++fired; });
        const auto t0 = std::chrono::steady_clock::now();
        while (fired < 2 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
            pumpTimers();
            Sleep(2);
        }
        CHECK(fired >= 2);
    }  // the window and its tree die here: the widget's timer must not outlive it
    const int at_death = fired;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(100)) {
        pumpTimers();
        Sleep(2);
    }
    CHECK_EQ(fired, at_death);
}

// Portable tests of N2c's input routing (NATIVE-DESKTOP-PLAN.md 27.7; the rules are in input_router.hpp): mouse
// delivery, bubbling, the implicit grab, drag threshold, hover, focus (click, Tab, mnemonic, window activation),
// shortcut priority, characters, tooltips on a fake clock, cursors, widgets dying mid-event, timers, HiDPI hit tests.
// Part of tcad_ui_core_tests: no Win32.
#include "mini_test.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/widget.hpp"

#include <map>
#include <string>
#include <vector>

using namespace tcad::ui;
using tcad::platform::KeyEvent;
using tcad::platform::Mod;
using tcad::platform::MouseButton;
using tcad::platform::MouseEvent;
using tcad::platform::MouseType;

namespace {

std::vector<std::string> g_log;

class FakeTimers final : public TimerService {
public:
    struct T {
        long long due;
        int interval;
        bool repeat;
        std::function<void()> fn;
    };
    long long now = 0;
    TimerId next = 1;
    std::map<TimerId, T> live;
    TimerId start(int ms, bool repeat, std::function<void()> fn) override {
        live[next] = {now + ms, ms, repeat, std::move(fn)};
        return next++;
    }
    void stop(TimerId id) override { live.erase(id); }
    void advance(int ms) {
        const long long end = now + ms;
        for (;;) {
            TimerId id = 0;
            long long due = end + 1;
            for (auto& [k, t] : live)
                if (t.due <= end && t.due < due) id = k, due = t.due;
            if (!id) break;
            now = due;
            auto fn = live[id].fn;
            if (live[id].repeat) live[id].due += live[id].interval;
            else live.erase(id);
            fn();
        }
        now = end;
    }
};

class NoText final : public TextEngine {
public:
    SizeF measure(std::string_view, const TextStyle&) override { return {0, 0}; }
};

class Host final : public UiHost {
public:
    double s = 1.0;
    NoText text;
    FakeTimers clock;
    Widget root;
    InputRouter router{root};
    Host(double scale = 1.0, int w = 200, int h = 200) : s(scale) {
        root.name = "root";
        root.setHost(this);
        root.setGeometry({0, 0, w, h});
        router.setTimers(&clock);
    }
    ~Host() override { root.setHost(nullptr); }
    void invalidate(const RectI&) override {}
    void scheduleLayout() override {}
    TextEngine& textEngine() override { return text; }
    double scale() const override { return s; }
    InputRouter* input() override { return &router; }
    TimerService* timers() override { return &clock; }
    void widgetGone(Widget* w) override { router.widgetGone(w); }
};

// Logs every event it gets; accepts mouse/keys when told to.
class Probe : public Widget {
public:
    bool take_mouse = false, take_keys = false, take_chars = true, override_ctrl_a = false, wants_tab = false;
    std::function<void()> on_down;
    explicit Probe(std::string n, FocusPolicy fp = FocusPolicy::None) {
        name = std::move(n);
        setFocusPolicy(fp);
    }
    bool mouseEvent(const UiMouseEvent& e) override {
        const char* t = e.type == MouseType::Move ? "move" : e.type == MouseType::Down ? "down" : e.type == MouseType::Up ? "up"
                        : e.type == MouseType::Wheel ? "wheel" : "?";
        g_log.push_back(name + ":" + t + "@" + std::to_string(static_cast<int>(e.pos.x)) + "," + std::to_string(static_cast<int>(e.pos.y)) +
                        (e.clicks == 2 ? " x2" : "") + (e.dragging ? " drag" : ""));
        if (e.type == MouseType::Down && on_down) on_down();
        return take_mouse;
    }
    bool keyEvent(const KeyEvent& e) override {
        g_log.push_back(name + ":key" + std::to_string(e.vk) + (e.down ? "" : "^"));
        return take_keys;
    }
    bool charEvent(char32_t c) override {
        g_log.push_back(name + ":char" + std::to_string(static_cast<unsigned>(c)));
        return take_chars;
    }
    bool overridesShortcut(const KeyEvent& e) const override { return override_ctrl_a && e.vk == 'A' && e.mods == Mod::Ctrl; }
    bool wantsTab() const override { return wants_tab; }
    void focusChanged(bool in, FocusReason) override { g_log.push_back(name + (in ? ":focus-in" : ":focus-out")); }
    void hoverChanged(bool in) override { g_log.push_back(name + (in ? ":enter" : ":leave")); }
    void activateMnemonic() override {
        g_log.push_back(name + ":mnemonic");
        Widget::activateMnemonic();
    }
};

MouseEvent me(MouseType t, double x, double y, MouseButton b = MouseButton::None, unsigned held = 0) {
    MouseEvent e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.button = b;
    e.buttons_down = held;
    return e;
}
constexpr unsigned kL = 1u << static_cast<int>(MouseButton::Left);
constexpr unsigned kR = 1u << static_cast<int>(MouseButton::Right);

KeyEvent key(int vk, Mod m = Mod::None, bool down = true) {
    KeyEvent k;
    k.vk = vk;
    k.mods = m;
    k.down = down;
    return k;
}

std::string logged() {
    std::string s;
    for (auto& l : g_log) s += l + " ";
    g_log.clear();
    return s;
}

// root(0,0,200,200) > p(10,10,100,100) > c(window 30,30,40,40)
struct Tree {
    Host h;
    Probe* p;
    Probe* c;
    Tree() {
        p = h.root.addChild<Probe>("p");
        p->setGeometry({10, 10, 100, 100});
        c = p->addChild<Probe>("c");
        c->setGeometry({20, 20, 40, 40});
        g_log.clear();
    }
};

}  // namespace

TEST(mouse_goes_to_the_deepest_widget_and_bubbles_until_accepted) {
    Tree t;
    t.p->take_mouse = true;
    CHECK(t.h.router.mouse(me(MouseType::Down, 40, 45, MouseButton::Left, kL)));
    CHECK_EQ(logged(), std::string("p:enter c:enter c:down@10,15 p:down@30,35 "));  // hover first, then c, then p (root logs nothing)
    CHECK(t.h.router.grabber() == t.p);
}

TEST(a_press_grabs_the_mouse_until_every_button_is_up) {
    Tree t;
    t.c->take_mouse = true;
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL));
    logged();
    t.h.router.mouse(me(MouseType::Move, 42, 41, MouseButton::None, kL));  // 2.2 px: not yet a drag
    t.h.router.mouse(me(MouseType::Move, 190, 5, MouseButton::None, kL));  // far outside c and p
    CHECK_EQ(logged(), std::string("c:move@12,11 c:move@160,-25 drag "));   // no hover change while grabbed
    t.h.router.mouse(me(MouseType::Down, 190, 5, MouseButton::Right, kL | kR));  // another button: still c
    t.h.router.mouse(me(MouseType::Up, 190, 5, MouseButton::Left, kR));
    CHECK(t.h.router.grabber() == t.c);
    t.h.router.mouse(me(MouseType::Up, 190, 5, MouseButton::Right, 0));
    CHECK(t.h.router.grabber() == nullptr);
    CHECK_EQ(logged(), std::string("c:down@160,-25 drag c:up@160,-25 drag c:up@160,-25 drag c:leave p:leave "));  // hover re-evaluated
}

TEST(a_disabled_widget_swallows_mouse_events) {
    Tree t;
    t.p->take_mouse = true;
    t.c->setEnabled(false);
    CHECK(!t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL)));
    CHECK(logged().find("p:down") == std::string::npos);
}

TEST(double_clicks_carry_a_click_count_and_wheel_goes_under_the_pointer) {
    Tree t;
    t.c->take_mouse = true;
    t.h.router.mouse(me(MouseType::DoubleClick, 40, 40, MouseButton::Left, kL));
    t.h.router.mouse(me(MouseType::Up, 40, 40, MouseButton::Left, 0));
    logged();
    auto w = me(MouseType::Wheel, 15, 15);
    w.wheel_steps = 1;
    t.h.router.mouse(w);
    CHECK_EQ(logged(), std::string("p:wheel@5,5 "));  // p ignores it (take_mouse false): on to the root, which logs nothing
}

TEST(a_press_gives_click_focus_to_the_first_widget_that_takes_it) {
    Tree t;
    t.p->setFocusPolicy(FocusPolicy::Click);
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL));  // c takes no focus: p does
    CHECK(t.p->hasFocus());
    t.h.router.mouse(me(MouseType::Up, 40, 40, MouseButton::Left, 0));
    t.h.router.mouse(me(MouseType::Down, 150, 150, MouseButton::Left, kL));  // on the root: focus stays
    CHECK(t.p->hasFocus());
    t.p->setFocusPolicy(FocusPolicy::Tab);  // keyboard-only: a click does not take it
    t.p->clearFocus();
    t.h.router.mouse(me(MouseType::Up, 150, 150, MouseButton::Left, 0));
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL));
    t.h.router.mouse(me(MouseType::Up, 40, 40, MouseButton::Left, 0));
    CHECK(t.h.router.focusWidget() == nullptr);
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Middle, 1u << 2));  // and middle never focuses
    CHECK(t.h.router.focusWidget() == nullptr);
}

TEST(hover_enters_outermost_first_and_leaves_deepest_first) {
    Tree t;
    t.h.router.mouse(me(MouseType::Move, 40, 40));
    CHECK_EQ(logged(), std::string("p:enter c:enter c:move@10,10 p:move@30,30 "));
    t.h.router.mouse(me(MouseType::Move, 15, 15));
    CHECK_EQ(logged(), std::string("c:leave p:move@5,5 "));
    t.h.router.mouse(me(MouseType::Leave, 0, 0));
    CHECK_EQ(logged(), std::string("p:leave "));
    CHECK(!t.p->isHovered());
}

TEST(tab_moves_focus_in_tree_order_skipping_what_cannot_take_it) {
    Host h;
    auto* a = h.root.addChild<Probe>("a", FocusPolicy::Tab);
    auto* b = h.root.addChild<Probe>("b", FocusPolicy::Strong);
    auto* click = h.root.addChild<Probe>("click", FocusPolicy::Click);  // mouse only
    auto* hidden_box = h.root.addChild<Probe>("box");
    auto* hid = hidden_box->addChild<Probe>("hid", FocusPolicy::Strong);
    auto* d = h.root.addChild<Probe>("d", FocusPolicy::Strong);
    auto* off = h.root.addChild<Probe>("off", FocusPolicy::Strong);
    hidden_box->hide();
    off->setEnabled(false);
    (void)click, (void)hid;
    g_log.clear();
    std::string order;
    for (int i = 0; i < 4; ++i) {
        h.router.key(key(0x09));
        order += h.router.focusWidget()->name + " ";
    }
    CHECK_EQ(order, std::string("a b d a "));
    h.router.key(key(0x09, Mod::Shift));
    CHECK(h.router.focusWidget() == d);
    d->wants_tab = true;  // e.g. a multi-line edit
    h.router.key(key(0x09));
    CHECK(h.router.focusWidget() == d);
    CHECK(logged().find("d:key9") != std::string::npos);
    (void)a, (void)b;
}

TEST(shortcuts_come_before_the_focus_widget_unless_it_overrides_them) {
    Host h;
    auto* e = h.root.addChild<Probe>("edit", FocusPolicy::Strong);
    int fired = 0;
    CHECK(h.router.shortcuts().add("Ctrl+A", [&] { ++fired; }));
    e->setFocus();
    e->take_keys = true;
    g_log.clear();
    h.router.key(key('A', Mod::Ctrl));
    CHECK(fired == 1 && logged().empty());
    e->override_ctrl_a = true;  // an edit's select-all wins over the window's shortcut
    h.router.key(key('A', Mod::Ctrl));
    CHECK(fired == 1);
    CHECK_EQ(logged(), std::string("edit:key65 "));
}

TEST(unhandled_keys_bubble_from_the_focus_widget_to_its_parents) {
    Tree t;
    t.c->setFocusPolicy(FocusPolicy::Strong);
    t.p->take_keys = true;
    t.c->setFocus();
    g_log.clear();
    CHECK(t.h.router.key(key('X')));
    CHECK(t.h.router.key(key('X', Mod::None, false)));  // key-up too
    CHECK_EQ(logged(), std::string("c:key88 p:key88 c:key88^ p:key88^ "));
}

TEST(alt_mnemonics_activate_and_cycle_widgets_with_the_same_letter) {
    Host h;
    auto* save = h.root.addChild<Probe>("save", FocusPolicy::Strong);
    auto* show = h.root.addChild<Probe>("show", FocusPolicy::Strong);
    auto* run = h.root.addChild<Probe>("run", FocusPolicy::Strong);
    save->setMnemonic(U's');  // "&Save" / "&Show": both 'S'
    show->setMnemonic(U'S');
    run->setMnemonic(U'R');
    g_log.clear();
    CHECK(h.router.key(key('S', Mod::Alt)));
    CHECK(h.router.focusWidget() == save);
    CHECK(h.router.key(key('S', Mod::Alt)));
    CHECK(h.router.focusWidget() == show);
    CHECK(h.router.key(key('S', Mod::Alt)));
    CHECK(h.router.focusWidget() == save);
    CHECK(!h.router.key(key('Q', Mod::Alt)));  // no such mnemonic, no focus widget taking keys
    CHECK(logged().find("save:mnemonic") != std::string::npos);
    (void)run;
}

TEST(characters_go_to_the_focus_widget_of_an_active_window_only) {
    Host h;
    auto* e = h.root.addChild<Probe>("edit", FocusPolicy::Strong);
    CHECK(!h.router.character(U'a'));  // no focus widget
    e->setFocus();
    g_log.clear();
    CHECK(h.router.character(0x1F600));
    h.router.windowActivated(false);
    CHECK(!h.router.character(U'b'));
    CHECK_EQ(logged(), std::string("edit:char128512 edit:focus-out "));
}

TEST(window_activation_hides_and_restores_focus_and_drops_a_grab) {
    Tree t;
    t.c->setFocusPolicy(FocusPolicy::Strong);
    t.c->take_mouse = true;
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL));
    CHECK(t.c->hasFocus() && t.h.router.grabber() == t.c);
    g_log.clear();
    t.h.router.windowActivated(false);  // e.g. focus went to the VTK child window, or another app
    CHECK(!t.c->hasFocus() && t.h.router.focusWidget() == t.c && t.h.router.grabber() == nullptr);
    t.h.router.windowActivated(true);
    CHECK(t.c->hasFocus());
    CHECK_EQ(logged(), std::string("c:focus-out c:focus-in "));
}

TEST(a_tooltip_appears_after_the_delay_and_any_press_hides_it) {
    Tree t;
    t.p->toolTip = "the parent's tooltip";  // c has none: the nearest one up the chain is used
    std::vector<std::string> shown;
    t.h.router.on_tooltip = [&](Widget* w, PointF) { shown.push_back(w ? w->name : "-"); };
    t.h.router.mouse(me(MouseType::Move, 40, 40));
    t.h.clock.advance(699);
    CHECK(shown.empty());
    t.h.router.mouse(me(MouseType::Move, 41, 40));  // moving within the same widget does not restart it
    t.h.clock.advance(1);
    CHECK(shown == std::vector<std::string>{"p"});
    CHECK(t.h.router.tooltipWidget() == t.p);
    t.h.router.mouse(me(MouseType::Down, 41, 40, MouseButton::Left, kL));
    CHECK((shown == std::vector<std::string>{"p", "-"}));
    t.h.router.mouse(me(MouseType::Up, 41, 40, MouseButton::Left, 0));
    t.h.router.mouse(me(MouseType::Move, 150, 150));  // the root: no tooltip anywhere up the chain
    t.h.clock.advance(5000);
    CHECK(shown.size() == 2);
}

TEST(the_cursor_is_the_nearest_set_one_and_the_grabber_keeps_it) {
    Tree t;
    t.c->setCursor(Cursor::IBeam);
    t.p->setCursor(Cursor::Hand);
    t.h.router.mouse(me(MouseType::Move, 150, 150));
    CHECK(t.h.router.cursor() == Cursor::Arrow);
    t.h.router.mouse(me(MouseType::Move, 15, 15));
    CHECK(t.h.router.cursor() == Cursor::Hand);
    t.h.router.mouse(me(MouseType::Move, 40, 40));
    CHECK(t.h.router.cursor() == Cursor::IBeam);
    t.c->take_mouse = true;
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL));
    t.h.router.mouse(me(MouseType::Move, 150, 150, MouseButton::None, kL));  // dragging out: still c's I-beam
    CHECK(t.h.router.cursor() == Cursor::IBeam);
    t.h.router.mouse(me(MouseType::Up, 150, 150, MouseButton::Left, 0));
    // a press on c that c ignores and p takes: p grabs, and the GRABBER's cursor shows, not the hovered c's
    t.c->take_mouse = false;
    t.p->take_mouse = true;
    t.h.router.mouse(me(MouseType::Move, 40, 40));
    t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL));
    CHECK(t.h.router.grabber() == t.p && t.h.router.hoverWidget() == t.c);
    CHECK(t.h.router.cursor() == Cursor::Hand);
}

TEST(a_widget_deleted_by_its_own_handler_is_never_touched_again) {
    Tree t;
    t.c->setFocusPolicy(FocusPolicy::Strong);
    t.c->setFocus();
    t.p->take_mouse = true;
    Probe* c = t.c;
    c->on_down = [&] { t.p->release(c); };  // the handler removes (and so destroys) its own widget
    CHECK(t.h.router.mouse(me(MouseType::Down, 40, 40, MouseButton::Left, kL)));  // then p still gets it
    CHECK(t.h.router.focusWidget() == nullptr && t.h.router.hoverWidget() == t.p);
    CHECK(t.h.router.grabber() == t.p);
    CHECK(logged().find("p:down") != std::string::npos);
}

TEST(hiding_or_disabling_the_focus_widget_drops_focus) {
    Host h;
    auto* e = h.root.addChild<Probe>("e", FocusPolicy::Strong);
    e->setFocus();
    e->hide();
    CHECK(h.router.focusWidget() == nullptr);
    e->show();
    e->setFocus();
    e->setEnabled(false);
    CHECK(h.router.focusWidget() == nullptr);
    e->setEnabled(true);
    auto owned = h.root.release(e);  // leaving the tree while hovered and focused
    CHECK(h.router.focusWidget() == nullptr);
}

TEST(widget_timers_fire_and_stop_when_the_widget_dies) {
    Host h;
    auto* w = h.root.addChild<Probe>("w");
    int single = 0, repeat = 0;
    w->startTimer(35, false, [&] { ++single; });
    const TimerId r = w->startTimer(20, true, [&] { ++repeat; });
    h.clock.advance(100);
    CHECK(single == 1 && repeat == 5);
    w->stopTimer(r);
    h.clock.advance(100);
    CHECK(repeat == 5);
    w->startTimer(20, true, [&] { ++repeat; });
    CHECK(h.clock.live.size() == 1);
    auto owned = h.root.release(w);  // leaving the tree stops its timers (a detached widget has no service)
    CHECK(h.clock.live.empty());
    owned.reset();
}

TEST(hit_testing_is_in_pixels_at_any_scale) {
    Host h(1.5, 300, 300);
    auto* w = h.root.addChild<Probe>("w");
    w->setGeometry({30, 30, 60, 60});  // px: DIPs 20..60
    w->take_mouse = true;
    g_log.clear();
    h.router.mouse(me(MouseType::Down, 25, 26, MouseButton::Left, kL));
    CHECK(logged().find("w:down@5,6") != std::string::npos);
    CHECK(h.router.widgetAt({19.9f, 30}) == &h.root && h.router.widgetAt({59.9f, 59.9f}) == w);
    CHECK(h.router.widgetAt({60.0f, 30}) == &h.root);
}

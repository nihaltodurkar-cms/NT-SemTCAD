// Portable tests of N3c (NATIVE-DESKTOP-PLAN.md 27.8.4): where a popup goes (placePopup), the ComboBox (items, data,
// current item and its signals, the drop-down's life, every key closed and open, typeahead, the wheel, the press
// filter, dismissal) and the popup's row list (hint, scrolling, the pointer). The popup windows themselves are faked
// here -- a service that records what was shown and lets a test dismiss it; the real ones are tested in
// test_ui_combo_win32.cpp. Part of tcad_ui_core_tests: no Win32. Every number is worked out by hand from the fake text
// engine: 6 DIPs a byte at the 12-DIP UI font, a 15-DIP line.
#include "mini_test.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/popup.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/combo_box.hpp"

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

// -- fakes ------------------------------------------------------------------------------------------------------

class FakeText final : public TextEngine {
public:
    SizeF measure(std::string_view s, const TextStyle& st) override { return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size}; }
};

class FakeTimers final : public TimerService {
public:
    struct T {
        long long due;
        std::function<void()> fn;
    };
    long long now = 0;
    TimerId next = 1;
    std::map<TimerId, T> live;
    TimerId start(int ms, bool, std::function<void()> fn) override {
        live[next] = {now + ms, std::move(fn)};
        return next++;
    }
    void stop(TimerId id) override { live.erase(id); }
    void advance(int ms) {
        const long long end = now + ms;
        for (;;) {
            TimerId id = 0;
            for (auto& [k, t] : live)
                if (t.due <= end) id = k;
            if (!id) break;
            auto fn = live[id].fn;
            live.erase(id);
            fn();
        }
        now = end;
    }
};

class FakePopups final : public PopupService {
public:
    class Handle final : public PopupHandle {
    public:
        Handle(FakePopups& s, Widget* anchor, std::unique_ptr<Widget> c) : svc(s), anchor(anchor), owned(std::move(c)) {}
        void close() override { svc.closeHandle(this, false); }
        Widget* content() const override { return owned.get(); }
        RectI screenRectPx() const override { return {}; }
        FakePopups& svc;
        Widget* anchor;
        std::unique_ptr<Widget> owned;
    };
    int shown = 0, closed_by_opener = 0;
    std::unique_ptr<Handle> cur;
    std::vector<std::unique_ptr<Handle>> graveyard;  // as the real service: a popup closes from inside its own handler
    PopupHandle* show(Widget* anchor, std::unique_ptr<Widget> content) override {
        if (cur) dismiss();
        ++shown;
        cur = std::make_unique<Handle>(*this, anchor, std::move(content));
        return cur.get();
    }
    void closeHandle(Handle* h, bool dismissed) {
        if (h != cur.get()) return;
        auto cb = h->on_dismissed;
        graveyard.push_back(std::move(cur));
        if (dismissed && cb) cb();
        else ++closed_by_opener;
    }
    void dismiss() {
        if (cur) closeHandle(cur.get(), true);
    }
    bool open() const { return cur != nullptr; }
    ComboPopupList* list() const { return cur ? dynamic_cast<ComboPopupList*>(cur->owned.get()) : nullptr; }
};

class Host final : public UiHost {
public:
    FakeText text;
    FakeTimers clock;
    FakePopups popups_;
    Widget root;
    InputRouter router{root};
    std::vector<std::string> announced;
    Host() {
        root.setHost(this);
        root.setGeometry({0, 0, 300, 300});
    }
    ~Host() override { root.setHost(nullptr); }
    void invalidate(const RectI&) override {}
    void scheduleLayout() override {}
    TextEngine& textEngine() override { return text; }
    double scale() const override { return 1.0; }
    InputRouter* input() override { return &router; }
    TimerService* timers() override { return &clock; }
    PopupService* popups() override { return &popups_; }
    void announce(Widget*, std::string_view s) override { announced.emplace_back(s); }
    void widgetGone(Widget* w) override { router.widgetGone(w); }  // as UiWindow does
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
UiMouseEvent listMouse(MouseType t, float x, float y) {
    UiMouseEvent e;
    e.type = t;
    e.button = t == MouseType::Move ? MouseButton::None : MouseButton::Left;
    e.pos = {x, y};
    return e;
}
constexpr int kF4 = 0x73;

// A combo box at (10,10) 120x24 with three items and every signal logged.
struct Rig {
    Host h;
    ComboBox* c;
    std::vector<std::string> log;
    Rig() {
        c = h.root.addChild<ComboBox>();
        c->setGeometry({10, 10, 120, 24});
        c->addItem("Boltzmann", 1);
        c->addItem("Fermi-Dirac", 2);
        c->addItem("Incomplete", 3);
        c->on_current_index_changed = [this](int i) { log.push_back("index " + std::to_string(i)); };
        c->on_current_text_changed = [this](const std::string& t) { log.push_back("text " + t); };
        c->on_activated = [this](int i) { log.push_back("activated " + std::to_string(i)); };
    }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
    void pressCombo() {
        h.router.mouse(mouse(MouseType::Down, 20, 20, kLeftBit));
        h.router.mouse(mouse(MouseType::Up, 20, 20, 0));
    }
    void focus() { c->setFocus(FocusReason::Tab); }
    void type(const std::string& s) {
        for (char ch : s) h.router.character(static_cast<char32_t>(ch));
    }
};

bool is(const std::string& got, const std::string& want) {
    if (got != want) std::printf("  got      [%s]%c  expected [%s]%c", got.c_str(), 10, want.c_str(), 10);
    return got == want;
}
#define CHECK_STR(a, b) CHECK(is((a), (b)))

}  // namespace

// -- where a popup goes -------------------------------------------------------------------------------------------

TEST(popup_goes_below_the_anchor_at_least_as_wide) {
    const RectI work{0, 0, 1000, 800};
    auto p = placePopup({100, 100, 120, 24}, {90, 200}, work);
    CHECK(p.rect == (RectI{100, 124, 120, 200}) && !p.above && !p.shrunk);  // the anchor's width wins
    p = placePopup({100, 100, 120, 24}, {300, 200}, work);
    CHECK(p.rect == (RectI{100, 124, 300, 200}));  // the content's, when wider
}

TEST(popup_moves_left_to_fit_the_work_area_and_is_no_wider_than_it) {
    const RectI work{0, 0, 1000, 800};
    CHECK(placePopup({900, 100, 80, 24}, {300, 100}, work).rect == (RectI{700, 124, 300, 100}));
    CHECK(placePopup({-50, 100, 80, 24}, {300, 100}, work).rect.x == 0);  // an anchor partly off the left edge
    CHECK(placePopup({100, 100, 80, 24}, {5000, 100}, work).rect == (RectI{0, 124, 1000, 100}));
    // a work area that does not start at 0 (a second monitor, left of the primary)
    CHECK(placePopup({-900, 100, 80, 24}, {300, 100}, {-1000, 0, 1000, 800}).rect == (RectI{-900, 124, 300, 100}));
}

TEST(popup_flips_above_when_it_does_not_fit_below_and_fits_above) {
    const RectI work{0, 0, 1000, 800};
    auto p = placePopup({100, 700, 120, 24}, {150, 200}, work);  // 76 below, 700 above
    CHECK(p.rect == (RectI{100, 500, 150, 200}) && p.above && !p.shrunk);
    p = placePopup({100, 576, 120, 24}, {150, 200}, work);  // exactly 200 below: stays below
    CHECK(p.rect.y == 600 && !p.above);
    p = placePopup({100, 577, 120, 24}, {150, 200}, work);  // 199 below: above
    CHECK(p.above && p.rect.y == 377);
}

TEST(popup_is_cut_to_the_roomier_side_when_it_fits_on_neither) {
    const RectI work{0, 0, 1000, 400};
    auto p = placePopup({100, 100, 120, 24}, {150, 600}, work);  // 276 below, 100 above: below, cut
    CHECK(p.rect == (RectI{100, 124, 150, 276}) && p.shrunk && !p.above);
    p = placePopup({100, 300, 120, 24}, {150, 600}, work);  // 76 below, 300 above: above, cut
    CHECK(p.rect == (RectI{100, 0, 150, 300}) && p.shrunk && p.above);
    p = placePopup({100, 100, 120, 24}, {150, 5000}, work);  // never taller than the work area
    CHECK(p.rect.height <= 400);
}

// -- items, data and the current item ---------------------------------------------------------------------------

TEST(combo_starts_empty_and_the_first_item_becomes_current) {
    Host h;
    auto* c = h.root.addChild<ComboBox>();
    std::vector<std::string> log;
    c->on_current_index_changed = [&](int i) { log.push_back("index " + std::to_string(i)); };
    c->on_current_text_changed = [&](const std::string& t) { log.push_back("text " + t); };
    CHECK(c->count() == 0 && c->currentIndex() == -1 && c->currentText().empty());
    CHECK(std::holds_alternative<std::monostate>(c->currentData()));
    c->addItem("Boltzmann", 1);
    CHECK(c->currentIndex() == 0 && c->currentText() == "Boltzmann");
    c->addItem("Fermi-Dirac", 2);
    CHECK(c->currentIndex() == 0);  // only the first
    CHECK((log == std::vector<std::string>{"index 0", "text Boltzmann"}));
    c->addItems({"a", "b"});
    CHECK(c->count() == 4 && c->itemText(3) == "b");
}

TEST(combo_data_and_lookup) {
    Rig r;
    r.c->addItem("Two D", true);
    r.c->addItem("Named", std::string("x-y"));
    r.c->addItem("Plain");
    CHECK(std::get<int>(r.c->itemData(1)) == 2);
    CHECK(std::get<bool>(r.c->itemData(3)) == true);
    CHECK(std::get<std::string>(r.c->itemData(4)) == "x-y");
    CHECK(std::holds_alternative<std::monostate>(r.c->itemData(5)));
    CHECK(std::holds_alternative<std::monostate>(r.c->itemData(99)) && r.c->itemText(-1).empty());
    CHECK(r.c->findText("Fermi-Dirac") == 1 && r.c->findText("nope") == -1 && r.c->findText("fermi-dirac") == -1);
    CHECK(r.c->findData(3) == 2 && r.c->findData(true) == 3 && r.c->findData(std::string("x-y")) == 4);
    CHECK(r.c->findData(4) == -1);
    CHECK(r.c->findData(false) == -1);  // a bool is not an int
    r.c->setCurrentIndex(3);
    CHECK(std::get<bool>(r.c->currentData()) == true);
}

TEST(combo_current_item_changes_are_reported_both_ways) {
    Rig r;
    r.take();
    r.c->setCurrentIndex(2);
    CHECK_STR(r.take(), "index 2, text Incomplete");
    r.c->setCurrentIndex(2);  // no change, no signal
    CHECK_STR(r.take(), "");
    r.c->setCurrentIndex(9);  // out of range: ignored
    r.c->setCurrentIndex(-2);
    CHECK(r.c->currentIndex() == 2);
    r.c->setCurrentText("Boltzmann");
    CHECK_STR(r.take(), "index 0, text Boltzmann");
    r.c->setCurrentText("nope");  // not an item: nothing changes
    CHECK(r.c->currentIndex() == 0 && r.take().empty());
    r.c->setCurrentIndexSilent(1);
    CHECK(r.c->currentIndex() == 1 && r.take().empty());  // QSignalBlocker
    r.c->setCurrentIndex(-1);
    CHECK_STR(r.take(), "index -1, text ");
    CHECK(r.c->currentText().empty());
}

TEST(combo_clear_leaves_no_current_item) {
    Rig r;
    r.take();
    r.c->clear();
    CHECK_STR(r.take(), "index -1, text ");
    CHECK(r.c->count() == 0 && r.c->currentIndex() == -1);
    r.c->clear();  // already empty
    CHECK_STR(r.take(), "");
}

TEST(combo_hint_is_the_widest_item_the_padding_and_the_arrow) {
    Host h;
    auto* c = h.root.addChild<ComboBox>();
    CHECK(c->sizeHint() == (SizeF{72, 23}));  // "000000" (36) + 16 + 20; a line edit's 15 + 8
    c->addItem("Boltzmann");
    c->addItem("Fermi-Dirac");  // 11 bytes: 66
    CHECK(c->sizeHint() == (SizeF{66 + 16 + 20, 23}));
    CHECK(c->minimumSizeHint() == c->sizeHint());
    CHECK(c->sizePolicy().horizontal == SizePolicy::Minimum && c->sizePolicy().vertical == SizePolicy::Fixed);
    CHECK(c->accessibleRole() == Role::ComboBox);
    CHECK(c->focusPolicy() == FocusPolicy::Strong);
}

// -- the drop-down ------------------------------------------------------------------------------------------------

TEST(a_press_opens_the_drop_down_with_the_current_item_highlighted) {
    Rig r;
    r.c->setCurrentIndex(1);
    r.take();
    r.pressCombo();
    CHECK(r.c->isPopupOpen() && r.h.popups_.open());
    ComboPopupList* l = r.h.popups_.list();
    CHECK(l != nullptr && l->items().size() == 3 && l->highlighted() == 1);
    CHECK(l->items()[2] == "Incomplete");
    CHECK(r.h.popups_.cur->anchor == r.c);
    CHECK(r.c->accessibleExpandState() == 1);
    CHECK(r.take().empty());  // opening changes nothing
    CHECK(r.h.router.focusWidget() == r.c);  // the combo keeps the focus: the popup only takes the mouse
}

TEST(a_press_on_the_combo_while_open_only_closes_it) {
    Rig r;
    r.pressCombo();
    CHECK(r.c->isPopupOpen());
    r.pressCombo();  // the filter closes it and swallows the press: no reopening
    CHECK(!r.c->isPopupOpen() && !r.h.popups_.open());
    CHECK(r.h.popups_.shown == 1);
    CHECK(r.c->accessibleExpandState() == 0);
    r.pressCombo();  // and it opens again
    CHECK(r.c->isPopupOpen() && r.h.popups_.shown == 2);
}

TEST(a_press_elsewhere_closes_it_and_still_reaches_what_is_there) {
    Rig r;
    auto* b = r.h.root.addChild<PushButton>("Run");
    b->setGeometry({10, 100, 80, 26});
    int clicks = 0;
    b->on_clicked = [&] { ++clicks; };
    r.pressCombo();
    r.h.router.mouse(mouse(MouseType::Down, 20, 110, kLeftBit));
    CHECK(!r.c->isPopupOpen());
    CHECK(b->isDown());  // the press was delivered
    r.h.router.mouse(mouse(MouseType::Up, 20, 110, 0));
    CHECK(clicks == 1);
    // the filter is gone with the popup
    r.h.router.mouse(mouse(MouseType::Down, 20, 110, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 20, 110, 0));
    CHECK(clicks == 2);
}

TEST(clicking_a_row_picks_it) {
    Rig r;
    r.pressCombo();
    ComboPopupList* l = r.h.popups_.list();
    l->setGeometry({0, 0, 100, 65});
    // rows are 21 DIPs (15 + 6) below a 1-DIP border: row 2 spans y 43..64
    CHECK(l->rowAt(0.5f) == -1 && l->rowAt(1.5f) == 0 && l->rowAt(22.5f) == 1 && l->rowAt(43.5f) == 2 && l->rowAt(200) == -1);
    l->mouseEvent(listMouse(MouseType::Move, 10, 30));  // hovering highlights
    CHECK(l->highlighted() == 1);
    l->mouseEvent(listMouse(MouseType::Down, 10, 50));
    CHECK(l->highlighted() == 2);
    r.take();
    l->mouseEvent(listMouse(MouseType::Up, 10, 50));
    CHECK_STR(r.take(), "index 2, text Incomplete, activated 2");
    CHECK(!r.c->isPopupOpen());
    CHECK(r.h.popups_.closed_by_opener == 1);
}

TEST(a_press_on_one_row_released_on_another_picks_nothing) {
    Rig r;
    r.pressCombo();
    ComboPopupList* l = r.h.popups_.list();
    l->setGeometry({0, 0, 100, 65});
    r.take();
    l->mouseEvent(listMouse(MouseType::Down, 10, 10));
    l->mouseEvent(listMouse(MouseType::Up, 10, 50));
    CHECK_STR(r.take(), "");
    CHECK(r.c->isPopupOpen());
    l->mouseEvent(listMouse(MouseType::Up, 10, 50));  // a release with no press of its own
    CHECK_STR(r.take(), "");
}

TEST(picking_the_current_item_again_still_reports_activated) {
    Rig r;
    r.pressCombo();
    ComboPopupList* l = r.h.popups_.list();
    l->setGeometry({0, 0, 100, 65});
    r.take();
    l->mouseEvent(listMouse(MouseType::Down, 10, 10));
    l->mouseEvent(listMouse(MouseType::Up, 10, 10));  // row 0 is already current
    CHECK_STR(r.take(), "activated 0");
    CHECK(!r.c->isPopupOpen());
}

TEST(the_window_dismissing_the_popup_leaves_the_combo_closed) {
    Rig r;
    r.pressCombo();
    int expand_events = 0;
    r.c->accessible_expand_changed = [&] { ++expand_events; };
    r.h.popups_.dismiss();  // deactivated, moved, resized
    CHECK(!r.c->isPopupOpen() && r.c->accessibleExpandState() == 0);
    CHECK(expand_events == 1);
    CHECK(!r.h.router.hasPressFilter());
    r.pressCombo();  // and it opens again
    CHECK(r.c->isPopupOpen());
}

TEST(changing_the_items_or_dying_closes_the_popup) {
    Rig r;
    r.pressCombo();
    r.c->addItem("late");
    CHECK(!r.c->isPopupOpen());
    r.pressCombo();
    r.c->clear();
    CHECK(!r.c->isPopupOpen());
    r.c->addItem("x");
    r.pressCombo();
    CHECK(r.h.popups_.open());
    r.h.root.release(r.c).reset();  // the combo box dies with its popup open
    CHECK(!r.h.popups_.open());
    CHECK(!r.h.router.hasPressFilter());
}

TEST(a_disabled_or_empty_combo_does_not_open) {
    Rig r;
    r.c->setEnabled(false);
    r.pressCombo();
    CHECK(!r.c->isPopupOpen());
    r.c->setEnabled(true);
    r.c->clear();
    r.pressCombo();
    CHECK(!r.c->isPopupOpen());
}

// -- keys --------------------------------------------------------------------------------------------------------

TEST(closed_keys_change_the_item_and_report_activated) {
    Rig r;
    r.focus();
    r.take();
    r.h.router.key(key(tcad::ui::keys::Down));
    CHECK_STR(r.take(), "index 1, text Fermi-Dirac, activated 1");
    r.h.router.key(key(tcad::ui::keys::Right));
    CHECK(r.c->currentIndex() == 2);
    r.take();
    r.h.router.key(key(tcad::ui::keys::Down));  // at the end: nothing
    CHECK_STR(r.take(), "");
    r.h.router.key(key(tcad::ui::keys::Up));
    r.h.router.key(key(tcad::ui::keys::Left));
    CHECK(r.c->currentIndex() == 0);
    r.h.router.key(key(tcad::ui::keys::End));
    CHECK(r.c->currentIndex() == 2);
    r.h.router.key(key(tcad::ui::keys::Home));
    CHECK(r.c->currentIndex() == 0);
    r.h.router.key(key(tcad::ui::keys::PageDown));  // ten, clamped
    CHECK(r.c->currentIndex() == 2);
    r.h.router.key(key(tcad::ui::keys::PageUp));
    CHECK(r.c->currentIndex() == 0);
    CHECK(!r.c->isPopupOpen());
}

TEST(space_f4_and_alt_down_open_the_drop_down) {
    Rig r;
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Space));
    CHECK(r.c->isPopupOpen());
    r.h.router.key(key(kF4));  // closes
    CHECK(!r.c->isPopupOpen());
    r.h.router.key(key(kF4));
    CHECK(r.c->isPopupOpen());
    r.h.router.key(key(tcad::ui::keys::Escape));
    r.h.router.key(key(tcad::ui::keys::Down, Mod::Alt));
    CHECK(r.c->isPopupOpen());
    r.h.router.key(key(tcad::ui::keys::Up, Mod::Alt));  // closes
    CHECK(!r.c->isPopupOpen());
    CHECK(r.c->currentIndex() == 0 && r.take().empty());  // none of it changed the item
}

TEST(open_keys_move_the_highlight_and_enter_picks) {
    Rig r;
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Space));
    ComboPopupList* l = r.h.popups_.list();
    r.take();
    r.h.router.key(key(tcad::ui::keys::Down));
    r.h.router.key(key(tcad::ui::keys::Down));
    CHECK(l->highlighted() == 2);
    CHECK(r.c->currentIndex() == 0);  // the highlight is not the item
    r.h.router.key(key(tcad::ui::keys::Down));  // the end
    CHECK(l->highlighted() == 2);
    r.h.router.key(key(tcad::ui::keys::Up));
    CHECK(l->highlighted() == 1);
    r.h.router.key(key(tcad::ui::keys::Home));
    CHECK(l->highlighted() == 0);
    r.h.router.key(key(tcad::ui::keys::End));
    CHECK(l->highlighted() == 2);
    r.h.router.key(key(tcad::ui::keys::PageUp));
    CHECK(l->highlighted() == 0);
    r.h.router.key(key(tcad::ui::keys::PageDown));
    CHECK(l->highlighted() == 2);
    CHECK(r.take().empty());
    r.h.router.key(key(tcad::ui::keys::Return));
    CHECK_STR(r.take(), "index 2, text Incomplete, activated 2");
    CHECK(!r.c->isPopupOpen());
}

TEST(open_space_and_tab_pick_and_escape_does_not) {
    Rig r;
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Space));
    r.h.router.key(key(tcad::ui::keys::Down));
    r.take();
    r.h.router.key(key(tcad::ui::keys::Space));  // picks
    CHECK_STR(r.take(), "index 1, text Fermi-Dirac, activated 1");
    r.h.router.key(key(tcad::ui::keys::Space));
    r.h.router.key(key(tcad::ui::keys::Down));
    r.h.router.key(key(tcad::ui::keys::Tab));  // picks, closes, and focus stays (Tab was the combo's)
    CHECK_STR(r.take(), "index 2, text Incomplete, activated 2");
    CHECK(!r.c->isPopupOpen() && r.h.router.focusWidget() == r.c);
    r.h.router.key(key(tcad::ui::keys::Space));
    r.h.router.key(key(tcad::ui::keys::Up));
    r.h.router.key(key(tcad::ui::keys::Escape));
    CHECK_STR(r.take(), "");
    CHECK(!r.c->isPopupOpen() && r.c->currentIndex() == 2);
}

TEST(escape_and_enter_belong_to_the_open_popup_before_window_shortcuts) {
    Rig r;
    int shortcut = 0;
    r.h.router.shortcuts().add("Escape", [&] { ++shortcut; });
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Escape));  // closed: the shortcut's
    CHECK(shortcut == 1);
    r.h.router.key(key(tcad::ui::keys::Space));
    r.h.router.key(key(tcad::ui::keys::Escape));  // open: the popup's
    CHECK(shortcut == 1 && !r.c->isPopupOpen());
}

TEST(a_ctrl_chord_is_not_the_combos) {
    Rig r;
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Down, Mod::Ctrl));
    CHECK(r.c->currentIndex() == 0);
}

TEST(keys_do_nothing_when_disabled) {
    Rig r;
    r.focus();
    r.c->setEnabled(false);
    CHECK(!r.c->keyEvent(key(tcad::ui::keys::Down)));
    CHECK(r.c->currentIndex() == 0);
}

// -- typeahead ---------------------------------------------------------------------------------------------------

TEST(typing_picks_the_first_item_starting_with_the_letters) {
    Rig r;
    r.c->addItems({"Apple", "apricot", "Banana", "Blueberry", "Cherry"});  // 3..7
    r.focus();
    r.take();
    r.type("b");  // from the current item on: "Banana"
    CHECK(r.c->currentText() == "Banana");
    CHECK_STR(r.take(), "index 5, text Banana, activated 5");
    r.type("l");  // "bl": the prefix grows
    CHECK(r.c->currentText() == "Blueberry");
    r.h.clock.advance(1100);  // forgotten after a second
    r.type("c");
    CHECK(r.c->currentText() == "Cherry");
}

TEST(typing_the_same_letter_again_cycles_and_case_is_folded) {
    Rig r;
    r.c->addItems({"Apple", "apricot", "Banana"});  // 3..5
    r.focus();
    r.type("A");
    CHECK(r.c->currentText() == "Apple");
    r.h.clock.advance(1100);
    r.type("a");
    CHECK(r.c->currentText() == "apricot");
    r.type("a");  // aa: the same letter, next item starting with it: wraps to Apple
    CHECK(r.c->currentText() == "Apple");
    r.h.clock.advance(1100);
    r.type("q");  // nothing starts with q: nothing changes
    CHECK(r.c->currentText() == "Apple");
}

TEST(typing_with_the_drop_down_open_moves_the_highlight_only) {
    Rig r;
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Space));
    r.take();
    r.type("i");
    CHECK(r.h.popups_.list()->highlighted() == 2);
    CHECK(r.c->currentIndex() == 0 && r.take().empty());
    CHECK(r.h.announced.back() == "Incomplete");
}

TEST(space_inside_a_typed_word_is_a_letter_not_the_drop_down) {
    Rig r;
    r.c->addItems({"Gate voltage", "Gate oxide", "Source"});  // 3..5
    r.focus();
    r.type("gate");
    CHECK(r.c->currentText() == "Gate voltage");
    r.h.router.key(key(tcad::ui::keys::Space));  // mid-word
    r.type(" ");
    CHECK(!r.c->isPopupOpen());
    r.type("o");
    CHECK(r.c->currentText() == "Gate oxide");
}

TEST(a_lone_space_character_does_not_type_ahead) {
    Rig r;
    r.focus();
    CHECK(!r.c->charEvent(U' '));
    CHECK(!r.c->charEvent(U'\t'));
    CHECK(r.c->currentIndex() == 0);
}

// -- the wheel ---------------------------------------------------------------------------------------------------

TEST(the_wheel_steps_the_item_only_while_focused) {
    Rig r;
    r.c->setCurrentIndex(1);
    r.take();
    r.h.router.mouse(wheel(20, 20, -1));  // not focused: passes by
    CHECK(r.c->currentIndex() == 1);
    r.focus();
    r.h.router.mouse(wheel(20, 20, -1));  // a notch toward the user: the next item
    CHECK_STR(r.take(), "index 2, text Incomplete, activated 2");
    r.h.router.mouse(wheel(20, 20, 2));   // away: up, two
    CHECK(r.c->currentIndex() == 0);
    r.take();
    r.h.router.key(key(tcad::ui::keys::Space));
    r.h.router.mouse(wheel(20, 20, -1));  // open: the combo leaves it alone
    CHECK(r.c->currentIndex() == 0);
}

// -- UI Automation -----------------------------------------------------------------------------------------------

TEST(the_value_is_the_text_and_setting_it_picks_the_item) {
    Rig r;
    CHECK(r.c->accessibleHasValue() && r.c->accessibleValue() == "Boltzmann" && !r.c->accessibleReadOnly());
    r.take();
    CHECK(r.c->accessibleSetValue("Incomplete"));
    CHECK_STR(r.take(), "index 2, text Incomplete, activated 2");
    CHECK(!r.c->accessibleSetValue("nope"));
    CHECK(r.c->currentIndex() == 2);
    CHECK(r.c->accessibleSetValue("Incomplete") && r.take().empty());  // the item it is: a success, no change
}

TEST(expand_and_collapse_open_and_close_it) {
    Rig r;
    CHECK(r.c->accessibleExpandState() == 0);
    r.c->accessibleExpand(true);
    CHECK(r.c->isPopupOpen() && r.c->accessibleExpandState() == 1);
    r.c->accessibleExpand(true);  // already open
    CHECK(r.h.popups_.shown == 1);
    r.c->accessibleExpand(false);
    CHECK(!r.c->isPopupOpen());
}

TEST(the_highlighted_row_is_announced_and_a_value_change_is_reported) {
    Rig r;
    int value_events = 0;
    r.c->accessible_range_changed = [&] { ++value_events; };
    r.focus();
    r.h.router.key(key(tcad::ui::keys::Down));  // closed: the value changed
    CHECK(value_events == 1);
    r.h.router.key(key(tcad::ui::keys::Space));
    r.h.router.key(key(tcad::ui::keys::Down));
    r.h.router.key(key(tcad::ui::keys::Home));
    CHECK((r.h.announced == std::vector<std::string>{"Incomplete", "Boltzmann"}));  // 2 of them moved
    r.h.popups_.list()->setGeometry({0, 0, 100, 65});
    r.h.popups_.list()->mouseEvent(listMouse(MouseType::Move, 10, 30));  // the pointer moves it too
    CHECK(r.h.announced.back() == "Fermi-Dirac");
}

// -- the row list ------------------------------------------------------------------------------------------------

TEST(the_list_hint_is_ten_rows_at_most) {
    Host h;
    ComboPopupList few({"a", "bb", "ccc"}, 0);
    few.setHost(&h);
    CHECK(few.sizeHint() == (SizeF{18 + 16 + 2, 3 * 21 + 2}));
    std::vector<std::string> many;
    for (int i = 0; i < 25; ++i) many.push_back("item " + std::to_string(i));  // 7 bytes at most: 42
    ComboPopupList lots(many, 0);
    lots.setHost(&h);
    CHECK(lots.sizeHint() == (SizeF{42 + 16 + 2, 10 * 21 + 2}));
    ComboPopupList none({}, -1);
    none.setHost(&h);
    CHECK(none.sizeHint().height == 21 + 2);
    CHECK(lots.accessibleRole() == Role::List);
}

TEST(the_list_scrolls_to_keep_the_highlight_in_view) {
    Host h;
    std::vector<std::string> many;
    for (int i = 0; i < 25; ++i) many.push_back("item " + std::to_string(i));
    ComboPopupList l(many, 0);
    l.setHost(&h);
    l.setGeometry({0, 0, 80, 212});  // ten rows
    CHECK(l.visibleRows() == 10 && l.firstVisible() == 0);
    l.setHighlighted(9);
    CHECK(l.firstVisible() == 0);
    l.setHighlighted(10);  // one past the last visible
    CHECK(l.firstVisible() == 1);
    l.setHighlighted(24);
    CHECK(l.firstVisible() == 15);  // 25 - 10
    l.setHighlighted(2);
    CHECK(l.firstVisible() == 2);
    CHECK(l.rowAt(1.5f) == 2);  // the first row shown is item 2
    l.setGeometry({0, 0, 80, 65});  // a cut popup: three rows
    CHECK(l.visibleRows() == 3);
    l.setHighlighted(24);
    CHECK(l.firstVisible() == 22);
}

TEST(the_list_wheel_scrolls_three_rows_a_notch_and_clamps) {
    Host h;
    std::vector<std::string> many;
    for (int i = 0; i < 25; ++i) many.push_back(std::to_string(i));
    ComboPopupList l(many, 0);
    l.setHost(&h);
    l.setGeometry({0, 0, 80, 212});
    UiMouseEvent e = listMouse(MouseType::Wheel, 10, 10);
    e.wheel_steps = -1;  // toward the user: down
    l.mouseEvent(e);
    CHECK(l.firstVisible() == 3);
    e.wheel_steps = -10;
    l.mouseEvent(e);
    CHECK(l.firstVisible() == 15);
    e.wheel_steps = 100;
    l.mouseEvent(e);
    CHECK(l.firstVisible() == 0);
    ComboPopupList small({"a", "b"}, 0);  // nothing to scroll
    small.setHost(&h);
    small.setGeometry({0, 0, 80, 44});
    e.wheel_steps = -1;
    CHECK(small.mouseEvent(e) && small.firstVisible() == 0);
}

TEST(the_list_paints_its_rows_and_the_highlight) {
    Host h;
    ComboPopupList l({"a", "b", "c"}, 1);
    l.setHost(&h);
    l.setGeometry({0, 0, 60, 65});
    RecordingPainter p;
    l.paint(p);
    int texts = 0, accent_rows = 0;
    for (const auto& op : p.ops()) {
        texts += op.kind == "text";
        accent_rows += op.kind == "fillRect" && op.rect.height == 21;
    }
    CHECK(texts == 3);
    CHECK(accent_rows == 1);  // the highlighted row
}

TEST(the_combo_paints_its_text_and_arrow) {
    Rig r;
    RecordingPainter p;
    r.c->paint(p);
    int texts = 0, polys = 0;
    for (const auto& op : p.ops()) {
        texts += op.kind == "text";
        polys += op.kind == "polygon";
    }
    CHECK(texts == 1 && polys == 1);
}

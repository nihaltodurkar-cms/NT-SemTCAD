// Portable tests of N3f's actions, menus, menu bar, tool bar and tool tip (NATIVE-DESKTOP-PLAN.md 27.8.7). The popup windows are
// faked (a service that records every request and keeps a stack); the real ones are tested in test_ui_chrome_win32.cpp.
// Part of tcad_ui_core_tests: no Win32. The fake text engine gives 6 DIPs a byte and a 15-DIP line, so a menu row is
// ceil(15) + 8 = 23 DIPs, a separator 7, a menu bar title 15 + 8 = 23 high.
#include "mini_test.hpp"

#include "fake_ui.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/edit_context_menu.hpp"
#include "ui/widgets/menu.hpp"
#include "ui/widgets/menu_bar.hpp"
#include "ui/widgets/tool_bar.hpp"

using namespace tcad::ui;
using namespace tcad::ui::fake;
using tcad::desktop::theme::T;
namespace keys = tcad::ui::keys;

namespace {

// Lays a popup's content out at its own size hint, as the real service does before showing it.
void layoutPopup(Widget* w) {
    const SizeF s = w->sizeHint();
    w->setGeometry({0, 0, static_cast<int>(std::ceil(s.width)), static_cast<int>(std::ceil(s.height))});
}

MenuPopup* asPopup(Widget* w) { return dynamic_cast<MenuPopup*>(w); }

struct Rig {
    Host h;
    ActionManager mgr{&h.router};
    Widget* owner;
    Action *open, *save, *quit, *log, *off;
    Menu file{"&File"};
    std::vector<std::string> log_;
    Rig() {
        owner = h.root.addChild<Widget>();
        owner->setGeometry({0, 0, 400, 30});
        open = mgr.create("&Open...", "Ctrl+O");
        save = mgr.create("&Save", "Ctrl+S");
        quit = mgr.create("&Quit", "Ctrl+Q");
        log = mgr.create("&Log scale");
        log->setCheckable(true);
        off = mgr.create("Disa&bled");
        off->setEnabled(false);
        for (Action* a : {open, save, quit, log, off}) a->on_triggered = [this, a](bool c) {
            log_.push_back(parseMnemonicText(a) + (a->isCheckable() ? (c ? " on" : " off") : "") + (file.isOpen() ? " [menu open]" : ""));
        };
        file.addAction(open);
        file.addAction(save);
        file.addSeparator();
        file.addAction(log);
        file.addAction(off);
        file.addSeparator();
        file.addAction(quit);
    }
    static std::string parseMnemonicText(Action* a) {
        std::string s;
        for (char c : a->text())
            if (c != '&') s += c;
        return s;
    }
    std::string take() {
        std::string s;
        for (const auto& e : log_) s += (s.empty() ? "" : ", ") + e;
        log_.clear();
        return s;
    }
    MenuPopup* popup() { return asPopup(h.popups_.content()); }
    void key(int vk, Mod m = Mod::None) { h.router.key(::tcad::ui::fake::key(vk, m)); }
    bool show() {
        const bool ok = file.showBelow(owner, owner);
        if (popup()) layoutPopup(popup());
        return ok;
    }
};

}  // namespace

// -- actions -------------------------------------------------------------------------------------------------------

TEST(an_action_starts_enabled_visible_and_plain_and_tells_its_listeners_when_it_changes) {
    Action a("&Run");
    CHECK(a.text() == "&Run" && a.isEnabled() && a.isVisible() && !a.isCheckable() && !a.isChecked() && !a.isSeparator());
    CHECK(!a.hasShortcut() && a.menu() == nullptr && a.toolTip().empty());
    int n = 0, m = 0;
    const int id = a.addListener([&] { ++n; });
    a.addListener([&] { ++m; });
    a.setText("&Stop");
    a.setText("&Stop");  // the same: nothing
    a.setEnabled(false);
    a.setVisible(false);
    a.setToolTip("Stops");
    a.setCheckable(true);
    CHECK(n == 5 && m == 5);
    a.removeListener(id);
    a.setEnabled(true);
    CHECK(n == 5 && m == 6);
    a.addListener([&] { a.removeListener(id); });  // a listener that removes another during a change is safe
    a.setVisible(true);
    CHECK(true);
}

TEST(a_checkable_action_toggles_when_triggered_and_reports_toggled_then_triggered) {
    Action a("&Log");
    std::vector<std::string> log;
    a.on_toggled = [&](bool c) { log.push_back(c ? "toggled on" : "toggled off"); };
    a.on_triggered = [&](bool c) { log.push_back(c ? "triggered on" : "triggered off"); };
    a.trigger();  // not checkable: only triggered, with checked false
    CHECK((log == std::vector<std::string>{"triggered off"}));
    log.clear();
    a.setCheckable(true);
    a.trigger();
    a.trigger();
    CHECK((log == std::vector<std::string>{"toggled on", "triggered on", "toggled off", "triggered off"}));
    log.clear();
    a.setChecked(true);  // the program: toggled, not triggered
    a.setChecked(true);
    CHECK((log == std::vector<std::string>{"toggled on"}) && a.isChecked());
}

TEST(only_a_checkable_action_reports_toggled_for_a_programs_set_checked) {
    Action a("&Log");
    int toggled = 0;
    a.on_toggled = [&](bool) { ++toggled; };
    a.setChecked(true);  // not checkable: the state is kept, nothing is reported
    CHECK(toggled == 0);
    a.setCheckable(true);
    a.setChecked(false);
    CHECK(toggled == 1);
}

TEST(a_disabled_action_and_a_separator_never_trigger) {
    Action a("x");
    int n = 0;
    a.on_triggered = [&](bool) { ++n; };
    a.setEnabled(false);
    a.trigger();
    CHECK(n == 0);
    auto sep = Action::makeSeparator();
    sep->on_triggered = [&](bool) { ++n; };
    sep->trigger();
    CHECK(n == 0 && sep->isSeparator() && sep->text().empty());
}

TEST(shortcuts_are_validated_when_set) {
    Action a("x");
    CHECK(a.setShortcut("Ctrl+O") && a.shortcutText() == "Ctrl+O" && a.hasShortcut());
    CHECK(!a.setShortcut("Ctrl+Wibble") && a.shortcutText() == "Ctrl+O");  // refused, unchanged
    CHECK(a.setShortcut("Shift+F5") && a.setShortcut("F5") && a.setShortcut("Alt+Left"));
    CHECK(a.setShortcut("") && !a.hasShortcut());
}

TEST(the_manager_registers_shortcuts_and_fires_only_enabled_visible_actions) {
    Host h;
    ActionManager mgr(&h.router);
    int n = 0;
    Action* a = mgr.create("&Open", "Ctrl+O");
    a->on_triggered = [&](bool) { ++n; };
    h.router.key(key('O', Mod::Ctrl));
    CHECK(n == 1);
    a->setEnabled(false);
    h.router.key(key('O', Mod::Ctrl));
    CHECK(n == 1);  // a disabled action's shortcut does nothing (and is not passed on as an error)
    a->setEnabled(true);
    a->setVisible(false);
    h.router.key(key('O', Mod::Ctrl));
    CHECK(n == 1);
    a->setVisible(true);
    h.router.key(key('O', Mod::Ctrl));
    CHECK(n == 2);
    KeyEvent held = key('O', Mod::Ctrl);
    held.repeat = true;
    h.router.key(held);  // an auto-repeat is not another activation
    CHECK(n == 2);
    // the case of the text is not a different key
    Action* b = mgr.create("&Save", "ctrl+s");
    CHECK(b->hasShortcut() && b->shortcutText() == "ctrl+s");
    int s = 0;
    b->on_triggered = [&](bool) { ++s; };
    h.router.key(key('S', Mod::Ctrl));
    CHECK(s == 1);
}

TEST(a_shortcut_that_is_taken_is_refused_counted_and_rebinding_frees_the_old_key) {
    Host h;
    ActionManager mgr(&h.router);
    Action* a = mgr.create("a", "Ctrl+K");
    Action* b = mgr.create("b", "ctrl+k");  // the same key
    CHECK(a->hasShortcut() && !b->hasShortcut() && mgr.conflicts() == 1);
    int n = 0;
    a->on_triggered = [&](bool) { ++n; };
    CHECK(mgr.bindShortcut(a, "Ctrl+L"));
    h.router.key(key('K', Mod::Ctrl));
    CHECK(n == 0);  // the old key is free again
    h.router.key(key('L', Mod::Ctrl));
    CHECK(n == 1);
    CHECK(mgr.bindShortcut(b, "Ctrl+K"));  // and can be taken by another
    CHECK(!mgr.bindShortcut(a, "Ctrl+K") && a->shortcutText().empty() && mgr.conflicts() == 2);  // refused: it had to let go of its own
    CHECK(!mgr.bindShortcut(a, "Ctrl+Nonsense"));
    CHECK(mgr.bindShortcut(a, ""));
    CHECK(mgr.find("a") == a && mgr.find("zzz") == nullptr && mgr.count() == 2);
}

TEST(destroying_the_manager_releases_its_keys_and_separators_are_actions_too) {
    Host h;
    {
        ActionManager mgr(&h.router);
        mgr.create("a", "Ctrl+J");
        CHECK(mgr.separator()->isSeparator() && mgr.count() == 2);
        CHECK(!h.router.shortcuts().add("Ctrl+J", [] {}));  // taken
    }
    CHECK(h.router.shortcuts().add("Ctrl+J", [] {}));  // free again
    ActionManager none(nullptr);  // without a router: actions without keys, nothing breaks
    Action* x = none.create("x", "Ctrl+X");
    CHECK(x->hasShortcut() && none.conflicts() == 0);
}

TEST(find_matches_the_text_without_its_mnemonic) {
    Host h;
    ActionManager mgr(&h.router);
    Action* a = mgr.create("&Open recent project");
    CHECK(mgr.find("Open recent project") == a && mgr.find("&Open recent project") == nullptr);
}

// -- the menu model ------------------------------------------------------------------------------------------------

TEST(a_menu_shows_only_visible_entries_and_never_starts_ends_or_doubles_a_separator) {
    Menu m("&M");
    ActionManager mgr(nullptr);
    Action *a = mgr.create("a"), *b = mgr.create("b"), *c = mgr.create("c");
    b->setVisible(false);
    m.addSeparator();  // first: dropped
    m.addAction(a);
    m.addSeparator();
    m.addAction(b);  // hidden: the two separators around it are one
    m.addSeparator();
    m.addAction(c);
    m.addSeparator();  // last: dropped
    const auto v = m.visibleEntries();
    CHECK(v.size() == 3 && v[0].action == a && v[1].action->isSeparator() && v[2].action == c);
    CHECK(m.entries().size() == 7 && !m.isEmpty());
    m.clear();
    CHECK(m.entries().empty() && m.isEmpty());
    Menu only_sep("x");
    only_sep.addSeparator();
    CHECK(only_sep.isEmpty());
    Menu sub("&Sub");
    Menu parent("&P");
    parent.addMenu(&sub);
    CHECK(parent.visibleEntries().size() == 1 && parent.visibleEntries()[0].submenu == &sub);
}

TEST(a_menu_that_would_be_empty_does_not_open_and_about_to_show_runs_before_every_show) {
    Rig r;
    Menu empty("&E");
    CHECK(!empty.showBelow(r.owner, r.owner) && !r.h.popups_.open());
    int about = 0;
    r.file.on_about_to_show = [&] {
        ++about;
        if (about == 2) r.save->setVisible(false);  // a recent-files menu rebuilds itself here
    };
    CHECK(r.show() && about == 1 && r.popup()->entries().size() == 7);
    r.file.close();
    CHECK(r.show() && about == 2 && r.popup()->entries().size() == 6);  // the hidden one is gone from this showing
    CHECK(r.file.shownCount() == 2);
}

TEST(a_menu_asks_for_a_popup_below_an_anchor_or_at_a_point) {
    Rig r;
    CHECK(r.file.showBelow(r.owner, r.owner));
    CHECK(r.h.popups_.requests.size() == 1 && r.h.popups_.requests[0].anchor == r.owner && r.h.popups_.requests[0].side == PopupSide::Below);
    CHECK(!r.h.popups_.requests[0].child && !r.h.popups_.requests[0].tooltip);
    r.file.close();
    CHECK(r.file.showAt(r.owner, {120, 40}));
    CHECK(r.h.popups_.requests[1].side == PopupSide::AtPoint && r.h.popups_.requests[1].point == (PointF{120, 40}));
    Widget bare;  // a widget outside any window has no popups to ask
    Menu m("x");
    m.addAction(r.open);
    CHECK(!m.showBelow(&bare, &bare));
}

TEST(closing_and_dismissal_end_the_menu_clear_the_routers_filters_and_report_it_once) {
    Rig r;
    int closed = 0;
    r.file.on_closed = [&] { ++closed; };
    r.show();
    CHECK(r.file.isOpen() && r.h.router.hasKeyFilter() && r.h.router.hasPressFilter());
    r.file.close();
    CHECK(!r.file.isOpen() && !r.h.router.hasKeyFilter() && !r.h.router.hasPressFilter() && closed == 1);
    r.file.close();  // already closed
    CHECK(closed == 1);
    r.show();
    r.h.popups_.dismiss();  // the window deactivated, moved or resized: the service dismisses it
    CHECK(!r.file.isOpen() && !r.h.router.hasKeyFilter() && closed == 2);
}

// -- the popup -------------------------------------------------------------------------------------------------------

TEST(rows_are_23_dips_separators_7_and_the_popup_is_as_wide_as_its_widest_entry) {
    Rig r;
    r.show();
    MenuPopup* p = r.popup();
    CHECK(p != nullptr);
    if (!p) return;
    CHECK(p->entries().size() == 7);  // open, save, ---, log, disabled, ---, quit
    // y: 1 (frame) + 3 (padding) = 4; rows 23, 23, then a 7 separator
    CHECK(p->rowTop(0) == 4 && p->rowTop(1) == 27 && p->rowTop(2) == 50 && p->rowTop(3) == 57 && p->rowTop(4) == 80 && p->rowTop(5) == 103 && p->rowTop(6) == 110);
    CHECK(p->rowHeight(2) == 7 && p->rowHeight(0) == 23);
    // 1 + 3 + (5 rows x 23) + (2 separators x 7) + 3 + 1
    CHECK(p->sizeHint().height == 2 + 6 + 5 * 23 + 2 * 7);
    // width: 2 + 24 (check column) + widest text ("Log scale" 9 bytes = 54) + 24 gap + widest shortcut ("Ctrl+O" 6 = 36) + 10 + 6
    CHECK(p->sizeHint().width == 2 + 24 + 54 + 24 + 36 + 10 + 6);
    const auto& items = p->children();
    CHECK(items.size() == 7);
    CHECK((items[0]->geometry() == RectI{1, 4, p->geometry().width - 2, 23}));
    CHECK((items[2]->geometry() == RectI{1, 50, p->geometry().width - 2, 7}));
    CHECK(items[0]->accessibleName == "Open..." && items[2]->accessibleRole() == Role::Separator && items[0]->accessibleRole() == Role::MenuItem);
    CHECK(p->entryText(1) == "Save" && p->entryShortcut(1) == "Ctrl+S" && p->entryShortcut(3).empty());
    CHECK(p->accessibleRole() == Role::Menu);
}

TEST(up_and_down_wrap_over_usable_entries_skipping_separators_and_disabled_ones) {
    Rig r;
    r.show();
    r.key(keys::Down);
    CHECK(r.popup()->highlighted() == 0);
    r.key(keys::Down);
    CHECK(r.popup()->highlighted() == 1);
    r.key(keys::Down);  // the separator (2) is skipped
    CHECK(r.popup()->highlighted() == 3);
    r.key(keys::Down);  // the disabled one (4) and the separator (5) are skipped
    CHECK(r.popup()->highlighted() == 6);
    r.key(keys::Down);  // wraps
    CHECK(r.popup()->highlighted() == 0);
    r.key(keys::Up);
    CHECK(r.popup()->highlighted() == 6);
    r.key(keys::Up);
    CHECK(r.popup()->highlighted() == 3);
    r.key(keys::Home);
    CHECK(r.popup()->highlighted() == 0);
    r.key(keys::End);
    CHECK(r.popup()->highlighted() == 6);
    r.popup()->setHighlighted(4);  // a disabled entry cannot be highlighted
    CHECK(r.popup()->highlighted() == 6);
    r.popup()->setHighlighted(2);
    CHECK(r.popup()->highlighted() == 6);
    r.popup()->setHighlighted(-1);
    CHECK(r.popup()->highlighted() == -1);
    CHECK(r.h.announced.size() > 3 && r.h.announced[0] == "Open...");  // each highlight is spoken, by the name without its marker
}

TEST(enter_and_space_close_the_menu_and_then_trigger_the_action) {
    Rig r;
    r.show();
    r.key(keys::Down);
    r.key(keys::Return);
    CHECK_STR(r.take(), "Open...");  // not "[menu open]": the menu closed first
    CHECK(!r.file.isOpen());
    r.show();
    r.key(keys::End);
    r.key(keys::Space);
    CHECK_STR(r.take(), "Quit");
    r.show();
    r.key(keys::Return);  // nothing highlighted: nothing happens, the menu stays
    CHECK(r.file.isOpen() && r.take().empty());
}

TEST(a_checkable_entry_toggles_and_a_disabled_one_never_triggers) {
    Rig r;
    r.show();
    CHECK(!r.popup()->isCheckedEntry(3));
    r.key(keys::Down);
    r.key(keys::Down);
    r.key(keys::Down);  // Log scale
    r.key(keys::Return);
    CHECK_STR(r.take(), "Log scale on");
    CHECK(r.log->isChecked());
    r.show();
    CHECK(r.popup()->isCheckedEntry(3));
    CHECK(!r.popup()->activate(4));  // the disabled entry
    CHECK(r.take().empty() && r.file.isOpen());
    r.file.close();
}

TEST(a_letter_activates_the_entry_with_that_mnemonic_and_a_shared_letter_cycles) {
    Rig r;
    r.show();
    r.key('S');
    CHECK_STR(r.take(), "Save");
    r.show();
    r.key('Z');  // no entry: nothing, the menu stays
    CHECK(r.file.isOpen() && r.take().empty());
    r.key('B');  // the disabled one has the mnemonic: not usable
    CHECK(r.take().empty());
    r.file.close();
    Menu twin("&T");
    Action *a = r.mgr.create("&Alpha"), *b = r.mgr.create("A&nother A"), *c = r.mgr.create("&Apple");
    twin.addAction(a);
    twin.addAction(b);  // mnemonic N
    twin.addAction(c);
    twin.showBelow(r.owner, r.owner);
    MenuPopup* p = asPopup(r.h.popups_.content());
    layoutPopup(p);
    r.key('A');
    CHECK(p->highlighted() == 0);  // two entries share A: the first press only highlights
    r.key('A');
    CHECK(p->highlighted() == 2);
    r.key('A');
    CHECK(p->highlighted() == 0);  // and round again
    twin.close();
}

TEST(a_press_anywhere_in_the_window_closes_the_menus_and_is_swallowed) {
    Rig r;
    int presses = 0;
    struct Probe : Widget {
        int* n;
        bool mouseEvent(const UiMouseEvent& e) override {
            if (e.type == MouseType::Down) ++*n;
            return true;
        }
    };
    auto* probe = r.h.root.addChild<Probe>();
    probe->n = &presses;
    probe->setGeometry({0, 200, 100, 50});
    r.show();
    r.h.router.mouse(mouse(MouseType::Down, 30, 220, kLeftBit));
    CHECK(!r.file.isOpen());
    CHECK(presses == 0);  // the press that closed the menu never reached what is under it
    r.h.router.mouse(mouse(MouseType::Up, 30, 220, 0));
    r.h.router.mouse(mouse(MouseType::Down, 30, 220, kLeftBit));
    CHECK(presses == 1);  // closed: presses go through again
    r.h.router.mouse(mouse(MouseType::Up, 30, 220, 0));
}

TEST(while_a_menu_is_open_every_key_is_its_and_tab_and_the_releases_are_swallowed) {
    Rig r;
    int typed = 0;
    struct Probe : Widget {
        int* n;
        bool keyEvent(const KeyEvent&) override { return ++*n, true; }
    };
    auto* probe = r.h.root.addChild<Probe>();
    probe->n = &typed;
    probe->setFocusPolicy(FocusPolicy::Strong);
    probe->setGeometry({0, 100, 100, 50});
    probe->setFocus(FocusReason::Tab);
    r.h.router.key(key('Q'));
    CHECK(typed == 1);
    r.show();
    r.h.router.key(key('X'));  // no such entry: the menu used it up, the probe never sees it
    r.h.router.key(key(keys::Tab));
    KeyEvent up = key(keys::Down);
    up.down = false;
    r.h.router.key(up);
    CHECK(typed == 1 && r.file.isOpen());
    r.h.router.key(key('Q', Mod::Alt));  // an Alt chord is not the menu's: it can reach the window's shortcuts and mnemonics
    r.file.close();
    r.h.router.key(key('Q'));
    CHECK(typed >= 2);  // closed: keys go to the focus widget again
}

TEST(a_submenu_opens_by_the_right_key_by_hovering_for_250_ms_or_by_a_click_beside_its_entry) {
    Rig r;
    Menu recent("&Recent");
    Action *r1 = r.mgr.create("one"), *r2 = r.mgr.create("two");
    recent.addAction(r1);
    recent.addAction(r2);
    r.file.addMenu(&recent);
    r.show();
    MenuPopup* p = r.popup();
    const int last = static_cast<int>(p->entries().size()) - 1;
    r.key(keys::End);
    CHECK(p->highlighted() == last);
    r.key(keys::Right);  // opens and moves into it: the first entry is highlighted
    CHECK(r.h.popups_.depth() == 2 && recent.isOpen());
    CHECK(r.h.popups_.requests.back().side == PopupSide::Right && r.h.popups_.requests.back().child);
    CHECK(r.h.popups_.requests.back().anchor == p->children()[static_cast<std::size_t>(last)].get());  // beside ITS entry
    MenuPopup* sub = recent.popup();
    CHECK(sub != nullptr && sub->parentPopup() == p && p->childPopup() == sub && sub->highlighted() == 0);
    r.key(keys::Down);
    CHECK(sub->highlighted() == 1);  // the keys go to the innermost
    r.key(keys::Left);  // closes the submenu, back in its parent
    CHECK(r.h.popups_.depth() == 1 && !recent.isOpen() && r.file.isOpen() && p->highlighted() == last);
    // by the pointer: hovering the entry for 250 ms
    MenuItemWidget* entry = static_cast<MenuItemWidget*>(p->children()[static_cast<std::size_t>(last)].get());
    p->setHighlighted(-1);
    entry->hoverChanged(true);
    r.h.clock.advance(249);
    CHECK(!recent.isOpen());
    r.h.clock.advance(2);
    CHECK(recent.isOpen());
    // hovering another entry closes it
    static_cast<MenuItemWidget*>(p->children()[0].get())->hoverChanged(true);
    CHECK(!recent.isOpen() && p->highlighted() == 0);
    // a click opens it at once
    UiMouseEvent down;
    down.type = MouseType::Down;
    down.button = MouseButton::Left;
    entry->mouseEvent(down);
    CHECK(recent.isOpen());
    CHECK(r.take().empty());  // opening a submenu triggers nothing
    r.file.close();
    CHECK(!recent.isOpen() && !r.file.isOpen());  // closing the root closes the submenu with it
    CHECK(r.h.popups_.depth() == 0);
}

TEST(the_pointer_leaving_an_entry_keeps_its_open_submenu_and_cancels_one_not_yet_open) {
    Rig r;
    Menu recent("&Recent");
    recent.addAction(r.mgr.create("one"));
    r.file.addMenu(&recent);
    r.show();
    MenuPopup* p = r.popup();
    const int last = static_cast<int>(p->entries().size()) - 1;
    auto* entry = static_cast<MenuItemWidget*>(p->children()[static_cast<std::size_t>(last)].get());
    entry->hoverChanged(true);
    r.h.clock.advance(300);
    CHECK(recent.isOpen() && p->highlighted() == last);
    entry->hoverChanged(false);  // the pointer moved into the submenu: the entry stays lit, the submenu stays
    CHECK(recent.isOpen() && p->highlighted() == last);
    r.file.close();
    r.show();
    p = r.popup();
    entry = static_cast<MenuItemWidget*>(p->children()[static_cast<std::size_t>(last)].get());
    entry->hoverChanged(true);
    r.h.clock.advance(100);
    entry->hoverChanged(false);  // left before the 250 ms: nothing opens afterwards
    CHECK(p->highlighted() == -1);
    r.h.clock.advance(400);
    CHECK(!recent.isOpen() && r.h.popups_.depth() == 1);
}

TEST(a_disabled_entry_is_exposed_disabled_to_ui_automation_and_an_enabled_one_is_not) {
    Rig r;
    r.show();
    MenuPopup* p = r.popup();
    CHECK(p->children()[0]->accessibleEnabled() && !p->children()[4]->accessibleEnabled());  // Open; Disabled
}

TEST(escape_closes_the_innermost_menu_first_and_then_the_menu) {
    Rig r;
    Menu recent("&Recent");
    recent.addAction(r.mgr.create("one"));
    r.file.addMenu(&recent);
    int closed = 0;
    r.file.on_closed = [&] { ++closed; };
    r.show();
    r.key(keys::End);
    r.key(keys::Right);
    CHECK(recent.isOpen());
    r.key(keys::Escape);
    CHECK(!recent.isOpen() && r.file.isOpen() && closed == 0);
    r.key(keys::Escape);
    CHECK(!r.file.isOpen() && closed == 1);
    r.show();
    r.key(keys::Left);  // at the top level with no menu bar: closes
    CHECK(!r.file.isOpen());
}

TEST(a_triggered_entry_of_a_submenu_closes_every_menu_and_runs) {
    Rig r;
    Menu recent("&Recent");
    Action* one = r.mgr.create("&one");
    one->on_triggered = [&](bool) { r.log_.push_back(std::string("one") + (r.file.isOpen() ? " [open]" : "")); };
    recent.addAction(one);
    r.file.addMenu(&recent);
    r.show();
    r.key(keys::End);
    r.key(keys::Right);
    r.key(keys::Return);
    CHECK_STR(r.take(), "one");
    CHECK(!r.file.isOpen() && !recent.isOpen() && r.h.popups_.depth() == 0);
}

TEST(the_pointer_highlights_an_entry_and_a_release_on_it_activates_it) {
    Rig r;
    r.show();
    MenuPopup* p = r.popup();
    auto* save = static_cast<MenuItemWidget*>(p->children()[1].get());
    save->hoverChanged(true);
    CHECK(p->highlighted() == 1);
    save->hoverChanged(false);  // the pointer left and nothing is open from it
    CHECK(p->highlighted() == -1);
    static_cast<MenuItemWidget*>(p->children()[4].get())->hoverChanged(true);  // the disabled one: no highlight
    CHECK(p->highlighted() == -1);
    static_cast<MenuItemWidget*>(p->children()[2].get())->hoverChanged(true);  // a separator
    CHECK(p->highlighted() == -1);
    UiMouseEvent e;
    e.button = MouseButton::Left;
    e.type = MouseType::Up;
    save->mouseEvent(e);  // a release with no press in this popup: nothing (the press was on the menu bar)
    CHECK(r.take().empty() && r.file.isOpen());
    e.type = MouseType::Down;
    save->mouseEvent(e);
    e.type = MouseType::Up;
    save->mouseEvent(e);
    CHECK_STR(r.take(), "Save");
    CHECK(!r.file.isOpen());
    r.show();
    auto* off = static_cast<MenuItemWidget*>(r.popup()->children()[4].get());
    e.type = MouseType::Down;
    off->mouseEvent(e);
    e.type = MouseType::Up;
    off->mouseEvent(e);
    CHECK(r.take().empty() && r.file.isOpen());  // a disabled entry does nothing
}

TEST(entries_paint_a_highlight_check_marks_dim_shortcuts_faint_disabled_text_and_an_arrow) {
    Rig r;
    Menu recent("&Recent");
    recent.addAction(r.mgr.create("one"));
    r.file.addMenu(&recent);
    r.log->setChecked(true);
    r.show();
    MenuPopup* p = r.popup();
    p->setHighlighted(1);
    auto paint = [&](int i) {
        auto rec = std::make_unique<RecordingPainter>();
        static_cast<MenuItemWidget*>(p->children()[static_cast<std::size_t>(i)].get())->paint(*rec);
        return rec;
    };
    auto hot = paint(1);
    bool fill = false, sc = false, text = false;
    for (const auto& op : hot->ops()) {
        if (op.kind == "fillRect" && op.color.b == token(T::Selection).b) fill = true;
        if (op.kind == "text" && op.text == "Ctrl+S") sc = op.style.halign == HAlign::Right && op.style.color.g == token(T::TextDim).g;
        if (op.kind == "text" && op.text == "Save") text = op.rect.x == 24;
    }
    CHECK(fill && sc && text);
    int lines = 0;
    for (const auto& op : paint(3)->ops()) lines += op.kind == "line";
    CHECK(lines == 2);  // the check mark of the checked entry
    bool faint = false;
    for (const auto& op : paint(4)->ops()) faint = faint || (op.kind == "text" && op.style.color.g == token(T::TextFaint).g);
    CHECK(faint);
    int sep = 0;
    for (const auto& op : paint(2)->ops()) sep += op.kind == "fillRect" && op.rect.height == 1;
    CHECK(sep == 1);  // a separator is one line
    int arrows = 0;
    for (const auto& op : paint(static_cast<int>(p->entries().size()) - 1)->ops()) arrows += op.kind == "polygon";
    CHECK(arrows == 1);  // a submenu entry has its triangle
    const HighContrast saved = highContrast();
    highContrast() = {true, Color::rgb(0x000000), Color::rgb(0xFFFFFF), Color::rgb(0x1AEBFF), Color::rgb(0x000000), Color::rgb(0x3FF23F)};
    bool black = false;
    for (const auto& op : paint(1)->ops()) black = black || (op.kind == "text" && op.text == "Save" && op.style.color.r == 0.0f);
    CHECK(black);  // high contrast: highlight text on the highlight
    highContrast() = saved;
}

TEST(an_open_menu_repaints_when_an_action_changes_and_forgets_it_when_it_closes) {
    Rig r;
    r.show();
    r.save->setText("&Save as");
    r.save->setEnabled(false);
    CHECK(!r.popup()->entryUsable(1));
    r.file.close();
    r.save->setText("&Save");  // no popup, no listener left to call: nothing breaks
    CHECK(true);
}

TEST(menu_entries_are_menu_items_with_invoke_toggle_and_expand_state) {
    Rig r;
    Menu recent("&Recent");
    recent.addAction(r.mgr.create("one"));
    r.file.addMenu(&recent);
    r.log->setChecked(true);
    r.show();
    MenuPopup* p = r.popup();
    auto item = [&](int i) { return static_cast<MenuItemWidget*>(p->children()[static_cast<std::size_t>(i)].get()); };
    CHECK(item(1)->accessibleToggleState() == -1 && item(3)->accessibleToggleState() == 1 && item(7)->accessibleExpandState() == 0);
    CHECK(item(1)->accessibleInvoke());
    CHECK_STR(r.take(), "Save");
    r.show();
    p = r.popup();
    CHECK(!item(4)->accessibleInvoke());  // disabled
    item(7)->accessibleExpand(true);
    CHECK(recent.isOpen() && item(7)->accessibleExpandState() == 1);
    item(7)->accessibleExpand(false);
    CHECK(!recent.isOpen() && item(7)->accessibleExpandState() == 0);
}

// -- the menu bar --------------------------------------------------------------------------------------------------

namespace {

struct BarRig {
    ~BarRig() { h.clearTree(); }  // the bar closes its menus: they must still exist
    Host h;
    ActionManager mgr{&h.router};
    MenuBar* bar;
    Menu file{"&File"}, view{"&View"}, run{"&Run"};
    std::vector<std::string> log;
    BarRig() {
        bar = h.root.addChild<MenuBar>();
        bar->setGeometry({0, 0, 400, 23});
        file.on_closed = [this] { log.push_back("file closed"); };  // (before the bar wraps it with its own)
        for (Menu* m : {&file, &view, &run}) {
            m->addAction(mgr.create("&One"));
            m->addAction(mgr.create("T&wo"));
            bar->addMenu(m);
        }
        bar->setGeometry({0, 0, 400, 23});  // (laid out again now that the titles are there)
    }
    std::string take() {
        std::string s;
        for (const auto& e : log) s += (s.empty() ? "" : ", ") + e;
        log.clear();
        return s;
    }
    void key(int vk, Mod m = Mod::None) { h.router.key(::tcad::ui::fake::key(vk, m)); }
    void keyUp(int vk) {
        KeyEvent e = ::tcad::ui::fake::key(vk);
        e.down = false;
        h.router.key(e);
    }
    MenuPopup* popup() { return asPopup(h.popups_.content()); }
    void layout() {
        if (popup()) layoutPopup(popup());
    }
};

}  // namespace

TEST(a_menu_bar_lays_its_titles_out_in_a_row_with_their_mnemonics) {
    BarRig r;
    CHECK(r.bar->count() == 3 && r.bar->sizeHint().height == 23);
    // "File" is 4 bytes = 24 DIPs + 8 each side = 40; "View" 40; "Run" 3 bytes = 18 + 16 = 34
    CHECK((r.bar->item(0)->geometry() == RectI{0, 0, 40, 23}) && (r.bar->item(1)->geometry() == RectI{40, 0, 40, 23}) && (r.bar->item(2)->geometry() == RectI{80, 0, 34, 23}));
    CHECK(r.bar->item(0)->accessibleName == "File" && r.bar->item(0)->mnemonic() == U'F' && r.bar->item(2)->mnemonic() == U'R');
    CHECK(r.bar->accessibleRole() == Role::MenuBar && r.bar->item(0)->accessibleRole() == Role::MenuItem);
    CHECK(r.bar->item(0)->accessibleExpandState() == 0);
    CHECK(r.bar->sizeHint().width == 114);
}

TEST(a_click_on_a_title_opens_its_menu_below_it_and_a_second_click_closes_it) {
    BarRig r;
    r.h.router.mouse(mouse(MouseType::Down, 20, 10, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 20, 10, 0));
    CHECK(r.file.isOpen() && r.bar->openIndex() == 0 && r.bar->isActive());
    CHECK(r.h.popups_.requests.back().anchor == r.bar->item(0) && r.h.popups_.requests.back().side == PopupSide::Below);
    CHECK(r.bar->item(0)->accessibleExpandState() == 1);
    r.h.router.mouse(mouse(MouseType::Down, 20, 10, kLeftBit));  // the filter closes it and swallows the press
    r.h.router.mouse(mouse(MouseType::Up, 20, 10, 0));
    CHECK(!r.file.isOpen() && !r.bar->isActive());
    CHECK_STR(r.take(), "file closed");
}

TEST(with_a_menu_open_the_pointer_over_another_title_switches_menus) {
    BarRig r;
    r.bar->openMenu(0);
    r.take();
    r.h.router.mouse(mouse(MouseType::Move, 60, 10, 0));  // over View
    CHECK(!r.file.isOpen() && r.view.isOpen() && r.bar->openIndex() == 1 && r.bar->activeIndex() == 1);
    CHECK_STR(r.take(), "file closed");
    CHECK(r.bar->isActive());  // switching is not leaving
    r.h.router.mouse(mouse(MouseType::Move, 90, 10, 0));
    CHECK(r.run.isOpen() && !r.view.isOpen());
}

TEST(a_lone_alt_activates_the_bar_and_the_arrows_enter_and_escape_work_it) {
    BarRig r;
    r.key(keys::Menu);
    CHECK(!r.bar->isActive());  // only on the release
    r.keyUp(keys::Menu);
    CHECK(r.bar->isActive() && r.bar->activeIndex() == 0 && r.bar->openIndex() == -1);
    r.key(keys::Right);
    CHECK(r.bar->activeIndex() == 1);
    r.key(keys::Right);
    r.key(keys::Right);
    CHECK(r.bar->activeIndex() == 0);  // wraps
    r.key(keys::Left);
    CHECK(r.bar->activeIndex() == 2);
    r.key(keys::Down);  // opens the menu with its first entry highlighted
    r.layout();
    CHECK(r.run.isOpen() && r.popup()->highlighted() == 0);
    r.key(keys::Escape);  // closes the menu and leaves the bar
    CHECK(!r.run.isOpen() && !r.bar->isActive());
    r.key(keys::Menu);
    r.keyUp(keys::Menu);
    r.key(keys::Menu);  // a second tap leaves
    r.keyUp(keys::Menu);
    CHECK(!r.bar->isActive());
    r.key(keys::Menu);
    r.keyUp(keys::Menu);
    r.key('V');  // a letter opens the menu with that mnemonic
    CHECK(r.view.isOpen());
    r.h.popups_.dismiss();
}

TEST(a_key_between_alt_down_and_up_is_not_a_tap) {
    BarRig r;
    r.key(keys::Menu);
    r.key('F', Mod::Alt);  // Alt+F: the mnemonic's business (below), not a tap
    r.keyUp(keys::Menu);
    CHECK(!r.bar->isActive() || r.file.isOpen());
    r.h.popups_.dismiss();
    BarRig q;
    q.key(keys::Menu);
    q.key(keys::Down);  // any other key
    q.keyUp(keys::Menu);
    CHECK(!q.bar->isActive());
    BarRig m;
    m.key(keys::Menu);
    m.h.router.mouse(mouse(MouseType::Down, 300, 200, kLeftBit));  // a click while Alt is held
    m.h.router.mouse(mouse(MouseType::Up, 300, 200, 0));
    m.keyUp(keys::Menu);
    CHECK(!m.bar->isActive());
    BarRig rep;  // auto-repeat of the Alt key is still one press
    rep.key(keys::Menu);
    KeyEvent again = ::tcad::ui::fake::key(keys::Menu);
    again.repeat = true;
    rep.h.router.key(again);
    rep.keyUp(keys::Menu);
    CHECK(rep.bar->isActive());
    BarRig mid;  // ...but a repeat after another key does not turn the lone Alt back on
    mid.key(keys::Menu);
    mid.key(keys::Down);
    mid.h.router.key(again);
    mid.keyUp(keys::Menu);
    CHECK(!mid.bar->isActive());
}

TEST(alt_and_a_letter_opens_that_menu_with_its_first_entry_highlighted) {
    BarRig r;
    r.key('V', Mod::Alt);
    r.layout();
    CHECK(r.view.isOpen() && r.popup() && r.popup()->highlighted() == 0);
    r.h.popups_.dismiss();
    CHECK(!r.view.isOpen() && !r.bar->isActive());
}

TEST(left_and_right_in_an_open_menu_go_to_the_neighbouring_titles_menu) {
    BarRig r;
    r.bar->openMenu(0, true);
    r.layout();
    r.key(keys::Right);
    CHECK(!r.file.isOpen() && r.view.isOpen());
    r.layout();
    CHECK(r.popup()->highlighted() == 0);  // the new menu opens with its first entry highlighted
    r.key(keys::Right);
    CHECK(r.run.isOpen());
    r.key(keys::Right);
    CHECK(r.file.isOpen());  // wraps
    r.key(keys::Left);
    CHECK(r.run.isOpen() && r.bar->activeIndex() == 2);
    r.h.popups_.dismiss();
}

TEST(a_menu_bar_item_is_invoked_and_expanded_through_ui_automation) {
    BarRig r;
    CHECK(r.bar->item(1)->accessibleInvoke() && r.view.isOpen());
    r.bar->item(1)->accessibleExpand(false);
    CHECK(!r.view.isOpen());
    r.bar->item(0)->accessibleExpand(true);
    CHECK(r.file.isOpen() && r.bar->item(0)->accessibleExpandState() == 1);
    r.h.popups_.dismiss();
}

TEST(a_menu_that_is_destroyed_while_open_or_a_bar_destroyed_with_it_open_is_safe) {
    auto host = std::make_unique<Host>();
    ActionManager mgr(&host->router);
    {
        Menu m("&M");
        m.addAction(mgr.create("x"));
        auto* bar = host->root.addChild<MenuBar>();
        bar->setGeometry({0, 0, 100, 23});
        bar->addMenu(&m);
        bar->openMenu(0);
        CHECK(m.isOpen());
        host->root.release(bar);  // the bar goes first, its menu still open
    }  // then the menu
    CHECK(!host->router.hasKeyFilter() && !host->router.hasPressFilter());
}

// -- the tool bar and the tool tip -----------------------------------------------------------------------------------

namespace {

struct ToolRig {
    ~ToolRig() { h.clearTree(); }  // the buttons stop listening to their actions: those must still exist
    Host h;
    ActionManager mgr{&h.router};
    ToolBar* tb;
    Action *run, *stop, *log;
    ToolRig() {
        tb = h.root.addChild<ToolBar>();
        tb->setGeometry({0, 0, 400, 30});
        run = mgr.create("&Run", "F5");
        stop = mgr.create("&Stop", "Shift+F5");
        stop->setEnabled(false);
        log = mgr.create("&Log scale");
        log->setCheckable(true);
        log->setToolTip("Plot on a log axis");
        tb->addAction(run);
        tb->addAction(stop);
        tb->addSeparator();
        tb->addAction(log);
        tb->setGeometry({0, 0, 400, 30});
    }
};

}  // namespace

TEST(a_tool_bar_lays_its_buttons_out_in_a_row_from_the_actions_text) {
    ToolRig r;
    CHECK(r.tb->buttonCount() == 3 && r.tb->accessibleRole() == Role::ToolBar);
    // margin 2; "Run" 3 bytes = 18 + 16 = 34 wide, 15 + 8 = 23 high; spacing 2; "Stop" 4 = 24 + 16 = 40; separator 7; "Log scale" 9 = 54 + 16 = 70
    ToolButton* b0 = r.tb->button(0);
    ToolButton* b1 = r.tb->button(1);
    CHECK(b0->geometry().x == 2 && b0->geometry().width == 34 && b0->geometry().height == 26);
    CHECK(b1->geometry().x == 2 + 34 + 2 && b1->geometry().width == 40);
    CHECK(r.tb->button(2)->geometry().x == 2 + 34 + 2 + 40 + 2 + 7 + 2);
    CHECK(r.tb->sizeHint().width == 2 + 34 + 2 + 40 + 2 + 7 + 2 + 70 + 2 && r.tb->sizeHint().height == 23 + 4);
    CHECK(r.tb->button(3) == nullptr);
}

TEST(a_button_follows_its_action_text_tool_tip_enabled_state_and_visibility) {
    ToolRig r;
    ToolButton* run = r.tb->button(0);
    CHECK_STR(run->accessibleName, "Run");  // no mnemonic marker
    CHECK_STR(run->toolTip, "Run (F5)");    // the text, then the shortcut
    CHECK_STR(r.tb->button(2)->toolTip, "Plot on a log axis");  // its own tool tip, and no shortcut to add
    CHECK(!r.tb->button(1)->isEnabled() && run->isEnabled());
    r.stop->setEnabled(true);
    CHECK(r.tb->button(1)->isEnabled());
    r.run->setText("&Run all");
    CHECK_STR(run->accessibleName, "Run all");
    CHECK(run->sizeHint().width == 7 * 6 + 16);
    r.run->setVisible(false);
    CHECK(!run->isVisibleSelf());
    CHECK(r.tb->button(1)->geometry().x == 2);  // the rest close up
    r.run->setVisible(true);
}

TEST(a_click_triggers_the_action_when_the_button_is_released_on_it) {
    ToolRig r;
    std::vector<std::string> log;
    r.run->on_triggered = [&](bool) { log.push_back("run"); };
    r.log->on_toggled = [&](bool c) { log.push_back(c ? "log on" : "log off"); };
    r.h.router.mouse(mouse(MouseType::Down, 10, 15, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 10, 15, 0));
    CHECK((log == std::vector<std::string>{"run"}));
    log.clear();
    r.h.router.mouse(mouse(MouseType::Down, 10, 15, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, 300, 15, kLeftBit));  // dragged off
    r.h.router.mouse(mouse(MouseType::Up, 300, 15, 0));
    CHECK(log.empty());  // released elsewhere: not a click
    const float x = static_cast<float>(r.tb->button(2)->geometry().x) + 10;
    r.h.router.mouse(mouse(MouseType::Down, x, 15, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, x, 15, 0));
    CHECK((log == std::vector<std::string>{"log on"}) && r.log->isChecked());
    log.clear();
    r.h.router.mouse(mouse(MouseType::Down, 50, 15, kLeftBit));  // Stop is disabled
    r.h.router.mouse(mouse(MouseType::Up, 50, 15, 0));
    CHECK(log.empty());
    CHECK(r.h.router.focusWidget() == nullptr);  // a click on a tool bar does not move the focus
}

TEST(a_tool_bar_is_one_tab_stop_and_the_arrow_keys_move_between_its_enabled_buttons) {
    ToolRig r;
    auto* after = r.h.root.addChild<Widget>();
    after->setFocusPolicy(FocusPolicy::Strong);
    after->setGeometry({0, 100, 50, 20});
    r.h.router.focusNext(true);
    CHECK(r.h.router.focusWidget() == r.tb->button(0));  // the first button
    r.h.router.focusNext(true);
    CHECK(r.h.router.focusWidget() == after);  // not the other buttons: one stop
    r.h.router.focusNext(false);
    CHECK(r.h.router.focusWidget() == r.tb->button(0));
    r.h.router.key(key(keys::Right));
    CHECK(r.h.router.focusWidget() == r.tb->button(2));  // Stop is disabled: skipped
    CHECK(r.tb->currentButton() == r.tb->button(2));
    r.h.router.key(key(keys::Right));
    CHECK(r.h.router.focusWidget() == r.tb->button(0));  // wraps
    r.h.router.key(key(keys::Left));
    CHECK(r.h.router.focusWidget() == r.tb->button(2));
    // the stop moves with the roving button
    r.h.router.focusNext(true);
    r.h.router.focusNext(false);
    CHECK(r.h.router.focusWidget() == r.tb->button(2));
    std::vector<std::string> log;
    r.log->on_toggled = [&](bool c) { log.push_back(c ? "on" : "off"); };
    r.h.router.key(key(keys::Space));
    CHECK((log == std::vector<std::string>{"on"}));
    r.h.router.key(key(keys::Return));
    CHECK((log == std::vector<std::string>{"on", "off"}));
}

TEST(the_left_arrow_in_a_tool_bar_goes_back_one_button_and_wraps_to_the_last) {
    ToolRig r;
    r.stop->setEnabled(true);  // three enabled buttons: back and forward differ
    r.h.router.focusNext(true);
    CHECK(r.h.router.focusWidget() == r.tb->button(0));
    r.h.router.key(key(keys::Left));
    CHECK(r.h.router.focusWidget() == r.tb->button(2));
    r.h.router.key(key(keys::Left));
    CHECK(r.h.router.focusWidget() == r.tb->button(1));
    r.h.router.key(key(keys::Right));
    CHECK(r.h.router.focusWidget() == r.tb->button(2));
}

TEST(a_tool_bar_reports_buttons_to_ui_automation_with_invoke_and_toggle) {
    ToolRig r;
    ToolButton* log = r.tb->button(2);
    CHECK(log->accessibleRole() == Role::Button && log->accessibleToggleState() == 0 && r.tb->button(0)->accessibleToggleState() == -1);
    CHECK(log->accessibleInvoke() && r.log->isChecked() && log->accessibleToggleState() == 1);
    CHECK(!r.tb->button(1)->accessibleInvoke());  // disabled
    log->accessibleToggle();
    CHECK(!r.log->isChecked());
}

TEST(a_tool_bar_can_hold_a_widget_and_a_destroyed_bar_stops_listening_to_its_actions) {
    Host h;
    ActionManager mgr(&h.router);
    Action* a = mgr.create("x");
    {
        auto* tb = h.root.addChild<ToolBar>();
        tb->setGeometry({0, 0, 300, 30});
        tb->addAction(a);
        Widget* w = tb->addWidget(std::make_unique<Widget>());
        w->setMinimumSize({50, 20});
        CHECK(w != nullptr && tb->buttonCount() == 1);
        h.root.release(tb);
    }
    a->setText("still fine");  // no listener left pointing at the dead button
    CHECK(true);
}

TEST(a_tool_tip_label_is_one_line_when_short_and_wraps_at_320_dips) {
    Host h;
    auto* t1 = h.root.addChild<ToolTipLabel>("Run the solver");
    // 14 bytes = 84 DIPs, plus 6 each side
    CHECK(t1->sizeHint().width == 84 + 12 && t1->accessibleRole() == Role::Text);
    CHECK(t1->sizeHint().height == 15 + 12 - 2);  // one 15-DIP line, 6 DIPs each side, the text drawn 1 DIP up
    auto* t2 = h.root.addChild<ToolTipLabel>(std::string(200, 'x'));  // 1200 DIPs of text
    CHECK(t2->sizeHint().width == 320);
    RecordingPainter p;
    t1->setGeometry({0, 0, 96, 22});
    t1->paint(p);
    bool text = false, frame = false;
    for (const auto& op : p.ops()) {
        if (op.kind == "text" && op.text == "Run the solver") text = true;
        if (op.kind == "strokeRect") frame = true;
    }
    CHECK(text && frame);
}

// -- the edit context menu ---------------------------------------------------------------------------------------------

TEST(the_edit_menu_has_six_commands_enabled_from_the_state_asked_each_time_it_opens_and_runs_them) {
    Host h;
    auto* owner = h.root.addChild<Widget>();
    owner->setGeometry({20, 30, 100, 20});
    EditContextMenu::State st;
    std::vector<EditContextMenu::Command> ran;
    int asked = 0;
    EditContextMenu m(EditContextMenu::Kind::Edit, [&] { ++asked; return st; }, [&](EditContextMenu::Command c) { ran.push_back(c); });
    st.paste = true;
    st.select_all = true;
    CHECK(m.show(owner, {5, 6}) && asked == 1);
    CHECK(h.popups_.requests.back().side == PopupSide::AtPoint && h.popups_.requests.back().point == (PointF{25, 36}));  // window DIPs
    MenuPopup* p = asPopup(h.popups_.content());
    CHECK(p != nullptr && p->entries().size() == 8);  // Undo, sep, Cut, Copy, Paste, Delete, sep, Select All
    if (!p) return;
    CHECK(!p->entryUsable(0) && !p->entryUsable(2) && !p->entryUsable(3) && p->entryUsable(4) && !p->entryUsable(5) && p->entryUsable(7));
    CHECK(p->entryText(0) == "Undo" && p->entryShortcut(0) == "Ctrl+Z" && p->entryShortcut(5).empty() && p->entryShortcut(7) == "Ctrl+A");
    CHECK(p->activate(4) && ran.size() == 1 && ran[0] == EditContextMenu::Command::Paste);
    st = {};
    st.undo = st.cut = st.copy = st.del = true;  // a different state the second time
    CHECK(m.show(owner, {5, 6}) && asked == 2);
    p = asPopup(h.popups_.content());
    CHECK(p && p->entryUsable(0) && p->entryUsable(2) && p->entryUsable(3) && !p->entryUsable(4) && p->entryUsable(5) && !p->entryUsable(7));
    for (int i : {0, 2, 3, 5}) {
        CHECK(p && p->activate(i));
        m.show(owner, {5, 6});
        p = asPopup(h.popups_.content());
    }
    using C = EditContextMenu::Command;
    CHECK((ran == std::vector<C>{C::Paste, C::Undo, C::Cut, C::Copy, C::Delete}));
}

TEST(a_label_menu_has_only_copy_and_select_all) {
    Host h;
    auto* owner = h.root.addChild<Widget>();
    owner->setGeometry({0, 0, 100, 20});
    EditContextMenu::State st;
    st.copy = false;
    st.select_all = true;
    std::vector<EditContextMenu::Command> ran;
    EditContextMenu m(EditContextMenu::Kind::Label, [&] { return st; }, [&](EditContextMenu::Command c) { ran.push_back(c); });
    CHECK(m.show(owner, {0, 0}));
    MenuPopup* p = asPopup(h.popups_.content());
    CHECK(p != nullptr && p->entries().size() == 3);  // Copy, separator, Select All
    if (!p) return;
    CHECK(!p->entryUsable(0) && p->entryUsable(2) && p->entryText(0) == "Copy" && p->entryText(2) == "Select All");
    CHECK(p->activate(2) && ran.size() == 1 && ran[0] == EditContextMenu::Command::SelectAll);
}

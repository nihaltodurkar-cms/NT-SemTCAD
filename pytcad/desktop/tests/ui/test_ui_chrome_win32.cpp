// N3f on real windows (NATIVE-DESKTOP-PLAN.md 27.8.7): the menu bar, the menus, the tool bar, tabs, the splitter, the
// message box and the edit context menus drawn through UiWindow + Direct2D on WARP -- goldens at 100/150/200% in the light
// theme and in high contrast (there is no dark theme) -- and driven by REAL window messages: a click on a title opens a
// menu in its own popup window, a click in that window triggers the action, the Alt tap, submenus, tool tips after the
// pointer rests, the modal message box's nested loop answered by a real key, the box's close button, the live high-contrast
// re-read, the right-click edit menu, and UI Automation through the real client (the menu's items). Part of
// tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "platform/app.hpp"
#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/action.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/menu.hpp"
#include "ui/widgets/menu_bar.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/splitter.hpp"
#include "ui/widgets/tab_widget.hpp"
#include "ui/widgets/tool_bar.hpp"
#include "ui/win32/clipboard.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/message_box_window.hpp"
#include "ui/win32/popup_window.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;
using tcad::platform::Mod;
using tcad::platform::MouseButton;

namespace {

constexpr float kW = 420, kH = 240;

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

// The scene: a menu bar (File with a submenu, a check entry and a disabled one; View), a tool bar, and under them a splitter
// holding an edit and a selectable label on the left and a tab widget on the right.
struct ChromeWindow {
    std::unique_ptr<UiWindow> w;
    ActionManager* mgr = nullptr;
    std::unique_ptr<ActionManager> mgr_owner;
    Action *open = nullptr, *save = nullptr, *quit = nullptr, *log = nullptr, *off = nullptr, *recent1 = nullptr, *run = nullptr;
    Menu file{"&File"}, view{"&View"}, recent{"&Recent"};
    MenuBar* bar = nullptr;
    ToolBar* tools = nullptr;
    Splitter* split = nullptr;
    TabWidget* tabs = nullptr;
    LineEdit* edit = nullptr;
    Label* note = nullptr;
    std::vector<std::string> events;

    // The widgets go first (they listen to the actions), then the manager (it releases its keys in the window's router),
    // then the window: members are destroyed in reverse order, so only the widget tree needs doing here.
    ~ChromeWindow() {
        if (!w) return;
        Widget& root = w->root();
        while (!root.children().empty()) root.release(root.children().front().get());
    }
    explicit ChromeWindow(double scale = 1.0) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_chrome_tests", .width = 420, .height = 240, .scale_override = scale});
        if (!r) {
            std::printf("  UiWindow::create: %s\n", r.error().c_str());
            return;
        }
        w = std::move(*r);
        w->resizeClient(px(kW, scale), px(kH, scale));
        SetWindowPos(w->window().hwnd(), nullptr, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        w->router().setAlwaysShowCues(false);
        mgr_owner = std::make_unique<ActionManager>(&w->router());
        mgr = mgr_owner.get();
        open = mgr->create("&Open...", "Ctrl+O");
        save = mgr->create("&Save", "Ctrl+S");
        quit = mgr->create("&Quit", "Ctrl+Q");
        log = mgr->create("&Log scale");
        log->setCheckable(true);
        log->setChecked(true);
        off = mgr->create("Disa&bled");
        off->setEnabled(false);
        recent1 = mgr->create("mosfet_2d.npz");
        run = mgr->create("&Run", "F5");
        for (Action* a : {open, save, quit, log, off, recent1, run}) a->on_triggered = [this, a](bool) { events.push_back(a->text()); };
        recent.addAction(recent1);
        file.addAction(open);
        file.addAction(save);
        file.addMenu(&recent);
        file.addSeparator();
        file.addAction(log);
        file.addAction(off);
        file.addSeparator();
        file.addAction(quit);
        view.addAction(run);
        Widget& root = w->root();
        auto* col = root.setLayout<BoxLayout>(Orientation::Vertical);
        bar = col->add<MenuBar>();
        bar->name = "bar";
        bar->addMenu(&file);
        bar->addMenu(&view);
        tools = col->add<ToolBar>();
        tools->name = "tools";
        tools->addAction(open);
        tools->addAction(save);
        tools->addSeparator();
        tools->addAction(run);
        open->setToolTip("Open a project (Ctrl+O)");
        split = root.addChild<Splitter>(Orientation::Horizontal);
        col->addWidget(split, 1);
        split->name = "split";
        auto left = std::make_unique<Widget>();
        auto* lbox = left->setLayout<BoxLayout>(Orientation::Vertical);
        edit = left->addChild<LineEdit>(w->window().hwnd());
        lbox->addWidget(edit);
        edit->name = "edit";
        edit->setText("Boltzmann statistics");
        note = lbox->add<Label>("Selectable note");
        lbox->addStretch(1);
        note->name = "note";
        note->setSelectable(true);
        split->addWidget(std::move(left));
        auto t = std::make_unique<TabWidget>();
        tabs = t.get();
        tabs->name = "tabs";
        tabs->addTab(std::make_unique<Label>("Structure page"), "&Structure");
        tabs->addTab(std::make_unique<Label>("Process page"), "&Process");
        tabs->addTab(std::make_unique<Label>("Catalog page"), "&Catalog");
        split->addWidget(std::move(t));
        w->renderNow(nullptr, false);
    }
    PopupWindowService& popups() { return w->popupService(); }
    UiWindow* popupWindow() { return popups().currentWindow(); }
    PointF centre(Widget* x) const {
        const RectI r = x->windowRect();
        return {static_cast<float>((r.x + r.width / 2.0) / w->scale()), static_cast<float>((r.y + r.height / 2.0) / w->scale())};
    }
    // the centre of entry `i` of the menu popup open in window `pw` (the popup's own window DIPs)
    static PointF entryCentre(UiWindow& pw, int i) {
        auto* mp = dynamic_cast<MenuPopup*>(pw.root().children().front().get());
        const RectI r = mp->windowRect();
        return {static_cast<float>(r.x / pw.scale()) + mp->sizeDips().width / 2, static_cast<float>(r.y / pw.scale()) + mp->rowTop(i) + mp->rowHeight(i) / 2};
    }
    std::string take() {
        std::string s;
        for (const auto& e : events) s += (s.empty() ? "" : ", ") + e;
        events.clear();
        return s;
    }
};

// -- UI Automation helpers ---------------------------------------------------------------------------------------------

ComPtr<IUIAutomation> client() {
    ComPtr<IUIAutomation> a;
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&a));
    return a;
}

std::vector<ComPtr<IUIAutomationElement>> ofType(IUIAutomation* a, IUIAutomationElement* parent, CONTROLTYPEID t) {
    std::vector<ComPtr<IUIAutomationElement>> out;
    ComPtr<IUIAutomationCondition> c;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = t;
    a->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &c);
    ComPtr<IUIAutomationElementArray> arr;
    if (FAILED(parent->FindAll(TreeScope_Descendants, c.Get(), &arr)) || !arr) return out;
    int n = 0;
    arr->get_Length(&n);
    for (int i = 0; i < n; ++i) {
        ComPtr<IUIAutomationElement> e;
        arr->GetElement(i, &e);
        out.push_back(e);
    }
    return out;
}

std::string nameOf(IUIAutomationElement* e) {
    BSTR b = nullptr;
    e->get_CurrentName(&b);
    std::string s = b ? narrowW(b) : std::string();
    SysFreeString(b);
    return s;
}

// -- goldens ---------------------------------------------------------------------------------------------------------

TEST(chrome_matches_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        ChromeWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) return;
        f.edit->setFocus(FocusReason::Tab);
        f.edit->setCaretBlinking(false);
        f.edit->model().selectAll();
        f.tabs->setTabEnabled(2, false);
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        CHECK(img.width == px(kW, pct / 100.0) && img.height == px(kH, pct / 100.0));
        const GoldenResult r = checkGolden(*warp(), "n3f_chrome@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}

TEST(chrome_in_high_contrast_matches_the_goldens) {
    const HighContrast saved = highContrast();
    for (int pct : {100, 150, 200}) {
        ChromeWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) break;
        nightSky();  // after the window exists: UiWindow::create reads the system's own state
        f.edit->setFocus(FocusReason::Tab);
        f.edit->setCaretBlinking(false);
        f.edit->model().selectAll();
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n3f_chrome_hc@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
    highContrast() = saved;
}

TEST(an_open_menu_matches_the_goldens_at_three_scales_in_light_and_in_high_contrast) {
    const HighContrast saved = highContrast();
    for (bool hc : {false, true}) {
        for (int pct : {100, 150, 200}) {
            ChromeWindow f(pct / 100.0);
            CHECK(f.w != nullptr);
            if (!f.w) return;
            if (hc) nightSky();
            f.bar->openMenu(0, true);  // the first entry highlighted
            UiWindow* pw = f.popupWindow();
            CHECK(pw != nullptr);
            if (!pw) break;
            Image img;
            pw->renderNow(&img, false);
            const GoldenResult r = checkGolden(*warp(), std::string(hc ? "n3f_menu_hc@" : "n3f_menu@") + std::to_string(pct) + ".png", img);
            CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
            f.popups().dismissAll();
        }
    }
    highContrast() = saved;
}

// -- menus in real popup windows ------------------------------------------------------------------------------------

TEST(a_click_on_a_title_opens_the_menu_in_its_own_window_and_a_click_in_it_runs_the_action) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.centre(f.bar->item(0)));
    CHECK(f.file.isOpen() && f.popups().depth() == 1);
    UiWindow* pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    CHECK(IsWindowVisible(pw->window().hwnd()) != FALSE);
    const LONG ex = GetWindowLongW(pw->window().hwnd(), GWL_EXSTYLE);
    CHECK((ex & WS_EX_NOACTIVATE) != 0);  // the menu never takes the activation from the window
    Driver pd(*pw);
    pd.click(ChromeWindow::entryCentre(*pw, 1));  // Save
    CHECK_STR(f.take(), "&Save");
    CHECK(!f.file.isOpen() && f.popups().depth() == 0);
    // a disabled entry: the click is taken, nothing runs, the menu stays
    d.click(f.centre(f.bar->item(0)));
    pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    Driver pd2(*pw);
    pd2.click(ChromeWindow::entryCentre(*pw, 5));  // Disabled (entries: Open, Save, Recent, sep, Log, Disabled, sep, Quit)
    CHECK(f.take().empty() && f.file.isOpen());
    // a press outside closes it
    d.click({300, 200});
    CHECK(!f.file.isOpen() && f.popups().depth() == 0);
}

TEST(the_alt_tap_activates_the_bar_the_arrows_walk_it_and_escape_leaves)  {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.key(VK_MENU, Mod::Alt);  // Alt down and up with nothing between: a tap
    CHECK(f.bar->isActive() && f.bar->activeIndex() == 0);
    d.key(VK_RIGHT);
    CHECK(f.bar->activeIndex() == 1);
    d.key(VK_LEFT);
    d.key(VK_DOWN);  // opens the first menu with its first entry highlighted
    CHECK(f.file.isOpen());
    d.key(VK_RIGHT);  // an entry with no submenu: on to the next title
    CHECK(!f.file.isOpen() && f.view.isOpen());
    d.key(VK_ESCAPE);
    CHECK(!f.view.isOpen());
    d.key(VK_ESCAPE);
    CHECK(!f.bar->isActive());
    // Alt+F opens File at once, and Enter on the highlighted entry runs it
    d.key('F', Mod::Alt);
    CHECK(f.file.isOpen());
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "&Open...");
    CHECK(!f.file.isOpen());
}

TEST(a_submenu_opens_in_a_second_window_beside_the_first_and_escape_closes_one_level) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.bar->openMenu(0, true);
    d.key(VK_DOWN);  // Save
    d.key(VK_DOWN);  // Recent (a submenu)
    d.key(VK_RIGHT);
    CHECK(f.popups().depth() == 2 && f.recent.isOpen());
    const auto ws = f.popups().windows();
    CHECK(ws.size() == 2);
    if (ws.size() == 2) {
        RECT a{}, b{};
        GetWindowRect(ws[0]->window().hwnd(), &a);
        GetWindowRect(ws[1]->window().hwnd(), &b);
        CHECK(b.left >= a.right - 8 && b.left <= a.right + 8);  // beside it, at its right edge (a little overlap)
    }
    d.key(VK_ESCAPE);
    CHECK(f.popups().depth() == 1 && f.file.isOpen() && !f.recent.isOpen());
    d.key(VK_ESCAPE);
    CHECK(f.popups().depth() == 0);
}

TEST(a_tool_tip_appears_where_the_pointer_rests_and_goes_when_it_leaves_or_a_press_comes) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.w->router().setTooltipDelayMs(60);
    d.move(f.centre(f.tools->button(0)));
    pumpFor(250);
    UiWindow* tip = f.popups().toolTipWindow();
    CHECK(tip != nullptr && IsWindowVisible(tip->window().hwnd()) != FALSE);
    if (tip) {  // below and to the right of the pointer, so the cursor image does not cover it
        POINT at{0, 0};
        ClientToScreen(f.w->window().hwnd(), &at);
        const PointF c = f.centre(f.tools->button(0));
        at.x += static_cast<LONG>(std::lround(c.x * f.w->scale()));
        at.y += static_cast<LONG>(std::lround(c.y * f.w->scale()));
        RECT tr{};
        GetWindowRect(tip->window().hwnd(), &tr);
        CHECK(tr.left >= at.x + 10 && tr.top >= at.y + 12);
    }
    CHECK(f.popups().depth() == 0);  // a tool tip is not in the menu stack
    d.press(f.centre(f.tools->button(0)));
    d.release(f.centre(f.tools->button(0)));
    CHECK(f.popups().toolTipWindow() == nullptr || IsWindowVisible(f.popups().toolTipWindow()->window().hwnd()) == FALSE);
}

// -- tabs and the splitter with real messages -----------------------------------------------------------------------

TEST(a_tab_click_selects_and_a_splitter_drag_moves_the_handle) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.centre(f.tabs->tabButton(1)));
    CHECK(f.tabs->currentIndex() == 1);
    const int before = f.split->sizes()[0];
    const PointF h = f.centre(f.split->handle(0));
    d.press(h);
    d.move({h.x + 40, h.y});
    d.release({h.x + 40, h.y});
    CHECK(f.split->sizes()[0] > before + 20);
    CHECK(f.split->sizes()[0] + f.split->sizes()[1] == static_cast<int>(std::lround(f.split->sizeDips().width - f.split->handleWidth())));
}

// -- the message box ------------------------------------------------------------------------------------------------

TEST(a_modal_message_box_disables_its_owner_runs_a_nested_loop_and_returns_the_pressed_button) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    MessageBoxSpec spec;
    spec.icon = MessageIcon::Question;
    spec.title = "Unsaved changes";
    spec.text = "Save the project before closing?";
    spec.buttons = {StandardButton::Save, StandardButton::Discard, StandardButton::Cancel};
    bool owner_was_disabled = false, box_was_active = false;
    app().post([&] {
        MessageBoxWindow* mb = MessageBoxWindow::active();
        box_was_active = mb != nullptr && mb->isModal();
        owner_was_disabled = IsWindowEnabled(f.w->window().hwnd()) == FALSE;
        if (mb) Driver(mb->window()).key(VK_RETURN);  // the default button: Save
    });
    const StandardButton b = MessageBoxWindow::exec(*f.w, spec);
    CHECK(box_was_active && owner_was_disabled);
    CHECK(b == StandardButton::Save);
    CHECK(IsWindowEnabled(f.w->window().hwnd()) != FALSE && MessageBoxWindow::openCount() == 0);
    // Escape: the Cancel button
    app().post([&] {
        if (MessageBoxWindow* mb = MessageBoxWindow::active()) Driver(mb->window()).key(VK_ESCAPE);
    });
    CHECK(MessageBoxWindow::exec(*f.w, spec) == StandardButton::Cancel);
    // a click on a button
    app().post([&] {
        MessageBoxWindow* mb = MessageBoxWindow::active();
        if (!mb) return;
        PushButton* pb = mb->content().button(StandardButton::Discard);
        const RectI r = pb->windowRect();
        Driver dd(mb->window());
        const PointF p{static_cast<float>((r.x + r.width / 2.0) / mb->window().scale()), static_cast<float>((r.y + r.height / 2.0) / mb->window().scale())};
        dd.click(p);
    });
    CHECK(MessageBoxWindow::exec(*f.w, spec) == StandardButton::Discard);
}

TEST(a_non_modal_message_box_leaves_the_owner_enabled_and_its_close_button_presses_escape) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    MessageBoxSpec spec;
    spec.icon = MessageIcon::Information;
    spec.title = "Done";
    spec.text = "The run finished.";
    spec.buttons = {StandardButton::Ok};
    StandardButton got = StandardButton::None;
    MessageBoxWindow* mb = MessageBoxWindow::show(*f.w, spec, [&](StandardButton b) { got = b; });
    CHECK(mb != nullptr && !mb->isModal());
    if (!mb) return;
    CHECK(IsWindowEnabled(f.w->window().hwnd()) != FALSE && MessageBoxWindow::openCount() == 1);
    SendMessageW(mb->window().window().hwnd(), WM_CLOSE, 0, 0);  // the close button: the only button is the escape button
    CHECK(got == StandardButton::Ok);
    pumpFor(50);
    CHECK(MessageBoxWindow::openCount() == 0);
    // Yes/No: the escape button is No (Qt's rule), so the close button answers No and the box is done
    spec.buttons = {StandardButton::Yes, StandardButton::No};
    StandardButton got2 = StandardButton::None;
    MessageBoxWindow* mb2 = MessageBoxWindow::show(*f.w, spec, [&](StandardButton b) { got2 = b; });
    CHECK(mb2 != nullptr);
    if (!mb2) return;
    SendMessageW(mb2->window().window().hwnd(), WM_CLOSE, 0, 0);
    CHECK(got2 == StandardButton::No);
    pumpFor(50);
    CHECK(MessageBoxWindow::openCount() == 0);
    // no escape button at all (Save/Discard): the close button is ignored until a button is pressed
    spec.buttons = {StandardButton::Save, StandardButton::Discard};
    StandardButton got3 = StandardButton::None;
    MessageBoxWindow* mb3 = MessageBoxWindow::show(*f.w, spec, [&](StandardButton b) { got3 = b; });
    CHECK(mb3 != nullptr);
    if (!mb3) return;
    SendMessageW(mb3->window().window().hwnd(), WM_CLOSE, 0, 0);
    CHECK(!mb3->isDone() && got3 == StandardButton::None && MessageBoxWindow::openCount() == 1);
    mb3->content().press(StandardButton::Save);
    CHECK(got3 == StandardButton::Save);
    pumpFor(50);
}

// -- the system's colours changing while running -------------------------------------------------------------------

TEST(a_system_colour_change_is_re_read_and_closes_the_open_popups) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    f.bar->openMenu(0, true);
    CHECK(f.popups().depth() == 1);
    const int before = f.w->paletteChanges();
    SendMessageW(f.w->window().hwnd(), WM_SYSCOLORCHANGE, 0, 0);
    CHECK(f.w->paletteChanges() == before + 1);
    CHECK(f.popups().depth() == 0);
    CHECK(!f.file.isOpen() && !f.bar->isActive());  // the menu was told it was dismissed
}

TEST(showing_a_second_popup_dismisses_the_first_and_tells_its_opener) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    CHECK(f.file.showBelow(f.bar, f.bar->item(0)) && f.file.isOpen());
    CHECK(f.view.showBelow(f.bar, f.bar->item(1)));  // not a child: the File menu goes
    CHECK(f.popups().depth() == 1 && !f.file.isOpen() && f.view.isOpen());
    f.popups().dismissAll();
    CHECK(f.popups().depth() == 0 && !f.view.isOpen());
}

// -- the edit context menus ----------------------------------------------------------------------------------------

TEST(a_right_click_in_an_edit_opens_the_edit_menu_and_its_entries_work) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.edit->setCaretBlinking(false);
    const PointF c = f.centre(f.edit);
    // an empty clipboard: Paste is not offered
    if (OpenClipboard(f.w->window().hwnd())) {
        EmptyClipboard();
        CloseClipboard();
    }
    CHECK(f.w->router().focusWidget() != f.edit);
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    CHECK(f.popups().depth() == 1 && f.w->router().focusWidget() == f.edit);  // the right click focuses the edit, as the left does
    if (UiWindow* ew = f.popupWindow()) {
        auto* ep = dynamic_cast<MenuPopup*>(ew->root().children().front().get());
        CHECK(ep != nullptr && !ep->entryUsable(4));
    }
    d.key(VK_ESCAPE);
    CHECK(f.popups().depth() == 0);
    setClipboardText(f.w->window().hwnd(), "pasted");
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    CHECK(f.popups().depth() == 1);
    UiWindow* pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    auto* mp = dynamic_cast<MenuPopup*>(pw->root().children().front().get());
    CHECK(mp != nullptr);
    if (!mp) return;
    // Undo, sep, Cut, Copy, Paste, Delete, sep, Select All: with no selection only Paste and Select All are usable
    CHECK(mp->entries().size() == 8);
    CHECK(!mp->entryUsable(0) && !mp->entryUsable(2) && !mp->entryUsable(3) && mp->entryUsable(4) && !mp->entryUsable(5) && mp->entryUsable(7));
    Driver pd(*pw);
    pd.click(ChromeWindow::entryCentre(*pw, 7));  // Select All
    CHECK(f.popups().depth() == 0);
    CHECK_STR(f.edit->model().selectedText(), "Boltzmann statistics");
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    Driver pd2(*pw);
    pd2.click(ChromeWindow::entryCentre(*pw, 4));  // Paste: replaces the selection
    CHECK_STR(f.edit->text(), "pasted");
    // Undo puts the old text back
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    Driver pd3(*pw);
    pd3.click(ChromeWindow::entryCentre(*pw, 0));
    CHECK_STR(f.edit->text(), "Boltzmann statistics");
    // Delete removes the selection
    f.edit->setFocus(FocusReason::Tab);
    d.key('A', Mod::Ctrl);
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    Driver pd4(*pw);
    pd4.click(ChromeWindow::entryCentre(*pw, 5));
    CHECK_STR(f.edit->text(), "");
    f.edit->setText("Boltzmann statistics");
    // the keyboard way: the Menu key opens it at the edit; Escape closes it
    f.edit->setFocus(FocusReason::Tab);
    d.key(VK_APPS);
    CHECK(f.popups().depth() == 1);
    d.key(VK_ESCAPE);
    CHECK(f.popups().depth() == 0);
    d.key(VK_F10, Mod::Shift);  // the other keyboard way
    CHECK(f.popups().depth() == 1);
    d.key(VK_ESCAPE);
    CHECK(f.popups().depth() == 0);
}

TEST(a_selectable_label_has_a_copy_and_select_all_menu) {
    ChromeWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    const PointF c = f.centre(f.note);
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    CHECK(f.popups().depth() == 1);
    UiWindow* pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    auto* mp = dynamic_cast<MenuPopup*>(pw->root().children().front().get());
    CHECK(mp != nullptr && mp->entries().size() == 3);  // Copy, separator, Select All
    if (!mp) return;
    CHECK(!mp->entryUsable(0) && mp->entryUsable(2));
    Driver pd(*pw);
    pd.click(ChromeWindow::entryCentre(*pw, 2));
    CHECK_STR(f.note->selectedText(), "Selectable note");
    d.press(c, MouseButton::Right);
    d.release(c, MouseButton::Right);
    pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    Driver pd2(*pw);
    pd2.click(ChromeWindow::entryCentre(*pw, 0));  // Copy
    CHECK_STR(clipboardText(f.w->window().hwnd()).value_or(""), "Selectable note");
}

// -- UI Automation ---------------------------------------------------------------------------------------------------

TEST(uia_sees_the_menu_bar_its_titles_and_an_open_menus_items) {
    ChromeWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto bars = ofType(a.Get(), win.Get(), UIA_MenuBarControlTypeId);
    ComPtr<IUIAutomationElement> ours;  // the window's own system menu is a menu bar too
    for (auto& b : bars)
        if (nameOf(b.Get()) == "Menu bar") ours = b;
    CHECK(ours != nullptr);
    auto titles = !ours ? std::vector<ComPtr<IUIAutomationElement>>{} : ofType(a.Get(), ours.Get(), UIA_MenuItemControlTypeId);
    CHECK(titles.size() == 2);
    if (titles.size() == 2) CHECK_STR(nameOf(titles[0].Get()), "File");
    // expand File through ExpandCollapse: the menu opens in its own window
    if (titles.size() == 2) {
        ComPtr<IUIAutomationExpandCollapsePattern> ec;
        CHECK(SUCCEEDED(titles[0]->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&ec))) && ec);
        if (ec) {
            CHECK(SUCCEEDED(ec->Expand()));
            CHECK(f.file.isOpen());
        }
    }
    UiWindow* pw = f.popupWindow();
    CHECK(pw != nullptr);
    if (!pw) return;
    ComPtr<IUIAutomationElement> pe;
    a->ElementFromHandle(pw->window().hwnd(), &pe);
    auto items = ofType(a.Get(), pe.Get(), UIA_MenuItemControlTypeId);
    CHECK(items.size() == 6);  // Open, Save, Recent, Log scale, Disabled, Quit (separators are not items)
    if (items.size() == 6) {
        CHECK_STR(nameOf(items[0].Get()), "Open...");
        CHECK_STR(nameOf(items[4].Get()), "Disabled");
        BOOL enabled = TRUE;
        items[4]->get_CurrentIsEnabled(&enabled);
        CHECK(!enabled);
        // Invoke runs the action and closes the menu
        ComPtr<IUIAutomationInvokePattern> inv;
        CHECK(SUCCEEDED(items[1]->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv);
        if (inv) {
            CHECK(SUCCEEDED(inv->Invoke()));
            CHECK_STR(f.take(), "&Save");
        }
    }
}

}  // namespace

// N3c on real windows (NATIVE-DESKTOP-PLAN.md 27.8.4): combo boxes and their drop-downs in REAL popup windows --
// goldens of the closed combos and of the popup's own frame at 100/150/200% (light and high contrast); the popup's
// window style (borderless, owned, never activated, not in the taskbar), its placement on the monitor's work area (below,
// or flipped above near the bottom), the owner keeping the keyboard focus, clicks, keys, typeahead and the wheel through
// REAL messages to the right window, dismissal (a press elsewhere, deactivation, moving, resizing, the owner's
// destruction), and UI Automation through the real client (ComboBox with Value and ExpandCollapse, the highlight
// announced). Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/combo_box.hpp"
#include "ui/win32/popup_window.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::platform::Mod;

namespace {

constexpr float kW = 400, kH = 200;

struct ComboWindow {
    std::unique_ptr<UiWindow> w;
    ComboBox *stats = nullptr, *cmap = nullptr, *backend = nullptr, *off = nullptr;
    PushButton* run = nullptr;
    std::vector<std::string> log;

    explicit ComboWindow(std::optional<double> scale = std::nullopt) {
        app();
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_combo_tests", .width = 400, .height = 200, .scale_override = scale});
        if (!r) {
            std::printf("  UiWindow::create: %s\n", r.error().c_str());
            return;
        }
        w = std::move(*r);
        const double s = scale.value_or(w->scale());
        w->resizeClient(px(kW, s), px(kH, s));
        // a known place on the screen: popups are placed against the monitor, so the tests must know where the owner is
        SetWindowPos(w->window().hwnd(), nullptr, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        w->router().setAlwaysShowCues(false);
        Widget& root = w->root();
        auto* col = root.setLayout<BoxLayout>(Orientation::Vertical);
        auto* form = col->addForm();
        stats = root.addChild<ComboBox>();
        stats->name = "stats";
        stats->addItem("Boltzmann", 1);
        stats->addItem("Fermi-Dirac", 2);
        stats->addItem("Incomplete ionization", 3);
        form->addRow("&Statistics", stats);
        cmap = root.addChild<ComboBox>();
        cmap->name = "cmap";
        for (const char* n : {"viridis", "plasma", "inferno", "magma", "cividis", "coolwarm", "turbo", "gray"}) cmap->addItem(n);
        cmap->setCurrentIndex(3);
        form->addRow("Color&map", cmap);
        backend = root.addChild<ComboBox>();
        backend->name = "backend";
        backend->addItem("pytcad");
        form->addRow("&Backend", backend);
        off = root.addChild<ComboBox>();
        off->name = "off";
        off->addItems({"Example", "File"});
        off->setCurrentIndex(1);
        off->setEnabled(false);
        form->addRow("Source", off);
        run = col->add<PushButton>("Run");
        run->name = "run";
        col->addStretch(1);
        stats->on_current_index_changed = [this](int i) { log.push_back("stats index " + std::to_string(i)); };
        stats->on_activated = [this](int i) { log.push_back("stats activated " + std::to_string(i)); };
        cmap->on_activated = [this](int i) { log.push_back("cmap activated " + std::to_string(i)); };
        w->renderNow(nullptr, false);
    }
    PopupWindowService& popups() { return w->popupService(); }
    UiWindow* popupWindow() { return popups().currentWindow(); }
    HWND popupHwnd() { return popupWindow() ? popupWindow()->window().hwnd() : nullptr; }
    RECT screenRect(Widget* x) const {
        POINT o{0, 0};
        ClientToScreen(w->window().hwnd(), &o);
        const RectI g = x->windowRect();
        const double k = w->window().dpiScale() / w->scale();
        return {o.x + static_cast<LONG>(std::lround(g.x * k)), o.y + static_cast<LONG>(std::lround(g.y * k)),
                o.x + static_cast<LONG>(std::lround(g.right() * k)), o.y + static_cast<LONG>(std::lround(g.bottom() * k))};
    }
    PointF dips(double px_x, double px_y) const { return {static_cast<float>(px_x / w->scale()), static_cast<float>(px_y / w->scale())}; }
    PointF centre(Widget* x) const {
        const RectI r = x->windowRect();
        return dips(r.x + r.width / 2.0, r.y + r.height / 2.0);
    }
    // the centre of row `i` of the open popup, in the popup window's DIPs
    PointF rowCentre(int i) {
        auto* list = dynamic_cast<ComboPopupList*>(popupWindow()->root().children().front().get());
        return {list->sizeDips().width / 2, 1.0f + list->rowHeight() * (static_cast<float>(i - list->firstVisible()) + 0.5f)};
    }
    ComboPopupList* list() {
        return popupWindow() ? dynamic_cast<ComboPopupList*>(popupWindow()->root().children().front().get()) : nullptr;
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

RECT rectOf(HWND h) {
    RECT r{};
    GetWindowRect(h, &r);
    return r;
}
RECT workAreaOf(const RECT& near_rect) {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromRect(&near_rect, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcWork;
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

struct Bstr {
    BSTR b;
    explicit Bstr(const wchar_t* w) : b(SysAllocString(w)) {}
    ~Bstr() { SysFreeString(b); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
};

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

std::string nameOf(IUIAutomationElement* e) {
    BSTR b = nullptr;
    e->get_CurrentName(&b);
    return str(b);
}

void goldenOf(ComboWindow& f, const std::string& base, int pct, bool popup) {
    Image img;
    if (popup) {
        f.stats->showPopup();
        CHECK(f.popupWindow() != nullptr);
        if (!f.popupWindow()) return;
        f.list()->setHighlighted(1);
        CHECK(f.popupWindow()->renderNow(&img) == FrameStatus::Presented);
    } else {
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
    }
    const GoldenResult r = checkGolden(*warp(), base + "@" + std::to_string(pct) + ".png", img);
    CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
}

}  // namespace

TEST(combos_and_their_popup_match_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        {
            ComboWindow f(pct / 100.0);
            CHECK(f.w != nullptr);
            if (!f.w) return;
            f.stats->setFocus(FocusReason::Tab);
            goldenOf(f, "n3c_combo", pct, false);
        }
        {
            ComboWindow f(pct / 100.0);
            CHECK(f.w != nullptr);
            if (!f.w) return;
            goldenOf(f, "n3c_popup", pct, true);
        }
    }
}

TEST(combos_and_their_popup_in_high_contrast_match_the_goldens) {
    const HighContrast saved = highContrast();
    for (int pct : {100, 150, 200}) {
        {
            ComboWindow f(pct / 100.0);
            CHECK(f.w != nullptr);
            if (!f.w) break;
            nightSky();  // after the window exists: UiWindow::create reads the system's own state
            f.stats->setFocus(FocusReason::Tab);
            goldenOf(f, "n3c_combo_hc", pct, false);
        }
        {
            ComboWindow f(pct / 100.0);
            CHECK(f.w != nullptr);
            if (!f.w) break;
            nightSky();
            goldenOf(f, "n3c_popup_hc", pct, true);
        }
    }
    highContrast() = saved;
}

TEST(the_popup_is_a_borderless_owned_window_that_is_never_activated) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    CHECK(f.popupWindow() == nullptr);
    d.click(f.centre(f.stats));
    CHECK(f.stats->isPopupOpen() && f.popupWindow() != nullptr);
    if (!f.popupWindow()) return;
    HWND ph = f.popupHwnd();
    const LONG style = GetWindowLongW(ph, GWL_STYLE), ex = GetWindowLongW(ph, GWL_EXSTYLE);
    CHECK((style & WS_POPUP) != 0 && (style & WS_CAPTION) == 0 && (style & WS_THICKFRAME) == 0);  // borderless
    CHECK((ex & WS_EX_NOACTIVATE) != 0 && (ex & WS_EX_TOOLWINDOW) != 0);                         // no activation, no taskbar
    CHECK(GetWindow(ph, GW_OWNER) == f.w->window().hwnd());                                        // above its owner
    CHECK(IsWindowVisible(ph) != 0);
    CHECK(SendMessageW(ph, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(f.w->window().hwnd()), MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE);
    // the combo keeps the keyboard focus: the popup only takes the mouse
    CHECK(f.w->router().focusWidget() == f.stats);
    CHECK(f.popupWindow()->router().focusWidget() == nullptr);
    // its content is the list, filling its client area
    const RectI g = f.list()->geometry();
    const auto [cw, ch] = f.popupWindow()->window().clientSize();
    CHECK(g.x == 0 && g.y == 0 && g.width == cw && g.height == ch);
}

TEST(the_popup_opens_below_the_combo_at_least_as_wide) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.centre(f.stats));
    CHECK(f.popupWindow() != nullptr);
    if (!f.popupWindow()) return;
    const RECT a = f.screenRect(f.stats), p = rectOf(f.popupHwnd());
    CHECK(p.left == a.left && p.top == a.bottom);
    CHECK(p.right - p.left >= a.right - a.left);
    const SizeF hint = f.list()->sizeHint();
    CHECK(p.bottom - p.top == static_cast<LONG>(std::ceil(hint.height * f.popupWindow()->scale())));  // three rows and a border
    CHECK(f.list()->highlighted() == 0);
    // the popup's own scale is its window's DPI: the monitor it opened on
    CHECK(f.popupWindow()->scale() == f.popupWindow()->window().dpiScale());
}

TEST(near_the_bottom_of_the_screen_the_popup_flips_above_the_combo) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    // move the owner so the combo ends 40 px above the bottom of its monitor's work area
    RECT a = f.screenRect(f.stats);
    const RECT work = workAreaOf(a);
    RECT wr = rectOf(f.w->window().hwnd());
    SetWindowPos(f.w->window().hwnd(), nullptr, wr.left, wr.top + (work.bottom - 40 - a.bottom), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    a = f.screenRect(f.stats);
    CHECK(work.bottom - a.bottom == 40);
    Driver d(*f.w);
    d.click(f.centre(f.stats));
    CHECK(f.popupWindow() != nullptr);
    if (!f.popupWindow()) return;
    const RECT p = rectOf(f.popupHwnd());
    CHECK(p.bottom == a.top);  // directly above
    CHECK(p.top >= work.top && p.right - p.left >= a.right - a.left);
    CHECK(p.left == a.left);
    // a click on its first row still picks it (hit testing in the flipped popup)
    const PointF row = f.rowCentre(2);
    Driver pd(*f.popupWindow());
    pd.click(row);
    CHECK(f.stats->currentIndex() == 2);
}

TEST(a_click_on_a_row_of_the_popup_picks_it_and_closes_it) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.centre(f.stats));
    CHECK(f.popupWindow() != nullptr);
    if (!f.popupWindow()) return;
    HWND ph = f.popupHwnd();
    Driver pd(*f.popupWindow());  // REAL messages to the popup's own window
    pd.move(f.rowCentre(1));
    CHECK(f.list()->highlighted() == 1);
    pd.press(f.rowCentre(1));
    pd.release(f.rowCentre(1));
    CHECK_STR(f.take(), "stats index 1, stats activated 1");
    CHECK(!f.stats->isPopupOpen() && f.popupWindow() == nullptr);
    CHECK(IsWindowVisible(ph) == 0);  // hidden at once (its window is destroyed later)
    CHECK(f.stats->currentText() == "Fermi-Dirac");
    // a second popup gets a window of its own
    d.click(f.centre(f.stats));
    CHECK(f.popups().shown() == 2 && f.popupWindow() != nullptr);
}

TEST(a_press_outside_the_popup_closes_it_and_clicking_the_combo_again_only_closes) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.centre(f.cmap));
    CHECK(f.cmap->isPopupOpen());
    d.click(f.centre(f.cmap));  // the combo: closes, and does not reopen
    CHECK(!f.cmap->isPopupOpen() && f.popups().shown() == 1);
    d.click(f.centre(f.cmap));
    d.click(f.centre(f.run));   // elsewhere: closes, and the press reaches the button
    CHECK(!f.cmap->isPopupOpen());
    CHECK(f.run->hasFocus());
    CHECK_STR(f.take(), "");  // nothing was picked
}

TEST(keys_reach_the_combo_not_the_popup_and_enter_picks) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.stats->setFocus(FocusReason::Tab);
    d.key(VK_SPACE);
    CHECK(f.stats->isPopupOpen());
    d.key(VK_DOWN);
    d.key(VK_DOWN);
    CHECK(f.list()->highlighted() == 2 && f.stats->currentIndex() == 0);
    d.key(VK_UP);
    d.key(VK_RETURN);
    CHECK_STR(f.take(), "stats index 1, stats activated 1");
    CHECK(!f.stats->isPopupOpen());
    d.key(VK_F4);
    d.key(VK_ESCAPE);  // takes it back
    CHECK(!f.stats->isPopupOpen() && f.stats->currentIndex() == 1);
    d.key(VK_DOWN, Mod::Alt);  // Alt+Down opens
    CHECK(f.stats->isPopupOpen());
    d.key(VK_UP, Mod::Alt);
    CHECK(!f.stats->isPopupOpen());
    CHECK_STR(f.take(), "");
    d.key(VK_DOWN);  // closed: changes the item
    CHECK_STR(f.take(), "stats index 2, stats activated 2");
}

TEST(typing_a_letter_picks_an_item_and_the_wheel_steps_while_focused) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    f.stats->setFocus(FocusReason::Tab);
    d.type(u"f");
    CHECK_STR(f.take(), "stats index 1, stats activated 1");
    d.wheel(f.centre(f.stats), -1);  // toward the user: the next
    CHECK(f.stats->currentIndex() == 2);
    f.take();
    d.wheel(f.centre(f.cmap), -1);  // not the focused one: nothing
    CHECK(f.cmap->currentIndex() == 3);
}

TEST(a_long_list_scrolls_with_the_wheel_in_its_own_window) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    for (int i = 0; i < 30; ++i) f.backend->addItem("backend " + std::to_string(i));
    Driver d(*f.w);
    d.click(f.centre(f.backend));
    CHECK(f.popupWindow() != nullptr);
    if (!f.popupWindow()) return;
    CHECK(f.list()->visibleRows() == 10 && f.list()->firstVisible() == 0);
    Driver pd(*f.popupWindow());
    pd.wheel({f.popupWindow()->root().sizeDips().width / 2, 40}, -1);  // three rows
    CHECK(f.list()->firstVisible() == 3);
    f.list()->setHighlighted(29);
    CHECK(f.list()->firstVisible() == 20);
}

TEST(the_owner_changing_under_the_popup_closes_it) {
    ComboWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    HWND owner = f.w->window().hwnd();
    // deactivation: the owner loses the keyboard focus (WM_KILLFOCUS)
    d.click(f.centre(f.stats));
    CHECK(f.stats->isPopupOpen());
    SendMessageW(owner, WM_KILLFOCUS, 0, 0);
    CHECK(!f.stats->isPopupOpen() && f.popupWindow() == nullptr);
    SendMessageW(owner, WM_SETFOCUS, 0, 0);
    // moving
    d.click(f.centre(f.stats));
    CHECK(f.stats->isPopupOpen());
    RECT r = rectOf(owner);
    SetWindowPos(owner, nullptr, r.left + 30, r.top + 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(!f.stats->isPopupOpen());
    // resizing
    d.click(f.centre(f.stats));
    CHECK(f.stats->isPopupOpen());
    r = rectOf(owner);
    SetWindowPos(owner, nullptr, 0, 0, r.right - r.left + 40, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(!f.stats->isPopupOpen());
    // and it still opens afterwards, and the combo works
    d.click(f.centre(f.stats));
    CHECK(f.stats->isPopupOpen());
    f.stats->hidePopup();
    CHECK(!f.stats->isPopupOpen() && f.popupWindow() == nullptr);
}

TEST(destroying_the_owner_with_a_popup_open_is_clean) {
    auto f = std::make_unique<ComboWindow>();
    CHECK(f->w != nullptr);
    if (!f->w) return;
    Driver d(*f->w);
    d.click(f->centre(f->stats));
    CHECK(f->popupWindow() != nullptr);
    HWND ph = f->popupHwnd();
    f.reset();  // the owner, its combo and the popup's window all go
    CHECK(IsWindow(ph) == 0);
}

TEST(uia_sees_combo_boxes_with_value_and_expand_collapse) {
    ComboWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    f.take();
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto st = find(a.Get(), win.Get(), "stats");
    CHECK(st != nullptr);
    if (!st) return;
    CONTROLTYPEID ct = 0;
    st->get_CurrentControlType(&ct);
    CHECK(ct == UIA_ComboBoxControlTypeId);
    CHECK_STR(nameOf(st.Get()), "Statistics");  // a form label names it; the '&' is gone
    ComPtr<IUIAutomationValuePattern> vp;
    CHECK(SUCCEEDED(st->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&vp))) && vp);
    if (vp) {
        BSTR b = nullptr;
        vp->get_CurrentValue(&b);
        CHECK_STR(str(b), "Boltzmann");
        Bstr item(L"Fermi-Dirac"), none(L"Nope");
        CHECK(SUCCEEDED(vp->SetValue(item.b)));
        CHECK(f.stats->currentIndex() == 1);
        CHECK_STR(f.take(), "stats index 1, stats activated 1");
        CHECK(FAILED(vp->SetValue(none.b)));  // not an item: refused
        CHECK(f.stats->currentIndex() == 1);
    }
    ComPtr<IUIAutomationExpandCollapsePattern> ec;
    CHECK(SUCCEEDED(st->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&ec))) && ec);
    if (ec) {
        ExpandCollapseState s = ExpandCollapseState_LeafNode;
        ec->get_CurrentExpandCollapseState(&s);
        CHECK(s == ExpandCollapseState_Collapsed);
        CHECK(SUCCEEDED(ec->Expand()));
        CHECK(f.stats->isPopupOpen() && f.popupWindow() != nullptr);
        ec->get_CurrentExpandCollapseState(&s);
        CHECK(s == ExpandCollapseState_Expanded);
        // the highlight moves: it is announced (UiaRaiseNotificationEvent), and the keyboard focus is still the combo's
        const int before = f.w->uia().announcements();
        f.stats->keyEvent({VK_DOWN, Mod::None, true, false});
        CHECK(f.w->uia().announcements() == before + 1);
        CHECK_STR(f.w->uia().lastAnnouncement(), "Incomplete ionization");
        CHECK(SUCCEEDED(ec->Collapse()));
        CHECK(!f.stats->isPopupOpen());
        ec->get_CurrentExpandCollapseState(&s);
        CHECK(s == ExpandCollapseState_Collapsed);
    }
    // a disabled combo cannot be expanded
    auto off = find(a.Get(), win.Get(), "off");
    ComPtr<IUIAutomationExpandCollapsePattern> oec;
    CHECK(off && SUCCEEDED(off->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&oec))) && oec);
    if (oec) {
        CHECK(FAILED(oec->Expand()));
        CHECK(!f.off->isPopupOpen());
    }
}

// N2f (NATIVE-DESKTOP-PLAN.md 27.7): accessibility, checked through the REAL UI Automation client API in process --
// the element tree against the widget tree, properties, bounding rectangles, the Invoke / Toggle / Value / Text
// patterns, focus, a removed widget's element going unavailable -- and the high-contrast palette's goldens.
// Part of tcad_ui_render_tests. Narrator and Accessibility Insights need a person: NATIVE-DESKTOP-PLAN.md 27.7.6.
#include "mini_test.hpp"
#include "render_test_support.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;

namespace {

class Probe final : public Widget {
public:
    Role role;
    int invoked = 0, toggle = 0;
    Probe(std::string id, Role r, std::string label) : role(r) {
        name = std::move(id);
        accessibleName = std::move(label);
        setFocusPolicy(FocusPolicy::Strong);
    }
    SizeF sizeHint() const override { return {120, 24}; }
    Role accessibleRole() const override { return role; }
    bool accessibleInvoke() override { return ++invoked, true; }
    int accessibleToggleState() const override { return role == Role::CheckBox ? toggle : -1; }
    void accessibleToggle() override { toggle = toggle ? 0 : 1; }
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({0, 0, s.width, s.height}, token(role == Role::Button ? T::Accent : T::AlternateBase));
        const auto c = p.crisp({0, 0, s.width, s.height}, 1.0f);
        p.strokeRect(c.rect, token(hasFocus() ? T::Focus : T::BorderStrong), c.width);
        TextStyle ts;
        ts.color = token(role == Role::Button ? T::OnAccent : T::Text);
        ts.halign = HAlign::Center;
        p.drawText({0, 0, s.width, s.height}, accessibleName, ts);
    }
};

struct A11yWindow {
    std::unique_ptr<UiWindow> w;
    Probe *run = nullptr, *check = nullptr, *hidden = nullptr;
    LineEdit* edit = nullptr;
    explicit A11yWindow(double scale = 1.0) {
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_a11y_tests", .width = 300, .height = 160, .scale_override = scale});
        if (!r) return;
        w = std::move(*r);
        w->resizeClient(px(300, scale), px(160, scale));
        auto* col = w->root().setLayout<BoxLayout>(Orientation::Vertical);
        run = col->add<Probe>("run_button", Role::Button, "Run");
        run->toolTip = "Solve the device";
        check = col->add<Probe>("log_scale", Role::CheckBox, "Log scale");
        hidden = col->add<Probe>("hidden_one", Role::Button, "Hidden");
        hidden->hide();
        edit = col->add<LineEdit>(w->window().hwnd());
        edit->name = "voltage";
        edit->accessibleName = "Voltage [V]";
        edit->setText("alpha beta");  // before the first layout, on purpose: the resize must scroll it back
        edit->setCaretBlinking(false);
        col->addStretch(1);
        w->renderNow(nullptr, false);
    }
};

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

std::vector<ComPtr<IUIAutomationElement>> children(IUIAutomation* a, IUIAutomationElement* parent) {
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

ComPtr<IUIAutomationElement> byId(IUIAutomation* a, IUIAutomationElement* win, const char* id) {
    for (auto& c : children(a, win)) {
        BSTR b = nullptr;
        c->get_CurrentAutomationId(&b);
        if (str(b) == id) return c;
    }
    return nullptr;
}

}  // namespace

TEST(the_uia_tree_is_the_visible_widget_tree_with_its_properties) {
    A11yWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    CHECK(SUCCEEDED(a->ElementFromHandle(f.w->window().hwnd(), &win)) && win);
    if (!win) return;
    std::string ids, others;
    for (auto& c : children(a.Get(), win.Get())) {
        BSTR b = nullptr;
        c->get_CurrentFrameworkId(&b);
        const std::string fw = str(b);
        if (fw != "TCAD") {  // the window's own non-client parts (title bar), from Windows' HWND provider
            CONTROLTYPEID t = 0;
            c->get_CurrentControlType(&t);
            others += (t == UIA_TitleBarControlTypeId ? std::string("title bar") : std::to_string(t)) + " (" + fw + ") ";
            continue;
        }
        c->get_CurrentAutomationId(&b);
        ids += str(b) + " ";
    }
    std::printf("  not ours: %s\n", others.c_str());
    std::printf("  UIA children of the window: %s\n", ids.c_str());
    CHECK_EQ(ids, std::string("run_button log_scale voltage "));  // the hidden button is not in the tree
    // the text set before the first layout is visible from its start once the edit has its size (resized())
    CHECK(f.edit->model().text() == "alpha beta" && f.edit->geometry().width > 100);
    auto run = byId(a.Get(), win.Get(), "run_button");
    CHECK(run != nullptr);
    if (!run) return;
    CONTROLTYPEID type = 0;
    run->get_CurrentControlType(&type);
    CHECK_EQ(type, CONTROLTYPEID{UIA_ButtonControlTypeId});
    BSTR b = nullptr;
    run->get_CurrentName(&b);
    CHECK_EQ(str(b), std::string("Run"));
    run->get_CurrentHelpText(&b);
    CHECK_EQ(str(b), std::string("Solve the device"));
    run->get_CurrentFrameworkId(&b);
    CHECK_EQ(str(b), std::string("TCAD"));
    BOOL enabled = FALSE, focusable = FALSE;
    run->get_CurrentIsEnabled(&enabled);
    run->get_CurrentIsKeyboardFocusable(&focusable);
    CHECK(enabled && focusable);
    RECT r{};
    run->get_CurrentBoundingRectangle(&r);
    const RectI g = f.run->windowRect();
    POINT tl{g.x, g.y};
    ClientToScreen(f.w->window().hwnd(), &tl);
    CHECK(r.left == tl.x && r.top == tl.y && r.right - r.left == g.width && r.bottom - r.top == g.height);
    auto edit = byId(a.Get(), win.Get(), "voltage");
    CHECK(edit != nullptr);
    if (edit) {
        edit->get_CurrentControlType(&type);
        CHECK_EQ(type, CONTROLTYPEID{UIA_EditControlTypeId});
    }
}

TEST(invoke_toggle_and_value_patterns_act_on_the_widgets) {
    A11yWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    ComPtr<IUIAutomationInvokePattern> inv;
    auto run = byId(a.Get(), win.Get(), "run_button");
    CHECK(run && SUCCEEDED(run->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv);
    if (inv) CHECK(SUCCEEDED(inv->Invoke()) && f.run->invoked == 1);
    ComPtr<IUIAutomationTogglePattern> tog;
    auto chk = byId(a.Get(), win.Get(), "log_scale");
    CHECK(chk && SUCCEEDED(chk->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&tog))) && tog);
    if (tog) {
        CHECK(SUCCEEDED(tog->Toggle()));
        ToggleState st = ToggleState_Off;
        tog->get_CurrentToggleState(&st);
        CHECK(st == ToggleState_On && f.check->toggle == 1);
    }
    ComPtr<IUIAutomationInvokePattern> none;
    chk->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&none));
    CHECK(!none);  // a check box offers Toggle, not Invoke
    ComPtr<IUIAutomationValuePattern> val;
    auto edit = byId(a.Get(), win.Get(), "voltage");
    CHECK(edit && SUCCEEDED(edit->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&val))) && val);
    if (val) {
        BSTR b = nullptr;
        val->get_CurrentValue(&b);
        CHECK_EQ(str(b), std::string("alpha beta"));
        CHECK(SUCCEEDED(val->SetValue(const_cast<wchar_t*>(L"gamma delta"))));
        CHECK_EQ(f.edit->text(), std::string("gamma delta"));
    }
}

TEST(the_text_pattern_reads_selects_and_moves_by_units) {
    A11yWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    auto edit = byId(a.Get(), win.Get(), "voltage");
    ComPtr<IUIAutomationTextPattern> text;
    CHECK(edit && SUCCEEDED(edit->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text);
    if (!text) return;
    ComPtr<IUIAutomationTextRange> doc;
    text->get_DocumentRange(&doc);
    BSTR b = nullptr;
    doc->GetText(-1, &b);
    CHECK_EQ(str(b), std::string("alpha beta"));
    f.edit->model().setSelection(0, 5);
    ComPtr<IUIAutomationTextRangeArray> sel;
    text->GetSelection(&sel);
    ComPtr<IUIAutomationTextRange> s0;
    int n = 0;
    if (sel) sel->get_Length(&n), sel->GetElement(0, &s0);
    CHECK(n == 1 && s0);
    if (s0) {
        s0->GetText(-1, &b);
        CHECK_EQ(str(b), std::string("alpha"));
        // collapse to its end, move one word on, expand: "beta"
        ComPtr<IUIAutomationTextRange> r;
        s0->Clone(&r);
        int moved = 0;
        r->MoveEndpointByRange(TextPatternRangeEndpoint_Start, r.Get(), TextPatternRangeEndpoint_End);
        r->Move(TextUnit_Word, 1, &moved);
        r->ExpandToEnclosingUnit(TextUnit_Word);
        r->GetText(-1, &b);
        CHECK_EQ(str(b), std::string("beta"));
        CHECK(SUCCEEDED(r->Select()));
        CHECK_EQ(f.edit->model().selectedText(), std::string("beta"));
        r->ExpandToEnclosingUnit(TextUnit_Character);
        r->GetText(-1, &b);
        CHECK_EQ(str(b), std::string("b"));
        ComPtr<IUIAutomationTextRange> found;
        doc->FindText(const_cast<wchar_t*>(L"PHA"), FALSE, TRUE, &found);
        CHECK(found != nullptr);
        if (found) {
            found->GetText(-1, &b);
            CHECK_EQ(str(b), std::string("pha"));
        }
    }
}

TEST(focus_through_uia_and_a_removed_widget_goes_unavailable) {
    A11yWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    const int events = f.w->uia().eventsAttempted();
    auto chk = byId(a.Get(), win.Get(), "log_scale");
    CHECK(chk && SUCCEEDED(chk->SetFocus()));
    CHECK(f.check->hasFocus());
    CHECK(f.w->uia().eventsAttempted() > events);  // the focus event was raised (when a client listens)
    BOOL has = FALSE;
    chk->get_CurrentHasKeyboardFocus(&has);
    CHECK(has);
    auto run = byId(a.Get(), win.Get(), "run_button");
    auto owned = f.w->root().release(f.run);  // the widget leaves the tree
    owned.reset();
    BSTR b = nullptr;
    const HRESULT hr = run->get_CurrentName(&b);
    std::printf("  a removed widget's element: hr=0x%08lx\n", static_cast<unsigned long>(hr));
    CHECK(hr == UIA_E_ELEMENTNOTAVAILABLE);
    if (SUCCEEDED(hr)) SysFreeString(b);
}

TEST(high_contrast_maps_the_chrome_to_the_system_palette) {
    HighContrast& hc = highContrast();
    const HighContrast saved = hc;
    // a fixed palette (Windows' "Night sky"-like), so the goldens do not depend on this machine's theme. Set again AFTER
    // each window exists: UiWindow::create reads the system's own high-contrast state.
    auto night_sky = [&] {
        hc.on = true;
        hc.window = Color::rgb(0x000000);
        hc.window_text = Color::rgb(0xFFFFFF);
        hc.highlight = Color::rgb(0x1AEBFF);
        hc.highlight_text = Color::rgb(0x000000);
        hc.gray_text = Color::rgb(0x3FF23F);
    };
    night_sky();
    CHECK(token(T::Base) == Color::rgb(0x000000) && token(T::Text) == Color::rgb(0xFFFFFF) && token(T::Focus) == Color::rgb(0x1AEBFF));
    CHECK(token(T::Overlay) == Color::rgb(0xFFFFFF));  // data colours keep theirs (Overlay is white in the theme)
    for (int pct : {100, 150, 200}) {
        A11yWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) break;
        night_sky();
        f.check->setFocus();
        f.edit->model().setSelection(0, 5);
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n2f_high_contrast@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
    hc = saved;
    CHECK(!highContrast().on || saved.on);
}

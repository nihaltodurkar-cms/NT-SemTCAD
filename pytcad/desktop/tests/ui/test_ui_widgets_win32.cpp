// N3a on real windows (NATIVE-DESKTOP-PLAN.md 27.8.1): the widget set drawn through UiWindow + Direct2D on WARP --
// goldens at 100/150/200% in the light theme and in high contrast; the buttons driven by REAL window messages
// (clicks, Space, arrow keys, Alt and Alt+<letter> as WM_SYSKEYDOWN); UI Automation through the real client API
// (Invoke on a push button, Toggle on a check box, SelectionItem on radio buttons, a group box's children, names
// without '&', a form field named by its label); and a wrapped label's height from the real DirectWrite engine.
// Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"
#include "ui_driver.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/group_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/ui_window.hpp"

#include <UIAutomation.h>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;
using tcad::platform::Mod;

namespace {

constexpr float kW = 400, kH = 330;
const char* kNote =
    "The solver converged in 14 Newton iterations; the final update norm was below tolerance for every carrier density.";

struct WidgetsWindow {
    std::unique_ptr<UiWindow> w;
    Label *title = nullptr, *note = nullptr, *vg_label = nullptr;
    LineEdit* vg = nullptr;
    GroupBox *models = nullptr, *stats = nullptr;
    CheckBox *mobility = nullptr, *impact = nullptr, *btbt = nullptr;
    RadioButton *boltzmann = nullptr, *fermi = nullptr;
    PushButton *run = nullptr, *stop = nullptr, *save = nullptr;
    std::vector<std::string> log;

    explicit WidgetsWindow(double scale = 1.0) {
        auto r = UiWindow::create(warp(), {.title = L"tcad_ui_widgets_tests", .width = 400, .height = 330, .scale_override = scale});
        if (!r) {
            std::printf("  UiWindow::create: %s\n", r.error().c_str());
            return;
        }
        w = std::move(*r);
        w->resizeClient(px(kW, scale), px(kH, scale));
        w->router().setAlwaysShowCues(false);  // the machine's setting must not change a test
        Widget& root = w->root();
        auto* col = root.setLayout<BoxLayout>(Orientation::Vertical);
        title = col->add<Label>("Device setup");
        title->setFont(15, true);
        note = col->add<Label>(kNote);
        note->name = "note";
        note->setWordWrap(true);
        auto* form = col->addForm();
        vg = root.addChild<LineEdit>(w->window().hwnd());
        vg->name = "gate_voltage";
        vg->setText("1.5");
        vg->setCaretBlinking(false);
        vg_label = form->addRow("&Gate voltage [V]", vg);
        auto* row = col->addBox(Orientation::Horizontal);
        models = row->add<GroupBox>("Physics models");
        models->name = "models";
        auto* ml = models->setLayout<BoxLayout>(Orientation::Vertical);
        mobility = ml->add<CheckBox>("Field-dependent &mobility");
        mobility->name = "mobility";
        mobility->setChecked(true);
        impact = ml->add<CheckBox>("&Impact ionization");
        impact->name = "impact";
        btbt = ml->add<CheckBox>("Band-to-band tunneling");
        btbt->setEnabled(false);
        stats = row->add<GroupBox>("Statistics");
        auto* sl = stats->setLayout<BoxLayout>(Orientation::Vertical);
        boltzmann = sl->add<RadioButton>("&Boltzmann");
        boltzmann->name = "boltzmann";
        boltzmann->setChecked(true);
        fermi = sl->add<RadioButton>("&Fermi-Dirac");
        fermi->name = "fermi";
        sl->addStretch(1);
        auto* buttons = col->addBox(Orientation::Horizontal);
        run = buttons->add<PushButton>("&Run");
        run->name = "run";
        stop = buttons->add<PushButton>("&Stop");
        save = buttons->add<PushButton>("Export PVD");
        save->setEnabled(false);
        buttons->addStretch(1);
        col->addStretch(1);
        run->on_clicked = [this] { log.push_back("run"); };
        stop->on_clicked = [this] { log.push_back("stop"); };
        impact->on_toggled = [this](bool on) { log.push_back(std::string("impact ") + (on ? "on" : "off")); };
        fermi->on_toggled = [this](bool on) { log.push_back(std::string("fermi ") + (on ? "on" : "off")); };
        w->renderNow(nullptr, false);
    }
    PointF centre(Widget* x) const {
        const RectI g = x->windowRect();
        const double s = w->scale();
        return {static_cast<float>((g.x + g.width / 2.0) / s), static_cast<float>((g.y + g.height / 2.0) / s)};
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

// Depth-first by AutomationId (Widget::name).
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

}  // namespace

TEST(widgets_match_the_goldens_at_three_scales) {
    CHECK(warp() != nullptr);
    if (!warp()) return;
    for (int pct : {100, 150, 200}) {
        WidgetsWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) return;
        f.w->router().setAlwaysShowCues(true);  // the underlines are part of the picture
        f.run->setFocus();
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        CHECK(img.width == px(kW, pct / 100.0) && img.height == px(kH, pct / 100.0));
        const GoldenResult r = checkGolden(*warp(), "n3a_widgets@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}

TEST(widgets_in_high_contrast_match_the_goldens) {
    const HighContrast saved = highContrast();
    for (int pct : {100, 150, 200}) {
        WidgetsWindow f(pct / 100.0);
        CHECK(f.w != nullptr);
        if (!f.w) break;
        nightSky();  // after the window exists: UiWindow::create reads the system's own state
        f.w->router().setAlwaysShowCues(true);
        f.boltzmann->setFocus();
        Image img;
        CHECK(f.w->renderNow(&img) == FrameStatus::Presented);
        const GoldenResult r = checkGolden(*warp(), "n3a_widgets_hc@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
    highContrast() = saved;
}

TEST(a_wrapped_label_takes_the_real_engines_height_for_its_width) {
    WidgetsWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    TextStyle st;
    st.wrap = true;
    const float width = f.note->sizeDips().width;
    const SizeF m = f.w->textEngine().measureWrapped(kNote, st, width);
    std::printf("  note: %.0f DIPs wide, %.1f DIPs tall, %d lines\n", width, m.height, f.w->textEngine().lineCount(kNote, st, width));
    CHECK(f.w->textEngine().lineCount(kNote, st, width) >= 2);
    CHECK(f.note->geometry().height == static_cast<int>(std::ceil(m.height)));
    // wider: fewer lines, a shorter label, and the form below moves up
    const int form_y = f.vg->geometry().y;
    f.w->resizeClient(700, 330);
    f.w->renderNow(nullptr, false);
    CHECK(f.note->geometry().height < static_cast<int>(std::ceil(m.height)));
    CHECK(f.vg->geometry().y < form_y);
}

TEST(buttons_answer_real_mouse_and_key_messages) {
    WidgetsWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    d.click(f.centre(f.run));
    CHECK(f.run->hasFocus());
    d.press(f.centre(f.stop));
    d.release({f.centre(f.stop).x + 300, f.centre(f.stop).y});  // released off it: no click
    d.click(f.centre(f.impact));
    d.click(f.centre(f.save));  // disabled
    d.click(f.centre(f.fermi));
    CHECK(f.fermi->isChecked() && !f.boltzmann->isChecked());
    d.key(VK_UP);  // arrow keys move within the radio group and check
    CHECK(f.boltzmann->hasFocus() && f.boltzmann->isChecked());
    f.impact->setFocus();
    d.key(VK_SPACE);
    CHECK((f.log == std::vector<std::string>{"run", "impact on", "fermi on", "fermi off", "impact off"}));
}

TEST(alt_shows_the_cues_and_alt_letters_click_and_focus_buddies) {
    WidgetsWindow f;
    CHECK(f.w != nullptr);
    if (!f.w) return;
    Driver d(*f.w);
    CHECK(!f.run->mnemonicCuesVisible());
    // Alt DOWN only: an unpaired Alt release would let DefWindowProc enter the system menu's modal loop
    SendMessageW(f.w->window().hwnd(), WM_SYSKEYDOWN, VK_MENU, 0x20000001);
    CHECK(f.run->mnemonicCuesVisible());
    d.key('R', Mod::Alt);
    CHECK(f.run->hasFocus());
    d.key('F', Mod::Alt);
    CHECK(f.fermi->hasFocus() && f.fermi->isChecked());
    d.key('G', Mod::Alt);
    CHECK(f.vg->hasFocus());
    CHECK((f.log == std::vector<std::string>{"run", "fermi on"}));
}

TEST(uia_sees_the_widgets_with_their_patterns) {
    WidgetsWindow f;
    auto a = client();
    CHECK(f.w && a);
    if (!f.w || !a) return;
    ComPtr<IUIAutomationElement> win;
    a->ElementFromHandle(f.w->window().hwnd(), &win);
    // Invoke on a push button, named without its '&'
    auto run = find(a.Get(), win.Get(), "run");
    CHECK(run != nullptr);
    if (!run) return;
    CHECK_EQ(nameOf(run.Get()), std::string("Run"));
    ComPtr<IUIAutomationInvokePattern> inv;
    CHECK(SUCCEEDED(run->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv);
    if (inv) CHECK(SUCCEEDED(inv->Invoke()));
    CHECK((f.log == std::vector<std::string>{"run"}));
    // Toggle on a check box, inside its group box
    auto models = find(a.Get(), win.Get(), "models");
    CHECK(models != nullptr);
    if (models) {
        CONTROLTYPEID ct = 0;
        models->get_CurrentControlType(&ct);
        CHECK(ct == UIA_GroupControlTypeId);
        CHECK_EQ(nameOf(models.Get()), std::string("Physics models"));
        CHECK(kids(a.Get(), models.Get()).size() == 3);  // the three check boxes
    }
    auto impact = find(a.Get(), win.Get(), "impact");
    ComPtr<IUIAutomationTogglePattern> tog;
    CHECK(impact && SUCCEEDED(impact->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&tog))) && tog);
    if (tog) {
        CHECK(SUCCEEDED(tog->Toggle()));
        CHECK(f.impact->isChecked());
    }
    // SelectionItem on radio buttons: selecting one deselects the other; no Invoke, no Toggle
    auto fermi = find(a.Get(), win.Get(), "fermi"), boltz = find(a.Get(), win.Get(), "boltzmann");
    ComPtr<IUIAutomationSelectionItemPattern> sel, sel_b;
    CHECK(fermi && SUCCEEDED(fermi->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&sel))) && sel);
    CHECK(boltz && SUCCEEDED(boltz->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&sel_b))) && sel_b);
    if (sel && sel_b) {
        BOOL on = TRUE;
        sel->get_CurrentIsSelected(&on);
        CHECK(!on);
        CHECK(SUCCEEDED(sel->Select()));
        sel->get_CurrentIsSelected(&on);
        CHECK(on && f.fermi->isChecked());
        sel_b->get_CurrentIsSelected(&on);
        CHECK(!on && !f.boltzmann->isChecked());
        CHECK(sel->RemoveFromSelection() == UIA_E_INVALIDOPERATION);  // a checked radio stays checked
    }
    if (fermi) {
        ComPtr<IUIAutomationInvokePattern> none;
        ComPtr<IUIAutomationTogglePattern> none_t;
        fermi->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&none));
        fermi->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&none_t));
        CHECK(!none && !none_t);
        CONTROLTYPEID ct = 0;
        fermi->get_CurrentControlType(&ct);
        CHECK(ct == UIA_RadioButtonControlTypeId);
        CHECK_EQ(nameOf(fermi.Get()), std::string("Fermi-Dirac"));
    }
    // the form's field is named by its label; the note is Text
    auto vg = find(a.Get(), win.Get(), "gate_voltage");
    CHECK(vg && nameOf(vg.Get()) == "Gate voltage [V]");
    auto note = find(a.Get(), win.Get(), "note");
    if (note) {
        CONTROLTYPEID ct = 0;
        note->get_CurrentControlType(&ct);
        CHECK(ct == UIA_TextControlTypeId);
    }
}

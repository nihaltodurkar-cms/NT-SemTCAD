// Portable tests of N3a's widgets (NATIVE-DESKTOP-PLAN.md 27.8.1): '&' mnemonics, Label (one line, word wrap,
// buddy), height-for-width in Box/Form/Stack, widget contents margins and GroupBox, the button behaviours pinned in
// button.hpp (press/release, Space, Enter, disabled, mnemonics, exclusive radio sets and ButtonGroup, one Tab stop per
// group, arrow keys), keyboard cues, and what each widget paints. Part of tcad_ui_core_tests: no Win32. Every
// expected number is worked out by hand from the fake text engine below and the rules in layout.hpp.
#include "mini_test.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/layout.hpp"
#include "ui/core/recording_painter.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/group_box.hpp"
#include "ui/widgets/label.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace tcad::ui;
using tcad::desktop::theme::T;
using tcad::platform::KeyEvent;
using tcad::platform::Mod;
using tcad::platform::MouseButton;
using tcad::platform::MouseEvent;
using tcad::platform::MouseType;

namespace {

// 0.5 em per UTF-8 byte, a 1.25 em line; wrapping is greedy at spaces (a space is 0.5 em too), one word per line at
// least. At the 12-DIP UI font: 6 DIPs a byte, 15 DIPs a line.
class WrapText final : public TextEngine {
public:
    SizeF measure(std::string_view s, const TextStyle& st) override {
        return {0.5f * st.size * static_cast<float>(s.size()), 1.25f * st.size};
    }
    SizeF measureWrapped(std::string_view s, const TextStyle& st, float max_width) override {
        const float byte = 0.5f * st.size;
        std::vector<std::size_t> words;
        for (std::size_t i = 0; i < s.size();) {
            const std::size_t e = std::min(s.find(' ', i), s.size());
            words.push_back(e - i);
            i = e + 1;
        }
        int lines = 0;
        float widest = 0, cur = -1;
        for (std::size_t w : words) {
            const float add = byte * static_cast<float>(w);
            if (cur < 0 || cur + byte + add > max_width) {
                if (cur >= 0) widest = std::max(widest, cur);
                cur = add;
                ++lines;
            } else {
                cur += byte + add;
            }
        }
        widest = std::max(widest, cur);
        return {widest, 1.25f * st.size * static_cast<float>(std::max(lines, 1))};
    }
};

class Host final : public UiHost {
public:
    double s = 1.0;
    WrapText text;
    Widget root;
    InputRouter router{root};
    Host(double scale = 1.0, int w = 200, int h = 200) : s(scale) {
        root.setHost(this);
        root.setGeometry({0, 0, w, h});
    }
    ~Host() override { root.setHost(nullptr); }
    void invalidate(const RectI&) override {}
    void scheduleLayout() override {}
    TextEngine& textEngine() override { return text; }
    double scale() const override { return s; }
    InputRouter* input() override { return &router; }
    void relayout() { root.setGeometry(root.geometry()); }
};

class Box final : public Widget {
public:
    Box(SizeF hint, Policy p = {SizePolicy::Fixed, SizePolicy::Fixed}) : hint_(hint) { setSizePolicy(p); }
    SizeF sizeHint() const override { return hint_; }
    SizeF minimumSizeHint() const override { return hint_; }

private:
    SizeF hint_;
};

// Ten 4-byte words: 24 DIPs a word, 30 per word with its space; n words on a line need 30n - 6 DIPs.
const std::string kTen = "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj";

constexpr unsigned kLeftBit = 1u << static_cast<unsigned>(MouseButton::Left);

MouseEvent mouse(MouseType t, float x, float y, unsigned held = 0, MouseButton b = MouseButton::Left) {
    MouseEvent e;
    e.type = t;
    e.button = t == MouseType::Move ? MouseButton::None : b;
    e.x = x;
    e.y = y;
    e.buttons_down = held;
    return e;
}
KeyEvent key(int vk, bool down = true, Mod m = Mod::None, bool repeat = false) { return {vk, m, down, repeat}; }

int countKind(const RecordingPainter& p, const std::string& kind) {
    int n = 0;
    for (const auto& op : p.ops()) n += op.kind == kind;
    return n;
}
const PaintOp* firstKind(const RecordingPainter& p, const std::string& kind) {
    for (const auto& op : p.ops())
        if (op.kind == kind) return &op;
    return nullptr;
}

}  // namespace

// -- mnemonics ----------------------------------------------------------------------------------------------------

TEST(mnemonic_marks_the_first_single_ampersand) {
    const auto m = parseMnemonic("&Run");
    CHECK(m.text == "Run");
    CHECK(m.key == U'R');
    CHECK(m.offset == 0 && m.length == 1);
    const auto x = parseMnemonic("E&xit");
    CHECK(x.text == "Exit" && x.key == U'x' && x.offset == 1);
}

TEST(mnemonic_double_ampersand_is_a_literal_and_trailing_ampersand_is_kept) {
    const auto m = parseMnemonic("Save && exit");
    CHECK(m.text == "Save & exit");
    CHECK(m.key == 0);
    CHECK(parseMnemonic("a&").text == "a&");
    const auto t = parseMnemonic("&&&Open");
    CHECK(t.text == "&Open" && t.key == U'O' && t.offset == 1);
}

TEST(mnemonic_later_markers_are_dropped_and_multibyte_keys_decode) {
    const auto m = parseMnemonic("&a &b");
    CHECK(m.text == "a b" && m.key == U'a');
    const auto u = parseMnemonic("&\xC2\xB5m");  // "&µm"
    CHECK(u.text == "\xC2\xB5m" && u.key == 0xB5 && u.length == 2);
}

// -- Label ------------------------------------------------------------------------------------------------------

TEST(label_one_line_hint_is_its_text) {
    Host h;
    auto* l = h.root.addChild<Label>("Doping");
    CHECK(l->sizeHint() == (SizeF{36, 15}));
    CHECK(l->minimumSizeHint() == (SizeF{36, 15}));
    CHECK(!l->hasHeightForWidth());
    CHECK(l->accessibleRole() == Role::Text);
    CHECK(l->accessibleName == "Doping");
}

TEST(label_wrapped_hint_is_capped_at_forty_ems_and_minimum_is_the_longest_word) {
    Host h;
    std::string long_text;
    for (int i = 0; i < 20; ++i) long_text += (i ? " " : "") + std::string("wxyz");  // 20 words: 594 DIPs in one line
    auto* l = h.root.addChild<Label>(long_text);
    l->setWordWrap(true);
    CHECK(l->hasHeightForWidth());
    // hint width 40 em = 480; 30n - 6 <= 480 -> 16 words a line -> 2 lines
    CHECK(l->sizeHint() == (SizeF{480, 30}));
    // the longest word, one word a line
    CHECK(l->minimumSizeHint() == (SizeF{24, 300}));
    CHECK(l->heightForWidth(114) == 75.0f);  // 4 words a line -> 5 lines
}

TEST(label_ampersand_is_literal_without_a_buddy) {
    Host h;
    auto* l = h.root.addChild<Label>("R&D");
    CHECK(l->shownText() == "R&D");
    CHECK(l->mnemonic() == 0);
    auto* f = h.root.addChild<Box>(SizeF{10, 10});
    l->setBuddy(f);
    CHECK(l->shownText() == "RD");
    CHECK(l->mnemonic() == U'D');
}

// -- height-for-width ---------------------------------------------------------------------------------------------

TEST(hfw_vertical_box_gives_a_wrapped_label_the_height_its_width_needs) {
    Host h(1.0, 200, 300);
    auto* col = h.root.setLayout<BoxLayout>(Orientation::Vertical);
    auto* l = col->add<Label>(kTen);
    l->setWordWrap(true);
    auto* below = col->add<Box>(SizeF{50, 20});
    col->addStretch(1);
    h.relayout();
    // content 182 wide: 6 words a line -> 2 lines -> 30
    CHECK(l->geometry() == (RectI{9, 9, 182, 30}));
    CHECK(below->geometry().y == 9 + 30 + 6);
    h.root.setGeometry({0, 0, 120, 300});  // content 102: 3 words a line -> 4 lines
    CHECK(l->geometry() == (RectI{9, 9, 102, 60}));
    CHECK(below->geometry().y == 9 + 60 + 6);
}

TEST(hfw_vertical_box_at_150_percent_rounds_in_device_pixels) {
    Host h(1.5, 300, 450);
    auto* col = h.root.setLayout<BoxLayout>(Orientation::Vertical);
    auto* l = col->add<Label>(kTen);
    l->setWordWrap(true);
    col->addStretch(1);
    h.relayout();
    // margins 13.5 -> 14 px; content 272 px = 181.3 DIPs: 6 words a line, 2 lines = 30 DIPs = 45 px
    CHECK(l->geometry() == (RectI{14, 14, 272, 45}));
}

TEST(hfw_layout_reports_height_for_width) {
    Host h(1.0, 200, 300);
    auto* col = h.root.setLayout<BoxLayout>(Orientation::Vertical);
    auto* l = col->add<Label>(kTen);
    l->setWordWrap(true);
    col->add<Box>(SizeF{50, 20});
    CHECK(col->hasHeightForWidth());
    CHECK(col->heightForWidthPx(200) == 9 + 30 + 6 + 20 + 9);
    CHECK(col->heightForWidthPx(120) == 9 + 60 + 6 + 20 + 9);
    CHECK(h.root.hasHeightForWidth());
    CHECK(h.root.heightForWidth(120) == 104.0f);
    auto* plain = h.root.addChild<Widget>();
    auto* pl = plain->setLayout<BoxLayout>(Orientation::Vertical);
    pl->add<Box>(SizeF{10, 10});
    CHECK(!pl->hasHeightForWidth());
}

TEST(hfw_horizontal_box_row_is_as_tall_as_its_tallest_item_at_its_width) {
    Host h(1.0, 200, 300);
    auto* col = h.root.setLayout<BoxLayout>(Orientation::Vertical);
    auto* row = col->addBox(Orientation::Horizontal);
    row->add<Box>(SizeF{50, 20});
    auto* l = row->add<Label>(kTen);
    l->setWordWrap(true);
    l->setSizePolicy({SizePolicy::Expanding, SizePolicy::Preferred});
    auto* below = col->add<Box>(SizeF{50, 20});
    col->addStretch(1);
    h.relayout();
    // the label gets 182 - 50 - 6 = 126: 4 words a line -> 3 lines -> 45; the row is 45 tall
    CHECK(l->geometry().width == 126);
    CHECK(below->geometry().y == 9 + 45 + 6);
}

TEST(hfw_form_spanning_row_and_field_use_their_widths) {
    Host h(1.0, 200, 300);
    auto* form = h.root.setLayout<FormLayout>();
    auto* note = h.root.addChild<Label>(kTen);
    note->setWordWrap(true);
    form->addRow(note);
    auto* field = h.root.addChild<Box>(SizeF{40, 20}, Policy{SizePolicy::Expanding, SizePolicy::Fixed});
    auto* label = form->addRow("Gate", field);
    h.relayout();
    CHECK(note->geometry() == (RectI{9, 9, 182, 30}));  // spanning: the full width
    CHECK(field->geometry().y == 9 + 30 + 6 + (20 - 20) / 2);
    CHECK(label->geometry().x == 9);
    CHECK(form->hasHeightForWidth());
    CHECK(form->heightForWidthPx(120) == 9 + 60 + 6 + 20 + 9);
}

TEST(hfw_stack_page_propagates_to_the_parent) {
    Host h(1.0, 200, 400);
    auto* col = h.root.setLayout<BoxLayout>(Orientation::Vertical);
    auto* stackw = col->add<Widget>();
    auto* stack = stackw->setLayout<StackLayout>();
    stack->setContentsMargins(0, 0, 0, 0);
    auto* page = stackw->addChild<Widget>();
    auto* form = page->setLayout<FormLayout>();
    form->setContentsMargins(0, 0, 0, 0);
    auto* note = page->addChild<Label>(kTen);
    note->setWordWrap(true);
    form->addRow(note);
    stack->addWidget(page);
    stack->addWidget(stackw->addChild<Box>(SizeF{10, 10}));
    auto* below = col->add<Box>(SizeF{50, 20});
    col->addStretch(1);
    h.relayout();
    CHECK(stack->hasHeightForWidth());
    CHECK(note->geometry().height == 30);
    CHECK(below->geometry().y == 9 + 30 + 6);
}

// -- contents margins and GroupBox --------------------------------------------------------------------------------

TEST(contents_margins_shrink_the_layout_area_and_grow_the_hints) {
    Host h(1.0, 200, 200);
    auto* w = h.root.addChild<Widget>();
    auto* l = w->setLayout<BoxLayout>(Orientation::Vertical);
    l->setContentsMargins(0, 0, 0, 0);
    auto* b = l->add<Box>(SizeF{40, 20});
    w->setContentsMargins({2, 10, 3, 4});
    CHECK(w->sizeHint() == (SizeF{45, 34}));
    w->setGeometry({0, 0, 100, 100});
    CHECK(b->geometry() == (RectI{2, 10 + (86 - 20) / 2 * 0, 40, 20}));
    CHECK(w->contentsRectPx() == (RectI{2, 10, 95, 86}));
}

TEST(group_box_frame_and_title_are_contents_margins) {
    Host h(1.0, 300, 300);
    auto* g = h.root.addChild<GroupBox>("Physics");
    auto* l = g->setLayout<BoxLayout>(Orientation::Vertical);
    auto* b = l->add<Box>(SizeF{100, 20});
    l->addStretch(1);
    // layout 118 x 38 plus the frame (1 each side) and the title band (15 on top, 1 at the bottom)
    CHECK(g->sizeHint() == (SizeF{120, 54}));
    g->setGeometry({0, 0, 200, 100});
    CHECK(b->geometry() == (RectI{10, 24, 100, 20}));
    CHECK(g->accessibleRole() == Role::Group);
    CHECK(g->accessibleName == "Physics");
}

TEST(group_box_is_at_least_as_wide_as_its_title) {
    Host h;
    auto* g = h.root.addChild<GroupBox>("A rather long group title");  // 25 bytes, bold is measured the same: 150
    g->setLayout<BoxLayout>(Orientation::Vertical)->add<Box>(SizeF{10, 10});
    CHECK(g->sizeHint().width == 150 + 24);
}

TEST(group_box_paints_frame_and_title_over_it) {
    Host h;
    auto* g = h.root.addChild<GroupBox>("Stats");
    g->setGeometry({0, 0, 120, 60});
    RecordingPainter p(1.0);
    g->paintTree(p);
    CHECK(countKind(p, "strokeRoundedRect") == 1);
    const PaintOp* bg = firstKind(p, "fillRect");
    CHECK(bg && bg->color == token(T::Window));  // the title's background covers the frame line
    const PaintOp* t = firstKind(p, "text");
    CHECK(t && t->text == "Stats" && t->style.bold);
}

// -- buttons ------------------------------------------------------------------------------------------------------

namespace {
struct ButtonRig {
    Host h{1.0, 300, 200};
    PushButton* b;
    int clicks = 0;
    ButtonRig() {
        b = h.root.addChild<PushButton>("&Run");
        b->setGeometry({10, 10, 80, 26});
        b->on_clicked = [this] { ++clicks; };
    }
};
}  // namespace

TEST(push_button_hint_is_text_padded_with_an_80_dip_minimum) {
    Host h;
    auto* b = h.root.addChild<PushButton>("&Run");
    CHECK(b->text() == "Run");
    CHECK(b->sizeHint() == (SizeF{80, 25}));  // 18 + 16 < 80; 15 + 10
    auto* wide = h.root.addChild<PushButton>("Export all results");  // 108 + 16
    CHECK(wide->sizeHint().width == 124);
    CHECK(b->sizePolicy().horizontal == SizePolicy::Minimum && b->sizePolicy().vertical == SizePolicy::Fixed);
}

TEST(push_button_clicks_on_release_inside_only) {
    ButtonRig r;
    r.h.router.mouse(mouse(MouseType::Down, 20, 20, kLeftBit));
    CHECK(r.b->isDown());
    CHECK(r.clicks == 0);
    r.h.router.mouse(mouse(MouseType::Up, 20, 20, 0));
    CHECK(r.clicks == 1);
    CHECK(!r.b->isDown());
    r.h.router.mouse(mouse(MouseType::Down, 20, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, 200, 20, kLeftBit));  // off it: not down
    CHECK(!r.b->isDown());
    r.h.router.mouse(mouse(MouseType::Up, 200, 20, 0));
    CHECK(r.clicks == 1);
    r.h.router.mouse(mouse(MouseType::Down, 20, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, 200, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Move, 30, 20, kLeftBit));  // back on it: down again
    CHECK(r.b->isDown());
    r.h.router.mouse(mouse(MouseType::Up, 30, 20, 0));
    CHECK(r.clicks == 2);
}

TEST(push_button_right_click_does_nothing) {
    ButtonRig r;
    r.h.router.mouse(mouse(MouseType::Down, 20, 20, 1u << static_cast<unsigned>(MouseButton::Right), MouseButton::Right));
    r.h.router.mouse(mouse(MouseType::Up, 20, 20, 0, MouseButton::Right));
    CHECK(r.clicks == 0);
}

TEST(push_button_space_clicks_on_release_and_repeat_is_ignored) {
    ButtonRig r;
    r.b->setFocus();
    r.h.router.key(key(keys::Space));
    CHECK(r.b->isDown());
    r.h.router.key(key(keys::Space, true, Mod::None, true));
    CHECK(r.clicks == 0);
    r.h.router.key(key(keys::Space, false));
    CHECK(r.clicks == 1);
    CHECK(!r.b->isDown());
}

TEST(push_button_space_held_then_focus_lost_cancels) {
    ButtonRig r;
    auto* other = r.h.root.addChild<PushButton>("Other");
    other->setGeometry({100, 10, 80, 26});
    r.b->setFocus();
    r.h.router.key(key(keys::Space));
    other->setFocus();
    CHECK(!r.b->isDown());
    r.h.router.key(key(keys::Space, false));  // goes to `other`, which never saw the press
    CHECK(r.clicks == 0);
}

TEST(push_button_enter_clicks) {
    ButtonRig r;
    r.b->setFocus();
    r.h.router.key(key(keys::Return));
    CHECK(r.clicks == 1);
}

TEST(push_button_disabled_never_clicks) {
    ButtonRig r;
    r.b->setEnabled(false);
    r.h.router.mouse(mouse(MouseType::Down, 20, 20, kLeftBit));
    r.h.router.mouse(mouse(MouseType::Up, 20, 20, 0));
    r.b->click();
    CHECK(r.clicks == 0);
    CHECK(!r.b->accessibleInvoke() || r.clicks == 0);
}

TEST(push_button_alt_mnemonic_focuses_and_clicks) {
    ButtonRig r;
    r.h.router.key(key(keys::Menu, true, Mod::Alt));
    r.h.router.key(key('R', true, Mod::Alt));
    CHECK(r.clicks == 1);
    CHECK(r.b->hasFocus());
}

TEST(push_button_uia_invoke_clicks) {
    ButtonRig r;
    CHECK(r.b->accessibleInvoke());
    CHECK(r.clicks == 1);
    CHECK(r.b->accessibleRole() == Role::Button);
    CHECK(r.b->accessibleName == "Run");
}

TEST(push_button_paints_states) {
    ButtonRig r;
    RecordingPainter idle(1.0);
    r.b->paintTree(idle);
    const PaintOp* fill = firstKind(idle, "fillRoundedRect");
    CHECK(fill && fill->color == token(T::Base));
    const PaintOp* stroke = firstKind(idle, "strokeRoundedRect");
    CHECK(stroke && stroke->width == 1.0f && stroke->color == token(T::BorderStrong));
    const PaintOp* text = firstKind(idle, "text");
    CHECK(text && text->text == "Run" && text->style.color == token(T::Text));

    r.b->setFocus();
    r.h.router.key(key(keys::Space));  // down
    RecordingPainter pressed(1.0);
    r.b->paintTree(pressed);
    CHECK(firstKind(pressed, "fillRoundedRect")->color == token(T::Focus));
    CHECK(firstKind(pressed, "strokeRoundedRect")->width == 2.0f);  // the focus ring
    CHECK(firstKind(pressed, "text")->style.color == token(T::OnAccent));
    r.h.router.key(key(keys::Space, false));

    r.b->setEnabled(false);
    RecordingPainter off(1.0);
    r.b->paintTree(off);
    CHECK(firstKind(off, "fillRoundedRect")->color == token(T::AlternateBase));
    CHECK(firstKind(off, "text")->style.color == token(T::TextFaint));
}

TEST(keyboard_cues_hide_the_underline_until_alt_is_pressed) {
    ButtonRig r;
    RecordingPainter before(1.0);
    r.b->paintTree(before);
    CHECK(countKind(before, "fillRect") == 0);
    r.h.router.key(key(keys::Menu, true, Mod::Alt));
    CHECK(r.b->mnemonicCuesVisible());
    RecordingPainter after(1.0);
    r.b->paintTree(after);
    CHECK(countKind(after, "fillRect") == 1);
    // under the 'R' (the first 6 DIPs of "Run", centred: (80 - 18) / 2 = 31 from the button's left), one pixel thick
    const PaintOp* u = firstKind(after, "fillRect");
    CHECK(u->rect.x == 10 + 31.0f && u->rect.width == 6.0f && u->rect.height == 1.0f);
    Host always;
    always.router.setAlwaysShowCues(true);
    CHECK(always.root.mnemonicCuesVisible());
}

// -- CheckBox -----------------------------------------------------------------------------------------------------

TEST(check_box_toggles_on_click_and_space) {
    Host h;
    auto* c = h.root.addChild<CheckBox>("&Log scale");
    c->setGeometry({0, 0, 120, 20});
    std::vector<std::string> log;
    c->on_toggled = [&](bool on) { log.push_back(on ? "on" : "off"); };
    c->on_clicked = [&] { log.push_back("clicked"); };
    h.router.mouse(mouse(MouseType::Down, 5, 5, kLeftBit));
    CHECK(!c->isChecked());  // not before the release
    h.router.mouse(mouse(MouseType::Up, 5, 5, 0));
    CHECK(c->isChecked());
    h.router.key(key(keys::Space));
    h.router.key(key(keys::Space, false));
    CHECK(!c->isChecked());
    CHECK((log == std::vector<std::string>{"on", "clicked", "off", "clicked"}));
    h.router.key(key(keys::Return));  // Enter is not a check box key
    CHECK(!c->isChecked());
}

TEST(check_box_set_checked_notifies_only_on_change) {
    Host h;
    auto* c = h.root.addChild<CheckBox>("x");
    int n = 0;
    c->on_toggled = [&](bool) { ++n; };
    c->setChecked(true);
    c->setChecked(true);
    CHECK(n == 1);
    c->setChecked(false);  // not exclusive: may be unchecked
    CHECK(!c->isChecked() && n == 2);
}

TEST(check_box_accessibility_is_toggle_not_invoke) {
    Host h;
    auto* c = h.root.addChild<CheckBox>("&Log scale");
    CHECK(c->accessibleRole() == Role::CheckBox);
    CHECK(c->accessibleName == "Log scale");
    CHECK(c->accessibleToggleState() == 0);
    c->accessibleToggle();
    CHECK(c->accessibleToggleState() == 1);
    CHECK(!c->accessibleInvoke());
    CHECK(c->isChecked());
}

TEST(check_box_hint_and_paint) {
    Host h;
    auto* c = h.root.addChild<CheckBox>("Log");  // 14 + 6 + 18 + 2; max(14, 15) + 4
    CHECK(c->sizeHint() == (SizeF{40, 19}));
    c->setGeometry({0, 0, 40, 19});
    c->setChecked(true);
    RecordingPainter p(1.0);
    c->paintTree(p);
    CHECK(firstKind(p, "fillRect")->color == token(T::Accent));
    CHECK(countKind(p, "line") == 2);  // the check mark
    CHECK(firstKind(p, "text")->rect.x == 1 + 14 + 6.0f);
}

// -- RadioButton and ButtonGroup ------------------------------------------------------------------------------------

namespace {
struct RadioRig {
    Host h{1.0, 300, 200};
    RadioButton *a, *b, *c;
    std::vector<std::string> log;
    RadioRig() {
        a = h.root.addChild<RadioButton>("&Boltzmann");
        b = h.root.addChild<RadioButton>("&Fermi-Dirac");
        c = h.root.addChild<RadioButton>("&Incomplete");
        int y = 0;
        for (auto* r : {a, b, c}) {
            r->setGeometry({0, y, 120, 20});
            y += 20;
            r->on_toggled = [this, r](bool on) { log.push_back(r->text() + (on ? " on" : " off")); };
        }
    }
    void clickAt(float y) {
        h.router.mouse(mouse(MouseType::Down, 5, y, kLeftBit));
        h.router.mouse(mouse(MouseType::Up, 5, y, 0));
    }
};
}  // namespace

TEST(radio_siblings_are_auto_exclusive) {
    RadioRig r;
    r.clickAt(5);
    r.clickAt(25);
    CHECK(!r.a->isChecked() && r.b->isChecked() && !r.c->isChecked());
    CHECK((r.log == std::vector<std::string>{"Boltzmann on", "Boltzmann off", "Fermi-Dirac on"}));
}

TEST(radio_checked_one_cannot_be_unchecked) {
    RadioRig r;
    r.b->setChecked(true);
    int clicks = 0;
    r.b->on_clicked = [&] { ++clicks; };
    r.clickAt(25);
    CHECK(r.b->isChecked());
    CHECK(clicks == 1);  // clicked still fires (Qt)
    r.b->setChecked(false);
    CHECK(r.b->isChecked());
}

TEST(radio_group_is_one_tab_stop) {
    RadioRig r;
    auto* after = r.h.root.addChild<PushButton>("After");
    after->setGeometry({0, 100, 80, 26});
    // none checked: the first member is the stop
    CHECK(r.a->isTabStop() && !r.b->isTabStop() && !r.c->isTabStop());
    r.c->setChecked(true);
    CHECK(!r.a->isTabStop() && r.c->isTabStop());
    r.h.router.focusNext(true);
    CHECK(r.c->hasFocus());
    r.h.router.focusNext(true);
    CHECK(after->hasFocus());
    r.h.router.focusNext(true);
    CHECK(r.c->hasFocus());
}

TEST(radio_arrow_keys_move_and_check_wrapping_and_skip_disabled) {
    RadioRig r;
    r.a->setChecked(true);
    r.a->setFocus();
    r.b->setEnabled(false);
    r.h.router.key(key(keys::Down));
    CHECK(r.c->hasFocus() && r.c->isChecked() && !r.a->isChecked());
    r.h.router.key(key(keys::Right));  // wraps past the end, skipping disabled b
    CHECK(r.a->hasFocus() && r.a->isChecked());
    r.h.router.key(key(keys::Up));
    CHECK(r.c->hasFocus() && r.c->isChecked());
}

TEST(radio_arrow_keys_do_nothing_for_check_boxes) {
    Host h;
    auto* x = h.root.addChild<CheckBox>("x");
    auto* y = h.root.addChild<CheckBox>("y");
    x->setGeometry({0, 0, 50, 20});
    y->setGeometry({0, 20, 50, 20});
    x->setFocus();
    CHECK(!h.router.key(key(keys::Down)));
    CHECK(x->hasFocus() && !y->isChecked());
}

TEST(radio_accessibility_is_selection_item) {
    RadioRig r;
    CHECK(r.b->accessibleRole() == Role::RadioButton);
    CHECK(r.b->accessibleSelectionState() == 0);
    r.b->accessibleSelect();
    CHECK(r.b->accessibleSelectionState() == 1);
    CHECK(r.a->accessibleSelectionState() == 0);
    CHECK(!r.b->accessibleInvoke());
    CHECK(r.b->accessibleToggleState() == -1);
}

TEST(radios_under_different_parents_are_independent) {
    Host h;
    auto* p1 = h.root.addChild<Widget>();
    auto* p2 = h.root.addChild<Widget>();
    auto* a = p1->addChild<RadioButton>("a");
    auto* b = p2->addChild<RadioButton>("b");
    a->setChecked(true);
    b->setChecked(true);
    CHECK(a->isChecked() && b->isChecked());
}

TEST(button_group_is_exclusive_across_parents_with_ids) {
    Host h;
    auto* p1 = h.root.addChild<Widget>();
    auto* p2 = h.root.addChild<Widget>();
    auto* a = p1->addChild<RadioButton>("a");
    auto* b = p2->addChild<RadioButton>("b");
    auto* c = p2->addChild<CheckBox>("c");
    ButtonGroup g;
    g.addButton(a, 10);
    g.addButton(b);
    g.addButton(c);
    CHECK(g.id(b) == -2 && g.id(c) == -3);
    std::vector<int> clicked;
    std::vector<std::pair<int, bool>> toggled;
    g.on_id_clicked = [&](int id) { clicked.push_back(id); };
    g.on_id_toggled = [&](int id, bool on) { toggled.emplace_back(id, on); };
    a->click();
    c->click();  // a check box in an exclusive group behaves as a radio
    CHECK(!a->isChecked() && c->isChecked());
    CHECK(g.checkedButton() == c && g.checkedId() == -3);
    CHECK((clicked == std::vector<int>{10, -3}));
    CHECK((toggled == std::vector<std::pair<int, bool>>{{10, true}, {10, false}, {-3, true}}));
    c->setChecked(false);
    CHECK(c->isChecked());
}

TEST(button_group_non_exclusive_lets_members_be_independent) {
    Host h;
    auto* a = h.root.addChild<CheckBox>("a");
    auto* b = h.root.addChild<CheckBox>("b");
    ButtonGroup g;
    g.setExclusive(false);
    g.addButton(a);
    g.addButton(b);
    a->click();
    b->click();
    CHECK(a->isChecked() && b->isChecked());
    a->click();
    CHECK(!a->isChecked());
}

TEST(button_group_lifetimes_either_order) {
    Host h;
    auto* a = h.root.addChild<RadioButton>("a");
    {
        ButtonGroup g;
        g.addButton(a);
        auto* b = h.root.addChild<RadioButton>("b");
        g.addButton(b);
        h.root.release(b).reset();  // the button dies first: it leaves the group
        CHECK(g.buttons().size() == 1);
    }
    CHECK(a->group() == nullptr);  // the group died first: the button is released
    a->click();
    CHECK(a->isChecked());
}

TEST(button_group_adding_a_checked_button_unchecks_the_other) {
    Host h;
    auto* p1 = h.root.addChild<Widget>();
    auto* p2 = h.root.addChild<Widget>();
    auto* a = p1->addChild<RadioButton>("a");
    auto* b = p2->addChild<RadioButton>("b");
    a->setChecked(true);
    b->setChecked(true);
    ButtonGroup g;
    g.addButton(a);
    g.addButton(b);
    CHECK(!a->isChecked() && b->isChecked());
}

// -- Form's addRow(text, field) and buddies -----------------------------------------------------------------------

TEST(form_text_row_makes_a_buddy_label_that_names_the_field) {
    Host h(1.0, 300, 200);
    auto* form = h.root.setLayout<FormLayout>();
    auto* field = h.root.addChild<PushButton>("Pick");
    field->accessibleName.clear();
    auto* l = form->addRow("&Gate voltage [V]", field);
    CHECK(l->shownText() == "Gate voltage [V]");
    CHECK(l->buddy() == field);
    CHECK(field->accessibleName == "Gate voltage [V]");
    h.relayout();
    h.router.key(key('G', true, Mod::Alt));
    CHECK(field->hasFocus());
}

TEST(form_text_row_keeps_a_field_name_already_set) {
    Host h;
    auto* form = h.root.setLayout<FormLayout>();
    auto* field = h.root.addChild<Box>(SizeF{10, 10});
    field->accessibleName = "Voltage";
    form->addRow("V", field);
    CHECK(field->accessibleName == "Voltage");
}

TEST(label_buddy_removed_is_never_touched) {
    Host h;
    auto* field = h.root.addChild<PushButton>("f");
    auto* l = h.root.addChild<Label>("&Name");
    l->setBuddy(field);
    h.root.release(field).reset();
    h.router.key(key('N', true, Mod::Alt));  // must not crash
    CHECK(h.router.focusWidget() == nullptr);
}

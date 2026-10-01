#include "ui_gallery.hpp"

#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/combo_box.hpp"
#include "ui/widgets/group_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/spin_box.hpp"

#include <cmath>

namespace tcad::ui::gallery {

namespace {

using tcad::desktop::theme::T;

// Text in several scripts, wrapped and aligned.
class Scripts final : public Widget {
public:
    Scripts() {
        accessibleName = "Text samples";
        setSizePolicy({SizePolicy::Expanding, SizePolicy::Preferred});
    }
    SizeF sizeHint() const override { return {260, 200}; }
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({0, 0, s.width, s.height}, token(T::Base));
        const auto c = p.crisp({0, 0, s.width, s.height}, 1.0f);
        p.strokeRect(c.rect, token(T::Border), c.width);
        TextStyle t;
        t.color = token(T::Text);
        t.valign = VAlign::Top;
        t.wrap = true;
        p.drawText({8, 6, s.width - 16, 50}, "Wrapped: a semiconductor device simulator draws its UI natively, without Qt.", t);
        t.wrap = false;
        t.size = 15;
        const char* lines[] = {"\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85",
                               "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87",
                               "\xE4\xBD\xA0\xE5\xA5\xBD \xC2\xB7 \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF",
                               "\xC2\xB5m, \xE2\x88\x87\xC2\xB7J = 0  \xF0\x9F\x98\x80"};
        float y = 58;
        for (const char* l : lines) {
            p.drawText({8, y, s.width - 16, 26}, l, t);
            y += 28;
        }
        TextStyle m;
        m.family = FontFamily::Monospace;
        m.color = token(T::TextDim);
        m.valign = VAlign::Top;
        p.drawText({8, y + 2, s.width - 16, 18}, "I = 1.23e-05 A", m);
    }
};

}  // namespace

void build(UiWindow& w, std::function<void(const std::string&)> report) {
    Widget& root = w.root();
    // the status line is created first (every handler writes to it) and placed at the bottom
    auto* line = root.addChild<Label>("Tab moves focus; Alt shows the underlines; Alt+R / Alt+S click; hover a button for its tooltip.");
    line->name = "status";
    auto status = [line, report](const std::string& s) {
        line->setText(s);
        if (report) report(s);
    };
    auto* outer = root.setLayout<BoxLayout>(Orientation::Vertical);
    outer->add<Label>("PyTCAD native UI \xE2\x80\x94 N2/N3 gallery")->setFont(15.0f, true);
    auto* row = outer->addBox(Orientation::Horizontal, 1);

    // left: a form with edits, a grid of edits, buttons and a check box, a stack, a radio group and a wrapped note
    auto* left = row->addBox(Orientation::Vertical, 1);
    auto* form = left->addForm();
    auto edit = [&](const char* id, const char* label, const char* text) {
        auto* e = root.addChild<LineEdit>(w.window().hwnd());
        e->name = id;
        e->accessibleName = label;
        e->setText(text);
        e->on_editing_finished = [e, label, status] { status(std::string("editing finished: ") + label + " = " + e->text()); };
        return e;
    };
    form->addRow("&Gate voltage [V]", edit("gate_voltage", "Gate voltage [V]", "1.5"));
    form->addRow("T&emperature [K]", edit("temperature", "Temperature [K]", "300"));
    // N3b: a decade field taking "1e17" or "5k", a whole-number field, and a slider
    auto* doping = root.addChild<DoubleSpinBox>(w.window().hwnd());
    doping->name = "doping";
    doping->setDecimals(0);
    doping->setRange(-1e21, 1e21);
    doping->setSingleStep(1e14);
    doping->setDecadeStep(true);
    doping->setValue(1e17);
    doping->on_editing_finished = [doping, status] { status("doping = " + doping->text()); };
    doping->on_rejected = [status](const std::string& t, Reject) { status("refused: " + t); };
    form->addRow("&Doping [cm^-3]", doping);
    auto* nodes = root.addChild<SpinBox>(w.window().hwnd());
    nodes->name = "nodes";
    nodes->setRange(2, 2000);
    nodes->setValue(64);
    nodes->on_editing_finished = [nodes, status] { status("nodes = " + nodes->text()); };
    form->addRow("Mesh &nodes", nodes);
    auto* frame = root.addChild<Slider>();
    frame->name = "frame";
    frame->setRange(0, 100);
    frame->setValue(40);
    frame->on_value_changed = [status](int v) { status("frame " + std::to_string(v)); };
    form->addRow("&Frame", frame);
    // N3c: a drop-down in its own popup window, with typeahead
    auto* model = root.addChild<ComboBox>();
    model->name = "model";
    model->addItem("Boltzmann", 1);
    model->addItem("Fermi-Dirac", 2);
    model->addItem("Incomplete ionization", 3);
    model->on_activated = [model, status](int) { status("model: " + model->currentText()); };
    form->addRow("&Model", model);
    auto* grid = left->addGrid();
    const char* axes[] = {"X", "Y", "Z"};
    for (int a = 0; a < 3; ++a) {
        grid->addWidget(root.addChild<Label>(axes[a]), a, 0);
        grid->addWidget(edit((std::string("crop_lo_") + axes[a]).c_str(), "crop from", "0"), a, 1);
        grid->addWidget(edit((std::string("crop_hi_") + axes[a]).c_str(), "crop to", "1"), a, 2);
    }
    auto* buttons = left->addBox(Orientation::Horizontal);
    auto* run = buttons->add<PushButton>("&Run");
    run->name = "run";
    run->toolTip = "Solve the device (Alt+R)";
    auto* stop = buttons->add<PushButton>("&Stop");
    stop->name = "stop";
    stop->toolTip = "Cancel the run (Alt+S)";
    auto* log = buttons->add<CheckBox>("&Log scale");
    log->name = "toggle";
    buttons->addStretch();
    auto* pages = left->add<Widget>();
    auto* stack = pages->setLayout<StackLayout>();
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(pages->addChild<Label>("State: idle"));
    stack->addWidget(pages->addChild<Label>("State: running\xE2\x80\xA6"));
    run->on_clicked = [stack, status] { stack->setCurrentIndex(1), status("clicked: Run"); };
    stop->on_clicked = [stack, status] { stack->setCurrentIndex(0), status("clicked: Stop"); };
    log->on_toggled = [status](bool on) { status(std::string("log scale ") + (on ? "on" : "off")); };
    auto* stats = left->add<GroupBox>("Statistics");
    auto* sl = stats->setLayout<BoxLayout>(Orientation::Horizontal);
    auto* boltzmann = sl->add<RadioButton>("&Boltzmann");
    boltzmann->setChecked(true);
    auto* fermi = sl->add<RadioButton>("&Fermi-Dirac");
    sl->addStretch();
    boltzmann->on_toggled = [status](bool on) {
        if (on) status("statistics: Boltzmann");
    };
    fermi->on_toggled = [status](bool on) {
        if (on) status("statistics: Fermi-Dirac");
    };
    auto* note = left->add<Label>("A wrapped label: its height follows its width (height-for-width), so resizing the window re-flows it.");
    note->setWordWrap(true);
    note->setColor(T::TextDim);
    left->addStretch(1);

    row->addWidget(root.addChild<Scripts>(), 1);  // right: text in several scripts
    outer->addWidget(line);
    w.router().on_tooltip = [status](Widget* wd, PointF) {
        if (wd) status("tooltip: " + wd->toolTip);
    };
}

}  // namespace tcad::ui::gallery

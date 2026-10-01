#include "ui/widgets/label.hpp"

#include "ui/core/clipboard.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/selection.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using tcad::desktop::theme::T;

namespace {

bool inTree(Widget* root, const Widget* w) {
    if (root == w) return true;
    for (auto& c : root->children())
        if (inTree(c.get(), w)) return true;
    return false;
}

}  // namespace

Label::Label(std::string text) : raw_(std::move(text)) { refresh(); }

void Label::refresh() {
    shown_ = buddy_ ? parseMnemonic(raw_) : MnemonicText{raw_, 0, 0, 0};
    accessibleName = shown_.text;
    setMnemonic(shown_.key);
    if (model_) model_->setText(shown_.text);  // a new text drops the selection
    updateGeometry();
    update();
}

void Label::setText(std::string text) {
    if (text == raw_) return;
    raw_ = std::move(text);
    refresh();
}

void Label::setWordWrap(bool on) {
    if (on == wrap_) return;
    wrap_ = on;
    updateGeometry();
    update();
}

void Label::setBuddy(Widget* buddy) {
    buddy_ = buddy;
    refresh();
}

void Label::setFont(float size, bool bold) {
    size_ = size;
    bold_ = bold;
    updateGeometry();
    update();
}

void Label::setColor(T token) {
    color_ = token;
    update();
}

TextStyle Label::style() const {
    TextStyle s;
    s.size = size_;
    s.bold = bold_;
    s.color = token(isEnabled() ? color_ : T::TextFaint);
    s.wrap = wrap_;
    return s;
}

SizeF Label::sizeHint() const {
    TextEngine* te = textEngine();
    if (!te) return {0, 0};
    const SizeF m = te->measure(shown_.text, style());
    if (!wrap_) return {std::ceil(m.width), std::ceil(m.height)};
    const float w = std::min(std::ceil(m.width), std::ceil(Style::standard().wrap_hint_ems * size_));
    return {w, heightForWidth(w)};
}

SizeF Label::minimumSizeHint() const {
    if (!wrap_) return sizeHint();
    TextEngine* te = textEngine();
    if (!te) return {0, 0};
    const TextStyle st = style();
    float widest = 0;
    const std::string& t = shown_.text;
    for (std::size_t i = 0; i < t.size();) {
        const std::size_t end = std::min(t.find_first_of(" \n", i), t.size());
        if (end > i) widest = std::max(widest, te->measure(std::string_view(t).substr(i, end - i), st).width);
        i = end + 1;
    }
    const float w = std::ceil(widest);
    return {w, heightForWidth(w)};
}

float Label::heightForWidth(float width) const {
    TextEngine* te = textEngine();
    if (!te) return 0;
    const TextStyle st = style();
    return std::ceil(wrap_ ? te->measureWrapped(shown_.text, st, width).height : te->measure(shown_.text, st).height);
}

void Label::setSelectable(bool on) {
    if (on == selectable()) return;
    if (on) {
        model_ = std::make_unique<EditModel>([this](std::string_view t) {
            TextEngine* te = textEngine();
            return te ? te->caretStops(t, style()) : std::vector<std::size_t>{};
        });
        model_->setReadOnly(true);
        model_->setText(shown_.text);
        model_->setSelection(0, 0);
        setFocusPolicy(FocusPolicy::Click);
        setCursor(Cursor::IBeam);
    } else {
        model_.reset();
        setFocusPolicy(FocusPolicy::None);
        setCursor(Cursor::Inherit);
    }
    update();
}

void Label::selectAll() {
    if (!model_) return;
    model_->selectAll();
    update();
}

void Label::setSelection(std::size_t from, std::size_t to) {
    if (!model_) return;
    model_->setSelection(from, to);
    update();
}

bool Label::copy() {
    if (!model_ || !model_->hasSelection()) return false;
    Clipboard* cb = host() ? host()->clipboard() : nullptr;
    if (!cb) return false;
    cb->setText(model_->selectedText());
    return true;
}

float Label::textTop() const {
    TextEngine* te = textEngine();
    if (!te) return 0;
    const SizeF s = sizeDips();
    const TextStyle st = style();
    const float h = wrap_ ? te->measureWrapped(shown_.text, st, s.width).height : te->measure(shown_.text, st).height;
    return (s.height - h) / 2;  // the text block is centred vertically, as drawText does it
}

std::size_t Label::offsetAt(PointF local) const {
    TextEngine* te = textEngine();
    if (!te) return 0;
    return model_->snap(te->hitTest(shown_.text, style(), sizeDips().width, {local.x, local.y - textTop()}).offset);
}

std::vector<RectF> Label::selectionRects() const {
    TextEngine* te = textEngine();
    if (!te || !model_ || !model_->hasSelection()) return {};
    const auto [a, b] = model_->selection();
    std::vector<RectF> out = te->selectionRects(shown_.text, style(), sizeDips().width, a, b);
    const float top = textTop();
    for (RectF& r : out) r.y += top;
    return out;
}

bool Label::mouseEvent(const UiMouseEvent& e) {
    using platform::MouseButton;
    using platform::MouseType;
    if (!model_ || !isEnabled()) return false;
    const bool press = e.type == MouseType::Down || e.type == MouseType::DoubleClick;
    if (press && e.button == MouseButton::Left) {
        const std::size_t at = offsetAt(e.pos);
        if (e.clicks >= 2) {
            const auto [a, b] = model_->wordAt(at);
            model_->setSelection(a, b);
        } else {
            model_->setSelection(any(e.mods & platform::Mod::Shift) ? model_->anchor() : at, at);
        }
    } else if (e.type == MouseType::Move && (e.buttons & (1u << static_cast<int>(MouseButton::Left)))) {
        model_->setSelection(model_->anchor(), offsetAt(e.pos));
    } else if (e.type == MouseType::Up && e.button == MouseButton::Right) {
        if (!context_menu_) {
            context_menu_ = std::make_unique<EditContextMenu>(
                EditContextMenu::Kind::Label,
                [this] {
                    EditContextMenu::State s;
                    s.copy = model_ && model_->hasSelection();
                    s.select_all = model_ && !model_->text().empty();
                    return s;
                },
                [this](EditContextMenu::Command c) {
                    if (c == EditContextMenu::Command::Copy) copy();
                    else if (c == EditContextMenu::Command::SelectAll) selectAll();
                });
        }
        context_menu_->show(this, e.pos);
        return true;
    } else {
        return press || e.type == MouseType::Up;
    }
    update();
    return true;
}

bool Label::overridesShortcut(const platform::KeyEvent& e) const {
    return model_ && e.mods == platform::Mod::Ctrl && (e.vk == 'A' || e.vk == 'C');
}

bool Label::keyEvent(const platform::KeyEvent& e) {
    if (!model_ || !e.down) return false;
    if (e.mods == platform::Mod::Ctrl && e.vk == 'A') return selectAll(), true;
    if ((e.mods == platform::Mod::Ctrl && (e.vk == 'C' || e.vk == keys::Insert))) return copy(), true;
    return false;
}

void Label::paint(Painter& p) {
    const SizeF s = sizeDips();
    std::vector<RectF> sel;
    if (model_ && model_->hasSelection()) {
        sel = selectionRects();
        fillSelection(p, sel, hasFocus());
    }
    // a wrapped label's lines are laid out by the text engine: the underline is only drawn for one-line labels (every
    // label with a buddy in the panels is a one-line form label)
    drawMnemonicText(p, textEngine(), {0, 0, s.width, s.height}, shown_, style(), !wrap_ && mnemonicCuesVisible());
    if (!sel.empty())
        redrawSelectedText(p, sel, hasFocus(), [&](Color c) {
            TextStyle st = style();
            st.color = c;
            p.drawText({0, 0, s.width, s.height}, shown_.text, st);
        });
}

void Label::activateMnemonic() {
    if (buddy_ && inTree(root(), buddy_)) buddy_->setFocus(FocusReason::Mnemonic);
}

}  // namespace tcad::ui

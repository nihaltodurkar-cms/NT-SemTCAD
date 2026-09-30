#include "ui/widgets/label.hpp"

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

void Label::paint(Painter& p) {
    const SizeF s = sizeDips();
    // a wrapped label's lines are laid out by the text engine: the underline is only drawn for one-line labels (every
    // label with a buddy in the panels is a one-line form label)
    drawMnemonicText(p, textEngine(), {0, 0, s.width, s.height}, shown_, style(), !wrap_ && mnemonicCuesVisible());
}

void Label::activateMnemonic() {
    if (buddy_ && inTree(root(), buddy_)) buddy_->setFocus(FocusReason::Mnemonic);
}

}  // namespace tcad::ui

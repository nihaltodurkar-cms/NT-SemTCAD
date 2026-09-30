#include "ui/widgets/button.hpp"

#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::Mod;
using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

namespace {

bool inside(const Widget& w, PointF p) {
    const SizeF s = w.sizeDips();
    return p.x >= 0 && p.y >= 0 && p.x < s.width && p.y < s.height;
}

void collectInOrder(Widget* w, const std::vector<AbstractButton*>& set, std::vector<AbstractButton*>& out) {
    if (auto* b = dynamic_cast<AbstractButton*>(w); b && std::find(set.begin(), set.end(), b) != set.end()) out.push_back(b);
    for (auto& c : w->children()) collectInOrder(c.get(), set, out);
}

}  // namespace

// -- AbstractButton ---------------------------------------------------------------------------------------------

AbstractButton::AbstractButton(std::string text) {
    setFocusPolicy(FocusPolicy::Strong);
    setText(std::move(text));
}

AbstractButton::~AbstractButton() {
    if (group_) group_->removeButton(this);
}

void AbstractButton::setText(std::string text) {
    shown_ = parseMnemonic(text);
    accessibleName = shown_.text;
    setMnemonic(shown_.key);
    updateGeometry();
    update();
}

std::vector<AbstractButton*> AbstractButton::exclusiveMembers() const {
    std::vector<AbstractButton*> out;
    auto* self = const_cast<AbstractButton*>(this);
    if (group_) {
        if (!group_->exclusive_) return out;
        std::vector<AbstractButton*> in_tree;
        collectInOrder(self->root(), group_->buttons_, in_tree);
        return in_tree;
    }
    if (!auto_exclusive_) return out;
    if (!parent()) return {self};
    for (auto& c : parent()->children())
        if (auto* b = dynamic_cast<AbstractButton*>(c.get()); b && b->auto_exclusive_ && !b->group_) out.push_back(b);
    return out;
}

void AbstractButton::setCheckedInternal(bool on) {
    checked_ = on;
    update();
    ButtonGroup* g = group_;
    const int id = g ? g->id(this) : 0;
    const auto toggled = on_toggled;  // a handler may destroy this button
    if (toggled) toggled(on);
    if (g && g->on_id_toggled) g->on_id_toggled(id, on);
}

void AbstractButton::setChecked(bool on) {
    if (!checkable_ || on == checked_) return;
    const auto members = exclusiveMembers();
    if (!on && !members.empty()) return;  // the checked button of an exclusive set stays checked (Qt)
    if (on)
        for (AbstractButton* m : members)
            if (m != this && m->checked_) m->setCheckedInternal(false);
    setCheckedInternal(on);
}

void AbstractButton::click() {
    if (!isEnabled()) return;
    ButtonGroup* g = group_;
    const int id = g ? g->id(this) : 0;
    if (checkable_ && !(checked_ && !exclusiveMembers().empty())) setChecked(!checked_);
    const auto clicked = on_clicked;
    if (clicked) clicked();
    if (g && g->on_id_clicked) g->on_id_clicked(id);
}

bool AbstractButton::mouseEvent(const UiMouseEvent& e) {
    const bool left = e.button == MouseButton::Left;
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick:  // Qt: a double click presses again
            if (!left) return false;
            down_ = true;
            key_down_ = false;
            update();
            return true;
        case MouseType::Move:
            if (!(e.buttons & (1u << static_cast<unsigned>(MouseButton::Left))) || key_down_) return false;
            if (down_ != inside(*this, e.pos)) {
                down_ = !down_;
                update();
            }
            return true;
        case MouseType::Up: {
            if (!left || key_down_) return false;
            const bool was = down_;
            down_ = false;
            update();
            if (was && inside(*this, e.pos)) click();
            return true;
        }
        default: return false;
    }
}

bool AbstractButton::keyEvent(const platform::KeyEvent& e) {
    if (e.vk == keys::Space && e.mods == Mod::None) {
        if (e.down) {
            if (!e.repeat && !down_) {
                down_ = key_down_ = true;
                update();
            }
        } else if (key_down_) {
            down_ = key_down_ = false;
            update();
            click();
        }
        return true;
    }
    if (e.down && e.mods == Mod::None && (e.vk == keys::Left || e.vk == keys::Up || e.vk == keys::Right || e.vk == keys::Down))
        return moveInGroup(e.vk == keys::Left || e.vk == keys::Up ? -1 : 1);
    return false;
}

bool AbstractButton::moveInGroup(int step) {
    const auto members = exclusiveMembers();
    if (members.size() < 2) return false;
    const auto n = static_cast<int>(members.size());
    const int at = static_cast<int>(std::find(members.begin(), members.end(), this) - members.begin());
    for (int k = 1; k < n; ++k) {
        AbstractButton* next = members[static_cast<std::size_t>(((at + step * k) % n + n) % n)];
        if (!next->isVisible() || !next->isEnabled()) continue;
        next->setFocus(FocusReason::Other);
        next->click();
        return true;
    }
    return false;
}

void AbstractButton::focusChanged(bool in, FocusReason) {
    if (!in && key_down_) down_ = key_down_ = false;  // Space was held: the press is cancelled
    update();
}

void AbstractButton::activateMnemonic() {
    setFocus(FocusReason::Mnemonic);
    click();
}

bool AbstractButton::isTabStop() const {
    const auto members = exclusiveMembers();
    if (members.empty()) return true;
    for (AbstractButton* m : members)
        if (m->checked_ && m->acceptsFocus(FocusReason::Tab)) return m == this;
    for (AbstractButton* m : members)
        if (m->acceptsFocus(FocusReason::Tab)) return m == this;
    return true;
}

bool AbstractButton::accessibleInvoke() {
    click();
    return true;
}

SizeF AbstractButton::textSize() const {
    TextEngine* te = textEngine();
    return te ? te->measure(shown_.text, TextStyle{}) : SizeF{0, 0};
}

// -- PushButton -------------------------------------------------------------------------------------------------

PushButton::PushButton(std::string text) : AbstractButton(std::move(text)) {
    setSizePolicy({SizePolicy::Minimum, SizePolicy::Fixed});  // QPushButton's
}

SizeF PushButton::sizeHint() const {
    const Style& st = Style::standard();
    const SizeF t = textSize();
    float w = std::ceil(t.width) + 2 * st.button_pad_h;
    if (!text().empty()) w = std::max(w, st.button_min_width);
    return {w, std::ceil(t.height) + 2 * st.button_pad_v};
}

bool PushButton::keyEvent(const platform::KeyEvent& e) {
    if (e.down && !e.repeat && e.mods == Mod::None && e.vk == keys::Return) {
        click();
        return true;
    }
    return AbstractButton::keyEvent(e);
}

void PushButton::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    const bool on = isEnabled();
    const RectF all{0, 0, s.width, s.height};
    const Color fill = token(!on ? T::AlternateBase : isDown() ? T::Focus : isHovered() ? T::AccentSoft : T::Base);
    p.fillRoundedRect(all, st.corner_radius, fill);
    const bool ring = hasFocus() && on;
    const auto c = p.crisp(all, ring ? st.focus_width : st.border_width);
    p.strokeRoundedRect(c.rect, st.corner_radius, token(ring ? T::Focus : on ? T::BorderStrong : T::Border), c.width);
    TextStyle ts;
    ts.color = token(!on ? T::TextFaint : isDown() ? T::OnAccent : T::Text);
    ts.halign = HAlign::Center;
    drawMnemonicText(p, textEngine(), all, label(), ts, mnemonicCuesVisible());
}

// -- CheckBox ---------------------------------------------------------------------------------------------------

CheckBox::CheckBox(std::string text) : AbstractButton(std::move(text)) {
    setCheckable(true);
    setSizePolicy({SizePolicy::Preferred, SizePolicy::Fixed});  // QCheckBox's
}

SizeF CheckBox::sizeHint() const {
    const Style& st = Style::standard();
    const SizeF t = textSize();
    const float text_w = text().empty() ? 0 : st.indicator_spacing + std::ceil(t.width);
    return {st.indicator + text_w + 2, std::max(st.indicator, std::ceil(t.height)) + 4};
}

void CheckBox::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    const bool on = isEnabled(), focus = hasFocus() && on;
    const RectF box{1, std::round((s.height - st.indicator) / 2), st.indicator, st.indicator};
    const Color fill = token(!on ? T::AlternateBase : isChecked() ? T::Accent : isDown() ? T::AccentSoft : T::Base);
    p.fillRect(box, isChecked() && !on ? token(T::BorderStrong) : fill);
    const auto c = p.crisp(box, focus ? st.focus_width : st.border_width);
    p.strokeRect(c.rect, token(focus ? T::Focus : !on ? T::Border : isHovered() || isChecked() ? T::Accent : T::BorderStrong), c.width);
    if (isChecked()) {
        const Color mark = token(T::OnAccent);
        const float x = box.x, y = box.y, k = st.indicator / 14.0f;
        p.drawLine({x + 3 * k, y + 7 * k}, {x + 6 * k, y + 10 * k}, mark, 2 * k);
        p.drawLine({x + 6 * k, y + 10 * k}, {x + 11 * k, y + 4 * k}, mark, 2 * k);
    }
    TextStyle ts;
    ts.color = token(on ? T::Text : T::TextFaint);
    const float tx = box.right() + st.indicator_spacing;
    drawMnemonicText(p, textEngine(), {tx, 0, s.width - tx, s.height}, label(), ts, mnemonicCuesVisible());
}

// -- RadioButton ------------------------------------------------------------------------------------------------

RadioButton::RadioButton(std::string text) : AbstractButton(std::move(text)) {
    setCheckable(true);
    setAutoExclusive(true);  // Qt: radio buttons with one parent are exclusive
    setSizePolicy({SizePolicy::Preferred, SizePolicy::Fixed});
}

SizeF RadioButton::sizeHint() const {
    const Style& st = Style::standard();
    const SizeF t = textSize();
    const float text_w = text().empty() ? 0 : st.indicator_spacing + std::ceil(t.width);
    return {st.indicator + text_w + 2, std::max(st.indicator, std::ceil(t.height)) + 4};
}

void RadioButton::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    const bool on = isEnabled(), focus = hasFocus() && on;
    const RectF ring{1, std::round((s.height - st.indicator) / 2), st.indicator, st.indicator};
    p.fillEllipse(ring, token(focus ? T::Focus : !on ? T::Border : isHovered() || isChecked() ? T::Accent : T::BorderStrong));
    const float w = focus ? st.focus_width : st.border_width;
    p.fillEllipse({ring.x + w, ring.y + w, ring.width - 2 * w, ring.height - 2 * w},
                  token(!on ? T::AlternateBase : isDown() ? T::AccentSoft : T::Base));
    if (isChecked()) {
        const float d = st.indicator * 3 / 7;
        p.fillEllipse({ring.x + (ring.width - d) / 2, ring.y + (ring.height - d) / 2, d, d}, token(on ? T::Accent : T::BorderStrong));
    }
    TextStyle ts;
    ts.color = token(on ? T::Text : T::TextFaint);
    const float tx = ring.right() + st.indicator_spacing;
    drawMnemonicText(p, textEngine(), {tx, 0, s.width - tx, s.height}, label(), ts, mnemonicCuesVisible());
}

// -- ButtonGroup ------------------------------------------------------------------------------------------------

ButtonGroup::~ButtonGroup() {
    for (AbstractButton* b : buttons_) b->group_ = nullptr;
}

void ButtonGroup::addButton(AbstractButton* b, int id) {
    if (!b || b->group_ == this) return;
    if (b->group_) b->group_->removeButton(b);
    buttons_.push_back(b);
    ids_.push_back(id == -1 ? next_auto_id_-- : id);
    b->group_ = this;
    if (exclusive_ && b->checked_)
        for (AbstractButton* o : buttons_)
            if (o != b && o->checked_) o->setCheckedInternal(false);
}

void ButtonGroup::removeButton(AbstractButton* b) {
    const auto it = std::find(buttons_.begin(), buttons_.end(), b);
    if (it == buttons_.end()) return;
    ids_.erase(ids_.begin() + (it - buttons_.begin()));
    buttons_.erase(it);
    b->group_ = nullptr;
}

AbstractButton* ButtonGroup::checkedButton() const {
    for (AbstractButton* b : buttons_)
        if (b->checked_) return b;
    return nullptr;
}

int ButtonGroup::checkedId() const {
    const AbstractButton* b = checkedButton();
    return b ? id(b) : -1;
}

int ButtonGroup::id(const AbstractButton* b) const {
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i] == b) return ids_[i];
    return -1;
}

AbstractButton* ButtonGroup::button(int id) const {
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (ids_[i] == id) return buttons_[i];
    return nullptr;
}

}  // namespace tcad::ui

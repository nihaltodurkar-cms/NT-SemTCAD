#include "ui/widgets/tool_bar.hpp"

#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/mnemonic.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

// -- a button -----------------------------------------------------------------------------------------------------------

ToolButton::ToolButton(ToolBar* bar, Action* action) : bar_(bar), action_(action) {
    setFocusPolicy(FocusPolicy::Tab);  // keyboard only: a click on a tool bar does not move the focus (Windows')
    listener_ = action_->addListener([this] { refresh(); });
    refresh();
}

ToolButton::~ToolButton() {
    if (action_ && listener_ >= 0) action_->removeListener(listener_);
}

void ToolButton::refresh() {
    const std::string shown = parseMnemonic(action_->text()).text;
    accessibleName = shown;
    toolTip = action_->toolTip().empty() ? shown : action_->toolTip();
    if (action_->hasShortcut()) toolTip += " (" + action_->shortcutText() + ")";
    setEnabled(action_->isEnabled());
    setVisible(action_->isVisible());
    updateGeometry();
    bar_->layoutItems();  // a button that came, went or changed size moves the others
    update();
}

bool ToolButton::isTabStop() const { return bar_->roving_ == this; }

SizeF ToolButton::sizeHint() const {
    TextEngine* te = textEngine();
    const SizeF t = te ? te->measure(parseMnemonic(action_->text()).text, TextStyle{}) : SizeF{40, 16};
    return {std::ceil(t.width) + 2 * kPadH, std::ceil(t.height) + 2 * kPadV};
}

void ToolButton::paint(Painter& p) {
    const SizeF s = sizeDips();
    const bool on = isEnabled(), lit = on && (isHovered() || down_), checked = action_->isCheckable() && action_->isChecked();
    const bool hc = highContrast().on;
    if (checked || (lit && down_)) p.fillRoundedRect({0, 0, s.width, s.height}, 3, token(T::Selection));
    else if (lit) p.fillRoundedRect({0, 0, s.width, s.height}, 3, token(T::AlternateBase));
    if (lit || checked) {
        const auto c = p.crisp({0, 0, s.width, s.height}, 1.0f);
        p.strokeRoundedRect(c.rect, 3, token(T::BorderStrong), c.width);
    }
    TextStyle st;
    st.valign = VAlign::Center;
    st.halign = HAlign::Center;
    st.color = token(!on ? T::TextFaint : ((checked || down_) && hc) ? T::OnAccent : T::Text);
    p.drawText({kPadH, 0, std::max(0.0f, s.width - 2 * kPadH), s.height}, parseMnemonic(action_->text()).text, st);
    if (hasFocus()) {
        const auto f = p.crisp({2, 2, std::max(0.0f, s.width - 4), std::max(0.0f, s.height - 4)}, 1.0f);
        p.strokeRect(f.rect, token(T::Focus), f.width);
    }
}

bool ToolButton::accessibleInvoke() {
    if (!isEnabled()) return false;
    action_->trigger();
    return true;
}

bool ToolButton::mouseEvent(const UiMouseEvent& e) {
    if (e.button != MouseButton::Left && e.type != MouseType::Move) return false;
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (!isEnabled()) return true;
            down_ = true;
            bar_->roving_ = this;
            update();
            return true;
        case MouseType::Move:
            if (down_) update();
            return false;
        case MouseType::Up: {
            const bool was = down_;
            down_ = false;
            update();
            const SizeF s = sizeDips();
            if (was && e.pos.x >= 0 && e.pos.y >= 0 && e.pos.x < s.width && e.pos.y < s.height) accessibleInvoke();  // released on it
            return true;
        }
        default: return false;
    }
}

bool ToolButton::keyEvent(const KeyEvent& e) {
    if (!e.down) return false;
    switch (e.vk) {
        case keys::Space:
        case keys::Return:
            if (!e.repeat) accessibleInvoke();
            return true;
        case keys::Left: bar_->moveRoving(-1); return true;
        case keys::Right: bar_->moveRoving(+1); return true;
        default: return false;
    }
}

// -- the bar ------------------------------------------------------------------------------------------------------------

class ToolBar::Separator : public Widget {
public:
    SizeF sizeHint() const override { return {ToolBar::kSeparator, 16}; }
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({std::floor(s.width / 2), 3, 1, std::max(0.0f, s.height - 6)}, token(T::Border));
    }
    Role accessibleRole() const override { return Role::Separator; }
};

ToolBar::ToolBar() {
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Fixed});
    name = "tool_bar";
    accessibleName = "Tool bar";
}

ToolButton* ToolBar::addAction(Action* a) {
    if (!a) return nullptr;
    auto* b = addChild<ToolButton>(this, a);
    items_.push_back(b);
    if (!roving_) roving_ = b;
    layoutItems();
    updateGeometry();
    return b;
}

void ToolBar::addSeparator() {
    items_.push_back(addChild<Separator>());
    layoutItems();
    updateGeometry();
}

Widget* ToolBar::addWidget(std::unique_ptr<Widget> w) {
    Widget* raw = adopt(std::move(w));
    if (raw) {
        items_.push_back(raw);
        layoutItems();
        updateGeometry();
    }
    return raw;
}

int ToolBar::buttonCount() const {
    int n = 0;
    for (Widget* w : items_) n += dynamic_cast<ToolButton*>(w) != nullptr;
    return n;
}

ToolButton* ToolBar::button(int i) const {
    int n = 0;
    for (Widget* w : items_)
        if (auto* b = dynamic_cast<ToolButton*>(w))
            if (n++ == i) return b;
    return nullptr;
}

SizeF ToolBar::sizeHint() const {
    float w = 2 * kMargin, h = 0;
    bool first = true;
    for (Widget* it : items_) {
        if (!it->isVisibleSelf()) continue;
        const SizeF s = it->sizeHint();
        w += s.width + (first ? 0 : kSpacing);
        h = std::max(h, s.height);
        first = false;
    }
    return {w, h + 2 * kMargin};
}

void ToolBar::layoutItems() {
    const double s = scale();
    const RectI g = geometry();
    float x = kMargin;
    for (Widget* it : items_) {
        if (!it->isVisibleSelf()) continue;
        const SizeF h = it->sizeHint();
        const int x0 = roundPx(x, s), x1 = roundPx(x + h.width, s);
        const int y0 = roundPx(kMargin, s), y1 = std::max(y0 + 1, g.height - roundPx(kMargin, s));
        it->setGeometry({x0, y0, std::max(1, x1 - x0), y1 - y0});
        x += h.width + kSpacing;
    }
}

void ToolBar::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));
    p.fillRect({0, s.height - 1, s.width, 1}, token(T::Border));
}

void ToolBar::moveRoving(int dir) {
    std::vector<ToolButton*> bs;
    for (Widget* w : items_)
        if (auto* b = dynamic_cast<ToolButton*>(w))
            if (b->isVisibleSelf() && b->isEnabled()) bs.push_back(b);
    if (bs.empty()) return;
    auto it = std::find(bs.begin(), bs.end(), roving_);
    std::size_t i = it == bs.end() ? 0 : static_cast<std::size_t>(it - bs.begin());
    i = (i + bs.size() + static_cast<std::size_t>(dir > 0 ? 1 : bs.size() - 1)) % bs.size();
    roving_ = bs[i];
    roving_->setFocus(FocusReason::Other);
    for (ToolButton* b : bs) b->update();
}

// -- the tool tip -------------------------------------------------------------------------------------------------------

SizeF ToolTipLabel::sizeHint() const {
    TextEngine* te = textEngine();
    if (!te) return {kMaxWidth, 24};
    TextStyle st;
    st.wrap = true;
    const float inner = kMaxWidth - 2 * kPad;
    const SizeF one = te->measure(text_, TextStyle{});
    const float w = std::min(std::ceil(one.width), inner);
    const SizeF m = one.width <= inner ? one : te->measureWrapped(text_, st, inner);
    return {std::min(kMaxWidth, std::ceil(std::max(w, m.width)) + 2 * kPad), std::ceil(m.height) + 2 * kPad - 2};
}

void ToolTipLabel::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    const auto b = p.crisp({0, 0, s.width, s.height}, 1.0f);
    p.strokeRect(b.rect, token(T::BorderStrong), b.width);
    TextStyle st;
    st.color = token(T::Text);
    st.valign = VAlign::Top;
    st.wrap = true;
    p.drawText({kPad, kPad - 1, std::max(0.0f, s.width - 2 * kPad), std::max(0.0f, s.height - 2 * kPad + 2)}, text_, st);
}

}  // namespace tcad::ui

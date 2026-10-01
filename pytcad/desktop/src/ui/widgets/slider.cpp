#include "ui/widgets/slider.hpp"

#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

Slider::Slider() {
    setFocusPolicy(FocusPolicy::Strong);
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Fixed});  // QSlider's horizontal default
}

bool Slider::setClamped(int v, bool notify) {
    v = std::clamp(v, min_, max_);
    if (v == value_) return false;
    value_ = v;
    update();
    if (notify) {
        notifyRangeChanged();
        if (on_value_changed) on_value_changed(value_);
    }
    return true;
}

void Slider::setRange(int minimum, int maximum) {
    min_ = minimum;
    max_ = std::max(minimum, maximum);
    const int before = value_;
    value_ = std::clamp(value_, min_, max_);
    update();
    if (value_ != before) {
        notifyRangeChanged();
        if (on_value_changed) on_value_changed(value_);
    }
}

void Slider::setValue(int v) { setClamped(v, true); }
void Slider::setValueSilent(int v) {
    setClamped(v, false);
    notifyRangeChanged();
}

RectF Slider::handleRect() const {
    const SizeF s = sizeDips();
    const float travel = std::max(0.0f, s.width - kHandle);
    const float t = max_ > min_ ? static_cast<float>(value_ - min_) / static_cast<float>(max_ - min_) : 0.0f;
    return {std::round(t * travel), std::round((s.height - kHandle) / 2), kHandle, kHandle};
}

int Slider::valueAtX(float x) const {
    const SizeF s = sizeDips();
    const float travel = s.width - kHandle;
    if (travel <= 0 || max_ <= min_) return min_;
    const double t = std::clamp(static_cast<double>(x - kHandle / 2) / travel, 0.0, 1.0);
    return min_ + static_cast<int>(std::lround(t * (max_ - min_)));
}

SizeF Slider::sizeHint() const { return {kLength, kThickness}; }
SizeF Slider::minimumSizeHint() const { return {2 * kHandle, kThickness}; }

void Slider::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    const bool on = isEnabled(), focus = hasFocus() && on;
    const float cy = std::round(s.height / 2);
    const RectF groove{kHandle / 2, cy - kGroove / 2, std::max(0.0f, s.width - kHandle), kGroove};
    p.fillRoundedRect(groove, kGroove / 2, token(T::AlternateBase));
    const auto gc = p.crisp(groove, st.border_width);
    p.strokeRoundedRect(gc.rect, kGroove / 2, token(on ? T::BorderStrong : T::Border), gc.width);
    const RectF h = handleRect();
    const float done = h.x + kHandle / 2 - groove.x;  // the part of the groove up to the handle's centre
    if (done > 0 && on) p.fillRoundedRect({groove.x, groove.y, std::min(done, groove.width), groove.height}, kGroove / 2, token(T::Accent));
    const Color fill = token(!on ? T::AlternateBase : dragging_ ? T::Focus : isHovered() ? T::AccentSoft : T::Base);
    p.fillRoundedRect(h, kHandle / 2, fill);
    const auto hc = p.crisp(h, focus ? st.focus_width : st.border_width);
    p.strokeRoundedRect(hc.rect, kHandle / 2, token(focus ? T::Focus : on ? T::BorderStrong : T::Border), hc.width);
}

bool Slider::mouseEvent(const UiMouseEvent& e) {
    const bool left = e.button == MouseButton::Left;
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (!left || !isEnabled()) return false;
            {
                const RectF h = handleRect();
                const bool on_handle = e.pos.x >= h.x && e.pos.x < h.right() && e.pos.y >= h.y && e.pos.y < h.bottom();
                grab_ = on_handle ? e.pos.x - (h.x + kHandle / 2) : 0.0f;
                dragging_ = true;
                setClamped(valueAtX(e.pos.x - grab_), true);
                update();
            }
            return true;
        case MouseType::Move:
            if (!dragging_) return false;
            setClamped(valueAtX(e.pos.x - grab_), true);
            return true;
        case MouseType::Up:
            if (!left || !dragging_) return false;
            dragging_ = false;
            update();
            return true;
        case MouseType::Wheel: {
            if (!hasFocus() || !isEnabled() || e.wheel_steps == 0) return false;
            const int per_notch = std::min(single_ * 3, page_);
            const int notches = static_cast<int>(std::lround(e.wheel_steps));
            setClamped(value_ + (notches != 0 ? notches : (e.wheel_steps > 0 ? 1 : -1)) * per_notch, true);
            return true;
        }
        default: return false;
    }
}

bool Slider::keyEvent(const platform::KeyEvent& e) {
    if (!e.down || !isEnabled() || any(e.mods & (platform::Mod::Ctrl | platform::Mod::Alt))) return false;
    switch (e.vk) {
        case keys::Left:
        case keys::Down: setClamped(value_ - single_, true); return true;
        case keys::Right:
        case keys::Up: setClamped(value_ + single_, true); return true;
        case keys::PageDown: setClamped(value_ - page_, true); return true;
        case keys::PageUp: setClamped(value_ + page_, true); return true;
        case keys::Home: setClamped(min_, true); return true;
        case keys::End: setClamped(max_, true); return true;
        default: return false;
    }
}

AccessibleRange Slider::accessibleRange() const {
    AccessibleRange r;
    r.valid = true;
    r.value = value_;
    r.minimum = min_;
    r.maximum = max_;
    r.small_step = single_;
    r.large_step = page_;
    return r;
}

bool Slider::accessibleSetRangeValue(double v) {
    if (!std::isfinite(v) || v < min_ || v > max_ || v != std::floor(v)) return false;  // refused, not clamped
    setValue(static_cast<int>(v));
    return true;
}

}  // namespace tcad::ui

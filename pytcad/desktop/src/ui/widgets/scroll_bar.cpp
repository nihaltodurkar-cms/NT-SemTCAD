#include "ui/widgets/scroll_bar.hpp"

#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

ScrollBar::ScrollBar(Orientation o) : orientation_(o) {
    setSizePolicy(o == Orientation::Vertical ? Policy{SizePolicy::Fixed, SizePolicy::Expanding}
                                             : Policy{SizePolicy::Expanding, SizePolicy::Fixed});
}

bool ScrollBar::setClamped(int v, bool notify) {
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

void ScrollBar::setRange(int minimum, int maximum) {
    min_ = minimum;
    max_ = std::max(minimum, maximum);
    const int before = value_;
    value_ = std::clamp(value_, min_, max_);
    update();
    notifyRangeChanged();
    if (value_ != before && on_value_changed) on_value_changed(value_);
}

void ScrollBar::setValue(int v) { setClamped(v, true); }

void ScrollBar::setValueSilent(int v) {
    setClamped(v, false);
    notifyRangeChanged();
}

float ScrollBar::length() const {
    const SizeF s = sizeDips();
    return orientation_ == Orientation::Vertical ? s.height : s.width;
}

RectF ScrollBar::trackRect() const {
    const SizeF s = sizeDips();
    return {0, 0, s.width, s.height};
}

RectF ScrollBar::thumbRect() const {
    const SizeF s = sizeDips();
    const float len = length();
    if (!isNeeded()) return {0, 0, 0, 0};
    const double range = static_cast<double>(max_ - min_);
    const float thumb = std::clamp(static_cast<float>(len * page_ / (range + page_)), std::min(kMinThumb, len), len);
    const float travel = std::max(0.0f, len - thumb);
    const float start = std::round(static_cast<float>((value_ - min_) / range) * travel);
    return orientation_ == Orientation::Vertical ? RectF{0, start, s.width, thumb} : RectF{start, 0, thumb, s.height};
}

int ScrollBar::valueAtThumbStart(float pos) const {
    if (!isNeeded()) return min_;
    const RectF t = thumbRect();
    const float thumb = orientation_ == Orientation::Vertical ? t.height : t.width;
    const float travel = length() - thumb;
    if (travel <= 0) return min_;
    const double f = std::clamp(static_cast<double>(pos) / travel, 0.0, 1.0);
    return min_ + static_cast<int>(std::lround(f * (max_ - min_)));
}

SizeF ScrollBar::sizeHint() const {
    return orientation_ == Orientation::Vertical ? SizeF{kThickness, 3 * kMinThumb} : SizeF{3 * kMinThumb, kThickness};
}

void ScrollBar::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::AlternateBase));
    const auto edge = p.crisp({0, 0, s.width, s.height}, st.border_width);
    p.strokeRect(edge.rect, token(T::Border), edge.width);
    if (!isNeeded()) return;
    RectF t = thumbRect();
    t = {t.x + 2, t.y + 2, std::max(0.0f, t.width - 4), std::max(0.0f, t.height - 4)};  // inset inside the track
    const bool on = isEnabled();
    const Color fill = token(!on ? T::Border : dragging_ ? T::Focus : isHovered() ? T::TextDim : T::BorderStrong);
    p.fillRoundedRect(t, std::min(t.width, t.height) / 2, fill);
}

void ScrollBar::stopRepeat() {
    if (repeat_) stopTimer(repeat_);
    repeat_ = 0;
}

void ScrollBar::pageToward(float pos) {
    const RectF t = thumbRect();
    const float start = orientation_ == Orientation::Vertical ? t.y : t.x;
    const float end = start + (orientation_ == Orientation::Vertical ? t.height : t.width);
    if (pos < start) setClamped(value_ - page_, true);
    else if (pos >= end) setClamped(value_ + page_, true);
    else stopRepeat();  // the thumb has reached the pointer
}

bool ScrollBar::mouseEvent(const UiMouseEvent& e) {
    const bool left = e.button == MouseButton::Left;
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick: {
            if (!left || !isEnabled() || !isNeeded()) return left;  // a bar with nothing to scroll still takes the press
            const RectF t = thumbRect();
            const float pos = along(e.pos);
            const float start = orientation_ == Orientation::Vertical ? t.y : t.x;
            const float len = orientation_ == Orientation::Vertical ? t.height : t.width;
            if (pos >= start && pos < start + len) {
                dragging_ = true;
                grab_ = pos - start;
                update();
            } else {
                held_pos_ = pos;
                pageToward(pos);
                stopRepeat();
                repeat_ = startTimer(kRepeatDelayMs, false, [this] {
                    pageToward(held_pos_);
                    if (repeat_) {  // still pressed and not yet at the pointer: now at the fast rate
                        stopRepeat();
                        repeat_ = startTimer(kRepeatMs, true, [this] { pageToward(held_pos_); });
                    }
                });
            }
            return true;
        }
        case MouseType::Move:
            if (dragging_) {
                setClamped(valueAtThumbStart(along(e.pos) - grab_), true);
                return true;
            }
            if (repeat_) held_pos_ = along(e.pos);
            return false;
        case MouseType::Up:
            if (!left) return false;
            if (!dragging_ && !repeat_) return false;
            dragging_ = false;
            stopRepeat();
            update();
            return true;
        case MouseType::Wheel: {
            if (!isEnabled() || e.wheel_steps == 0) return false;
            const int notches = static_cast<int>(std::lround(e.wheel_steps));
            setClamped(value_ - (notches != 0 ? notches : (e.wheel_steps > 0 ? 1 : -1)) * single_ * 3, true);  // away = up
            return true;
        }
        default: return false;
    }
}

AccessibleRange ScrollBar::accessibleRange() const {
    AccessibleRange r;
    r.valid = true;
    r.value = value_;
    r.minimum = min_;
    r.maximum = max_;
    r.small_step = single_;
    r.large_step = page_;
    return r;
}

bool ScrollBar::accessibleSetRangeValue(double v) {
    if (!std::isfinite(v) || v < min_ || v > max_ || v != std::floor(v)) return false;
    setValue(static_cast<int>(v));
    return true;
}

}  // namespace tcad::ui

#include "ui/win32/spin_box.hpp"

#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/numeric.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

NumberField::NumberField(HWND window, bool integral) : integral_(integral) {
    setSizePolicy({SizePolicy::Minimum, SizePolicy::Fixed});  // QAbstractSpinBox's default
    decimals_ = integral ? 0 : 2;
    edit_ = addChild<LineEdit>(window);
    setFocusProxy(edit_);  // a form label's Alt+<letter> reaches the edit
    edit_->on_editing_finished = [this] { commit(); };
    edit_->on_text_changed = [this] { textTyped(); };
    edit_->on_focus_changed = [this](bool in, FocusReason why) {
        if (!in) {
            stopRepeat();
            down_ = 0;
        } else if (why == FocusReason::Tab || why == FocusReason::Backtab || why == FocusReason::Mnemonic) {
            edit_->model().selectAll();  // Qt: tabbing into a spin box selects its text
            edit_->update();
        }
    };
    refreshText();
}

std::string NumberField::display(double v) const {
    if (!special_.empty() && v == min_) return special_;
    return formatQuantity(v, decimals_, unit_, integral_ ? Notation::Fixed : Notation::Auto);
}

void NumberField::refreshText() {
    refreshing_ = true;
    shown_ = display(value_);
    if (edit_->text() != shown_) edit_->setText(shown_);
    edit_->setInvalid(false);
    refreshing_ = false;
}

bool NumberField::setValueImpl(double v, bool notify, bool user) {
    const bool exact = v >= min_ && v <= max_;
    v = std::clamp(v, min_, max_);
    if (integral_) v = std::round(v);
    const bool changed = v != value_;
    value_ = v;
    if (!user) committed_ = value_;
    refreshText();
    if (changed) {
        notifyRangeChanged();
        if (notify) emitValueChanged();
    }
    return exact;
}

void NumberField::setRangeImpl(double lo, double hi) {
    min_ = lo;
    max_ = std::max(lo, hi);
    const double before = value_;
    value_ = std::clamp(value_, min_, max_);
    committed_ = std::clamp(committed_, min_, max_);
    refreshText();
    updateGeometry();
    if (value_ != before) {
        notifyRangeChanged();
        emitValueChanged();
    }
}

void NumberField::setSuffix(std::string unit) {
    unit_ = std::move(unit);
    refreshText();
    updateGeometry();
}

void NumberField::setSingleStep(double s) { step_ = std::fabs(s); }

NumberField::Outcome NumberField::evaluate(const std::string& text) const {
    Outcome o;
    if (!special_.empty() && text == special_) {
        o.ok = true;
        o.value = min_;
        return o;
    }
    const Parsed p = parseQuantity(text, unit_);
    if (p.status == ParseStatus::Empty || p.status == ParseStatus::Incomplete) {
        o.neutral = true;
        return o;
    }
    if (p.status == ParseStatus::Invalid) return o;
    double v = p.value;
    if (integral_) {
        const double r = std::round(v);
        if (std::fabs(v - r) > 1e-9 * std::max(1.0, std::fabs(v))) {
            o.why = Reject::NotWhole;
            return o;
        }
        v = r;
    }
    if (v < min_ || v > max_) {
        o.why = Reject::OutOfRange;
        return o;
    }
    o.ok = true;
    o.value = v;
    return o;
}

void NumberField::textTyped() {
    if (refreshing_) return;
    const Outcome o = evaluate(edit_->text());
    edit_->setInvalid(!o.ok && !o.neutral);
}

void NumberField::fireIfChanged() {
    if (value_ == committed_) return;
    committed_ = value_;
    if (on_editing_finished) on_editing_finished();
}

bool NumberField::commit() {
    bool accepted = true;
    const std::string typed = edit_->text();
    if (typed != shown_) {
        const Outcome o = evaluate(typed);
        if (o.ok) {
            setValueImpl(o.value, true, true);
        } else {
            accepted = false;
            refreshText();
            if (on_rejected) on_rejected(typed, o.neutral ? Reject::NotANumber : o.why);
        }
        refreshText();
    }
    fireIfChanged();
    return accepted;
}

void NumberField::stepBy(int n) {
    double base = value_;
    if (edit_->text() != shown_) {
        const Outcome o = evaluate(edit_->text());
        if (o.ok) base = o.value;
    }
    double v = decade_ ? stepDecade(base, n, step_) : stepLinear(base, n, step_);
    v = std::clamp(v, min_, max_);
    setValueImpl(v, true, true);
    refreshText();  // a step also drops whatever was typed
}

// -- the buttons ------------------------------------------------------------------------------------------------

RectF NumberField::buttonRect(int which) const {
    const SizeF s = sizeDips();
    const float half = std::floor(s.height / 2);
    const float x = s.width - kButtonWidth;
    return which == 1 ? RectF{x, 0, kButtonWidth, half} : RectF{x, half, kButtonWidth, s.height - half};
}

int NumberField::buttonAt(PointF p) const {
    for (int k = 1; k <= 2; ++k) {
        const RectF r = buttonRect(k);
        if (p.x >= r.x && p.x < r.right() && p.y >= r.y && p.y < r.bottom()) return k;
    }
    return 0;
}

void NumberField::stopRepeat() {
    if (repeat_) stopTimer(repeat_);
    repeat_ = 0;
}

void NumberField::startRepeat(int direction) {
    stopRepeat();
    repeat_dir_ = direction;
    repeat_ = startTimer(kRepeatDelayMs, false, [this] {
        repeat_ = 0;
        stepBy(repeat_dir_);
        repeat_ = startTimer(kRepeatRateMs, true, [this] { stepBy(repeat_dir_); });
    });
}

void NumberField::resized() {
    const RectI g = geometry();
    const int bw = static_cast<int>(std::lround(kButtonWidth * scale()));
    edit_->setGeometry({0, 0, std::max(0, g.width - bw), g.height});
}

SizeF NumberField::sizeHint() const {
    float w = 0;
    if (TextEngine* te = textEngine()) {
        TextStyle ts;
        ts.size = Style::standard().font_size;
        w = std::max({te->measure(display(min_), ts).width, te->measure(display(max_), ts).width, te->measure("00000000", ts).width});
    } else {
        w = 60;
    }
    return {std::ceil(w) + 2 * LineEdit::kPadding + kButtonWidth, edit_->sizeHint().height};
}

void NumberField::paint(Painter& p) {
    const Style& st = Style::standard();
    const bool on = isEnabled();
    for (int k = 1; k <= 2; ++k) {
        const RectF r = buttonRect(k);
        const bool at_limit = k == 1 ? value_ >= max_ : value_ <= min_;
        const bool pressed = down_ == k && down_inside_;
        const Color fill = token(!on || at_limit ? T::AlternateBase : pressed ? T::Focus : hot_ == k ? T::AccentSoft : T::Base);
        p.fillRect(r, fill);
        const auto c = p.crisp(r, st.border_width);
        p.strokeRect(c.rect, token(on ? T::BorderStrong : T::Border), c.width);
        const Color ink = token(!on || at_limit ? T::TextFaint : pressed ? T::OnAccent : T::Text);
        const float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
        if (k == 1) p.fillPolygon({{cx - 3.5f, cy + 1.5f}, {cx + 3.5f, cy + 1.5f}, {cx, cy - 2}}, ink);
        else p.fillPolygon({{cx - 3.5f, cy - 1.5f}, {cx + 3.5f, cy - 1.5f}, {cx, cy + 2}}, ink);
    }
}

void NumberField::hoverChanged(bool entered) {
    if (!entered && hot_) {
        hot_ = 0;
        update();
    }
}

bool NumberField::mouseEvent(const UiMouseEvent& e) {
    const bool left = e.button == MouseButton::Left;
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick: {
            const int which = buttonAt(e.pos);
            if (!left || !which || !isEnabled()) return false;
            down_ = which;
            down_inside_ = true;
            edit_->setFocus(FocusReason::Mouse);
            stepBy(which == 1 ? 1 : -1);
            startRepeat(which == 1 ? 1 : -1);
            update();
            return true;
        }
        case MouseType::Move: {
            const int which = buttonAt(e.pos);
            if (which != hot_) {
                hot_ = which;
                update();
            }
            if (!down_) return which != 0;
            const bool inside = which == down_;
            if (inside != down_inside_) {  // Qt: the repeat runs only while the pointer is over the pressed button
                down_inside_ = inside;
                if (inside) startRepeat(down_ == 1 ? 1 : -1);
                else stopRepeat();
                update();
            }
            return true;
        }
        case MouseType::Up:
            if (!left || !down_) return false;
            down_ = 0;
            stopRepeat();
            update();
            return true;
        case MouseType::Wheel: {
            if (!edit_->hasFocus() || !isEnabled() || e.wheel_steps == 0) return false;
            const int notches = std::max(1, static_cast<int>(std::lround(std::fabs(e.wheel_steps))));
            stepBy(e.wheel_steps > 0 ? notches : -notches);
            return true;
        }
        default: return false;
    }
}

bool NumberField::keyEvent(const platform::KeyEvent& e) {
    if (!e.down || !isEnabled() || any(e.mods & (platform::Mod::Ctrl | platform::Mod::Alt))) return false;
    switch (e.vk) {
        case keys::Up: stepBy(1); return true;
        case keys::Down: stepBy(-1); return true;
        case keys::PageUp: stepBy(decade_ ? 1 : 10); return true;
        case keys::PageDown: stepBy(decade_ ? -1 : -10); return true;
        case keys::Escape:
            if (edit_->text() == shown_) return false;  // nothing pending: Escape is someone else's (a dialog's)
            refreshText();
            return true;
        default: return false;
    }
}

// -- accessibility ----------------------------------------------------------------------------------------------

bool NumberField::accessibleSetValue(std::string_view v) {
    edit_->setText(v);
    return commit();
}

AccessibleRange NumberField::accessibleRange() const {
    AccessibleRange r;
    r.valid = true;
    r.value = value_;
    r.minimum = min_;
    r.maximum = max_;
    r.small_step = step_;
    r.large_step = step_ * 10;
    return r;
}

bool NumberField::accessibleSetRangeValue(double v) {
    if (!std::isfinite(v) || v < min_ || v > max_ || (integral_ && v != std::floor(v))) return false;  // refused, not clamped
    setValueImpl(v, true, true);
    refreshText();
    fireIfChanged();
    return true;
}

// -- the two concrete boxes ---------------------------------------------------------------------------------------

SpinBox::SpinBox(HWND window) : NumberField(window, true) {}

void SpinBox::setSpecialValueText(std::string t) {
    special_ = std::move(t);
    refreshText();
    updateGeometry();
}

DoubleSpinBox::DoubleSpinBox(HWND window) : NumberField(window, false) {}

void DoubleSpinBox::setDecimals(int d) {
    decimals_ = std::clamp(d, 0, 30);
    refreshText();
    updateGeometry();
}

}  // namespace tcad::ui

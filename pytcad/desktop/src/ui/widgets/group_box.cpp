#include "ui/widgets/group_box.hpp"

#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using tcad::desktop::theme::T;

namespace {

TextStyle titleStyle() {
    TextStyle s;
    s.bold = true;
    s.halign = HAlign::Left;
    return s;
}

// The title starts this far in from the frame's left edge, with this much frame gap each side of it.
constexpr float kTitleInset = 8.0f;
constexpr float kTitleGap = 4.0f;

}  // namespace

GroupBox::GroupBox(std::string title) : title_(std::move(title)) { accessibleName = title_; }

void GroupBox::setTitle(std::string title) {
    title_ = std::move(title);
    accessibleName = title_;
    updateGeometry();
    update();
}

float GroupBox::titleHeight() const {
    TextEngine* te = textEngine();  // none outside a window: nothing is laid out then
    return te ? std::ceil(te->measure(title_.empty() ? std::string_view("X") : std::string_view(title_), titleStyle()).height) : 0;
}

float GroupBox::titleWidth() const {
    TextEngine* te = textEngine();
    return title_.empty() || !te ? 0 : std::ceil(te->measure(title_, titleStyle()).width);
}

Margins GroupBox::contentsMargins() const {
    const float b = Style::standard().border_width;
    return {b, titleHeight(), b, b};
}

SizeF GroupBox::sizeHint() const {
    const SizeF l = Widget::sizeHint();
    const Margins m = contentsMargins();
    return {std::max(l.width, titleWidth() + 2 * (kTitleInset + kTitleGap)), std::max(l.height, m.top + m.bottom)};
}

SizeF GroupBox::minimumSizeHint() const {
    const SizeF l = Widget::minimumSizeHint();
    const Margins m = contentsMargins();
    return {std::max(l.width, titleWidth() + 2 * (kTitleInset + kTitleGap)), std::max(l.height, m.top + m.bottom)};
}

void GroupBox::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    const float th = titleHeight();
    const float top = std::round(th / 2);
    const bool on = isEnabled();
    const auto c = p.crisp({0, top, s.width, s.height - top}, st.border_width);
    p.strokeRoundedRect(c.rect, st.corner_radius, token(T::Border), c.width);
    if (title_.empty()) return;
    // the title sits on the frame's top edge: the frame is painted over with the window colour behind it
    const float tw = titleWidth();
    p.fillRect({kTitleInset, 0, tw + 2 * kTitleGap, th}, token(T::Window));
    TextStyle ts = titleStyle();
    ts.color = token(on ? T::Text : T::TextFaint);
    p.drawText({kTitleInset + kTitleGap, 0, tw + 1, th}, title_, ts);
}

}  // namespace tcad::ui

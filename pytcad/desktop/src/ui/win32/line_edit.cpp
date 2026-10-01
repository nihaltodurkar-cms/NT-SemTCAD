#include "ui/win32/line_edit.hpp"

#include "ui/core/selection.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using tcad::desktop::theme::T;

LineEdit::LineEdit(HWND window) : TextInput(window, false) {
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Fixed});  // QLineEdit's defaults
}

LineEdit::~LineEdit() = default;

float LineEdit::textTop() const { return std::floor((sizeDips().height - lineHeight()) / 2); }

SizeF LineEdit::sizeHint() const {
    return {std::ceil(160.0f), std::ceil(lineHeight() + 2 * kPadding)};  // QLineEdit is about 17 characters wide
}

float LineEdit::caretX(std::size_t offset) const {
    TextEngine* te = textEngine();
    return te ? te->caretRect(model_.text(), style(), 0, offset).x : 0.0f;
}

std::vector<RectF> LineEdit::rangeRects(std::size_t a, std::size_t b) const {
    const float x0 = caretX(a) - scroll_ + kPadding, x1 = caretX(b) - scroll_ + kPadding;
    return {{std::min(x0, x1), textTop(), std::fabs(x1 - x0), lineHeight()}};
}

std::size_t LineEdit::offsetAt(PointF local) const {
    TextEngine* te = textEngine();
    if (!te) return 0;
    return model_.snap(te->hitTest(model_.text(), style(), 0, {local.x - kPadding + scroll_, local.y - textTop()}).offset);
}

void LineEdit::ensureCaretVisible() {
    const float inner = std::max(1.0f, sizeDips().width - 2 * kPadding);
    const float x = caretX(model_.caret());
    if (x - scroll_ > inner) scroll_ = x - inner;
    if (x - scroll_ < 0) scroll_ = x;
    TextEngine* te = textEngine();
    const float full = te ? te->measure(model_.text(), style()).width : 0;
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, full - inner));
}

void LineEdit::textSet() {
    scroll_ = 0;
    committed_ = model_.text();
}

void LineEdit::setValidator(std::shared_ptr<const Validator> v) {
    validator_ = std::move(v);
    if (validator_) {
        const Validator* raw = validator_.get();
        model_.setFilter([raw](const std::string& candidate) { return raw->validate(candidate) != Validator::State::Invalid; });
    } else {
        model_.setFilter({});
    }
}

bool LineEdit::hasAcceptableInput() const {
    return !validator_ || validator_->validate(model_.text()) == Validator::State::Acceptable;
}

void LineEdit::finish(bool enter) {
    if (!hasAcceptableInput()) return;  // Qt: an Intermediate text is not "finished"
    if (enter && on_return_pressed) on_return_pressed();
    if (on_editing_finished) on_editing_finished();
    if (model_.text() != committed_) {
        committed_ = model_.text();
        if (on_commit) on_commit();
    }
}

bool LineEdit::enterPressed() {
    finish(true);
    return true;
}

void LineEdit::focusLost() { finish(false); }  // QLineEdit: editingFinished on focus loss too

void LineEdit::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    const auto border = p.crisp({0, 0, s.width, s.height}, invalid_ ? 2.0f : 1.0f);
    p.strokeRect(border.rect, token(invalid_ ? T::Error : hasFocus() ? T::Focus : T::BorderStrong), border.width);
    p.save();
    p.clipRect({kPadding - 1, 1, std::max(0.0f, s.width - 2 * kPadding + 2), std::max(0.0f, s.height - 2)});
    const float ox = kPadding - scroll_, oy = textTop(), lh = lineHeight();
    std::vector<RectF> sel;
    if (model_.hasSelection()) {
        const auto [a, b] = model_.selection();
        TextEngine* te = textEngine();
        if (te)
            for (RectF r : te->selectionRects(model_.text(), style(), 0, a, b)) sel.push_back({ox + r.x, oy, r.width, lh});
        fillSelection(p, sel, selectionActive());
    }
    TextStyle st = style();
    if (model_.text().empty() && !placeholder_.empty() && !model_.composing()) {
        st.color = token(T::TextFaint);
        p.drawText({kPadding, oy, std::max(0.0f, s.width - 2 * kPadding), lh}, placeholder_, st);
    }
    p.drawText({ox, oy, 100000.0f, lh}, model_.text(), st);
    if (!sel.empty())
        redrawSelectedText(p, sel, selectionActive(), [&](Color c) {
            TextStyle hs = style();
            hs.color = c;
            p.drawText({ox, oy, 100000.0f, lh}, model_.text(), hs);
        });
    if (model_.composing()) {  // the IME's composition, underlined as Windows edits do
        const auto [a, b] = model_.composition();
        const float x0 = caretX(a), x1 = caretX(b);
        p.drawLine({ox + std::min(x0, x1), oy + lh - 1}, {ox + std::max(x0, x1), oy + lh - 1}, token(T::Text), 1.0f);
    }
    if (hasFocus() && caretVisible() && !model_.readOnly()) {
        const float x = std::round((ox + caretX(model_.caret())) * static_cast<float>(p.scale())) / static_cast<float>(p.scale());
        p.fillRect({x, oy, 1.0f / static_cast<float>(p.scale()), lh}, token(T::Text));
    }
    p.restore();
}

void LineEdit::setInvalid(bool on) {
    if (invalid_ == on) return;
    invalid_ = on;
    update();
}

}  // namespace tcad::ui

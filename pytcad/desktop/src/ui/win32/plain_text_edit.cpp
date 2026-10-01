#include "ui/win32/plain_text_edit.hpp"

#include "ui/core/keys.hpp"
#include "ui/core/selection.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using tcad::desktop::theme::T;

PlainTextEdit::PlainTextEdit(HWND window) : TextInput(window, true) {
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Expanding});
    vbar_ = addChild<ScrollBar>(Orientation::Vertical);
    hbar_ = addChild<ScrollBar>(Orientation::Horizontal);
    vbar_->setVisible(false);
    hbar_->setVisible(false);
    vbar_->name = "vertical_scroll_bar";
    hbar_->name = "horizontal_scroll_bar";
    vbar_->accessibleName = "Vertical scroll bar";
    hbar_->accessibleName = "Horizontal scroll bar";
    vbar_->on_value_changed = [this](int) { update(); };
    hbar_->on_value_changed = [this](int) { update(); };
}

PlainTextEdit::~PlainTextEdit() { stopDragScroll(); }

// -- style and layout -------------------------------------------------------------------------------------------------

TextStyle PlainTextEdit::style() const {
    TextStyle s = TextInput::style();
    s.family = mono_ ? FontFamily::Monospace : FontFamily::Ui;
    return s;
}

TextStyle PlainTextEdit::lineStyle(int i) const {
    TextStyle s = style();
    const LineFormat& f = lineFormat(i);
    s.color = token(isEnabled() ? f.color : T::TextFaint);
    s.bold = f.bold;
    s.italic = f.italic;
    s.wrap = wrap_;
    s.valign = VAlign::Top;
    return s;
}

const LineFormat& PlainTextEdit::lineFormat(int i) const {
    static const LineFormat kDefault;
    return i >= 0 && static_cast<std::size_t>(i) < formats_.size() ? formats_[static_cast<std::size_t>(i)] : kDefault;
}

float PlainTextEdit::wrapWidth() const { return wrap_ && view_w_ > 0 ? std::max(1.0f, view_w_ - 2 * kPadding) : 0.0f; }

void PlainTextEdit::layout() const {
    TextEngine* te = textEngine();
    if (!te) return;
    const float w = wrapWidth();
    if (!dirty_ && w == laid_out_at_) return;
    lines_.clear();
    const std::string& text = model_.text();
    float y = 0, widest = 0;
    std::size_t pos = 0;
    for (int i = 0;; ++i) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        const std::string_view ln = std::string_view(text).substr(pos, end - pos);
        const TextStyle st = lineStyle(i);
        const SizeF m = ln.empty() ? te->measure("", st) : (w > 0 ? te->measureWrapped(ln, st, w) : te->measure(ln, st));
        lines_.push_back({pos, end, y, m.height, m.width});
        y += m.height;
        widest = std::max(widest, m.width);
        if (end >= text.size()) break;
        pos = end + 1;
    }
    content_h_ = y;
    content_w_ = widest;
    laid_out_at_ = w;
    dirty_ = false;
}

int PlainTextEdit::lineOf(std::size_t offset) const {
    layout();
    if (lines_.empty()) return 0;
    const auto it = std::upper_bound(lines_.begin(), lines_.end(), offset, [](std::size_t o, const Line& l) { return o < l.start; });
    return it == lines_.begin() ? 0 : static_cast<int>(it - lines_.begin()) - 1;
}

std::string_view PlainTextEdit::lineView(int i) const {
    layout();
    if (i < 0 || static_cast<std::size_t>(i) >= lines_.size()) return {};
    const Line& l = lines_[static_cast<std::size_t>(i)];
    return std::string_view(model_.text()).substr(l.start, l.end - l.start);
}

std::string PlainTextEdit::lineText(int i) const { return std::string(lineView(i)); }

float PlainTextEdit::contentHeight() const {
    layout();
    return content_h_;
}

RectF PlainTextEdit::viewportRect() const { return {1, 1, view_w_, view_h_}; }

int PlainTextEdit::firstVisibleLine() const {
    layout();
    const float sy = static_cast<float>(vbar_->value()) - kPadV;
    const auto it = std::lower_bound(lines_.begin(), lines_.end(), sy, [](const Line& l, float y) { return l.top + l.height <= y; });
    return it == lines_.end() ? static_cast<int>(lines_.size()) - 1 : static_cast<int>(it - lines_.begin());
}

SizeF PlainTextEdit::sizeHint() const { return {256, 192}; }                        // QAbstractScrollArea's
SizeF PlainTextEdit::minimumSizeHint() const { return {70, 3 * lineHeight() + 2 * kPadV + 2}; }

// -- scroll bars --------------------------------------------------------------------------------------------------------

void PlainTextEdit::updateBars() {
    if (in_update_bars_) return;
    TextEngine* te = textEngine();
    const RectI g = geometry();
    if (!te || g.empty()) return;
    in_update_bars_ = true;
    const double s = scale();
    const int bpx = std::max(1, roundPx(1.0f, s)), tpx = roundPx(ScrollBar::kThickness, s);
    bool vneed = false, hneed = false;
    for (int pass = 0; pass < 3; ++pass) {  // a bar takes room, which can make the other needed: monotone, so it settles
        view_w_ = static_cast<float>((g.width - 2 * bpx - (vneed ? tpx : 0)) / s);
        view_h_ = static_cast<float>((g.height - 2 * bpx - (hneed ? tpx : 0)) / s);
        dirty_ = true;
        layout();
        const bool nv = vneed || content_h_ + 2 * kPadV > view_h_ + 0.01f;
        const bool nh = hneed || (!wrap_ && content_w_ + 2 * kPadding > view_w_ + 0.01f);
        if (nv == vneed && nh == hneed) break;
        vneed = nv;
        hneed = nh;
    }
    const int vmax = vneed ? static_cast<int>(std::ceil(content_h_ + 2 * kPadV - view_h_)) : 0;
    const int hmax = hneed ? static_cast<int>(std::ceil(content_w_ + 2 * kPadding - view_w_)) : 0;
    vbar_->setSingleStep(std::max(1, static_cast<int>(std::lround(lineHeight()))));
    vbar_->setPageStep(std::max(1, static_cast<int>(view_h_)));
    vbar_->setRange(0, vmax);
    hbar_->setSingleStep(20);
    hbar_->setPageStep(std::max(1, static_cast<int>(view_w_)));
    hbar_->setRange(0, hmax);
    vbar_->setVisible(vneed);
    hbar_->setVisible(hneed);
    vbar_->setGeometry({g.width - bpx - tpx, bpx, tpx, std::max(0, g.height - 2 * bpx - (hneed ? tpx : 0))});
    hbar_->setGeometry({bpx, g.height - bpx - tpx, std::max(0, g.width - 2 * bpx - (vneed ? tpx : 0)), tpx});
    in_update_bars_ = false;
}

void PlainTextEdit::resized() {
    updateBars();
    ensureCaretVisible();
}

bool PlainTextEdit::atEnd() const { return vbar_->value() >= vbar_->maximum() - 2; }

void PlainTextEdit::setScroll(float x, float y) {
    hbar_->setValue(static_cast<int>(std::lround(x)));
    vbar_->setValue(static_cast<int>(std::lround(y)));
}

void PlainTextEdit::scrollToEnd() { vbar_->setValue(vbar_->maximum()); }
void PlainTextEdit::scrollToStart() { setScroll(0, 0); }

// -- geometry of the text -----------------------------------------------------------------------------------------------

RectF PlainTextEdit::caretRectDoc(std::size_t offset) const {
    layout();
    TextEngine* te = textEngine();
    const int i = lineOf(offset);
    if (!te || lines_.empty()) return {0, 0, 0, 16};
    const Line& l = lines_[static_cast<std::size_t>(i)];
    const TextStyle st = lineStyle(i);
    const RectF r = te->caretRect(lineView(i), st, wrapWidth(), std::min(offset, l.end) - l.start);
    return {r.x, l.top + r.y, 0, r.height > 0 ? r.height : te->measure("", st).height};
}

std::size_t PlainTextEdit::offsetAtDoc(PointF doc) const {
    layout();
    TextEngine* te = textEngine();
    if (!te || lines_.empty()) return 0;
    const auto it = std::upper_bound(lines_.begin(), lines_.end(), doc.y, [](float y, const Line& l) { return y < l.top; });
    const int i = it == lines_.begin() ? 0 : static_cast<int>(it - lines_.begin()) - 1;
    const Line& l = lines_[static_cast<std::size_t>(i)];
    if (l.end == l.start) return l.start;
    const TextHit h = te->hitTest(lineView(i), lineStyle(i), wrapWidth(), {doc.x, doc.y - l.top});
    return model_.snap(l.start + std::min(h.offset, l.end - l.start));
}

std::size_t PlainTextEdit::offsetAt(PointF local) const {
    return offsetAtDoc({local.x - kPadding + static_cast<float>(hbar_->value()), local.y - kPadV + static_cast<float>(vbar_->value())});
}

std::vector<RectF> PlainTextEdit::selectionRectsDoc(std::size_t a, std::size_t b) const {
    std::vector<RectF> out;
    TextEngine* te = textEngine();
    if (!te || a >= b) return out;
    layout();
    for (std::size_t i = static_cast<std::size_t>(lineOf(a)); i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        if (l.start > b) break;
        const std::size_t lo = std::max(a, l.start), hi = std::min(b, l.end);
        const TextStyle st = lineStyle(static_cast<int>(i));
        if (lo < hi) {
            for (RectF r : te->selectionRects(lineView(static_cast<int>(i)), st, wrapWidth(), lo - l.start, hi - l.start)) {
                r.y += l.top;
                out.push_back(r);
            }
        }
        if (a <= l.end && b > l.end && l.end < model_.text().size()) {  // the line feed is selected: a space-wide mark, as Qt's
            const RectF c = caretRectDoc(l.end);
            out.push_back({c.x, c.y, te->measure(" ", st).width, c.height});
        }
    }
    return out;
}

std::vector<RectF> PlainTextEdit::rangeRects(std::size_t a, std::size_t b) const {
    const float ox = kPadding - static_cast<float>(hbar_->value()), oy = kPadV - static_cast<float>(vbar_->value());
    if (a > b) std::swap(a, b);
    if (a == b) {
        const RectF c = caretRectDoc(a);
        return {{c.x + ox, c.y + oy, 0, c.height}};
    }
    std::vector<RectF> r = selectionRectsDoc(a, b);
    for (RectF& x : r) x.x += ox, x.y += oy;
    if (r.empty()) {
        const RectF c = caretRectDoc(a);
        r.push_back({c.x + ox, c.y + oy, 0, c.height});
    }
    return r;
}

RectF PlainTextEdit::caretRectInWidget() const {
    const RectF c = caretRectDoc(model_.caret());
    return {c.x + kPadding - static_cast<float>(hbar_->value()), c.y + kPadV - static_cast<float>(vbar_->value()), 0, c.height};
}

void PlainTextEdit::ensureCaretVisible() {
    if (view_h_ <= 0) return;
    const RectF c = caretRectDoc(model_.caret());
    float sy = static_cast<float>(vbar_->value()), sx = static_cast<float>(hbar_->value());
    const float inner_h = view_h_ - 2 * kPadV, inner_w = view_w_ - 2 * kPadding;
    if (c.y < sy) sy = c.y;
    else if (c.y + c.height > sy + inner_h) sy = c.y + c.height - inner_h;
    if (!wrap_) {
        if (c.x < sx) sx = c.x;
        else if (c.x > sx + inner_w) sx = c.x - inner_w;
    }
    setScroll(sx, sy);
}

// -- content ------------------------------------------------------------------------------------------------------------

void PlainTextEdit::contentChanged() {
    dirty_ = true;
    line_count_ = 1 + static_cast<int>(std::count(model_.text().begin(), model_.text().end(), '\n'));
    if (formats_.size() != static_cast<std::size_t>(line_count_)) formats_.assign(static_cast<std::size_t>(line_count_), LineFormat{});
    goal_valid_ = false;
    updateBars();
    update();
}

void PlainTextEdit::textSet() {
    const int n = 1 + static_cast<int>(std::count(model_.text().begin(), model_.text().end(), '\n'));
    formats_.assign(static_cast<std::size_t>(n), LineFormat{});
    line_count_ = n;
    dropped_ = 0;
    pristine_ = model_.text().empty();
    model_.setSelection(0, 0);
    trimToMaximum();
    setScroll(0, 0);
}

void PlainTextEdit::trimToMaximum() {
    if (max_lines_ <= 0) return;
    const int n = 1 + static_cast<int>(std::count(model_.text().begin(), model_.text().end(), '\n'));
    if (n <= max_lines_) return;
    const int drop = n - max_lines_;
    std::size_t cut = 0;
    for (int i = 0; i < drop; ++i) cut = model_.text().find('\n', cut) + 1;
    model_.forceReplace(0, cut, "");
    formats_.erase(formats_.begin(), formats_.begin() + std::min<std::ptrdiff_t>(drop, static_cast<std::ptrdiff_t>(formats_.size())));
    dropped_ += drop;
}

void PlainTextEdit::appendLine(std::string_view line, LineFormat format) {
    const bool was_at_end = follow_ && atEnd();
    const std::size_t old = u16Length();
    std::string add;
    if (pristine_) {
        formats_.assign(1, format);
        add.assign(line);
    } else {
        formats_.push_back(format);
        add = "\n";
        add.append(line);
    }
    pristine_ = false;
    model_.forceReplace(model_.text().size(), model_.text().size(), add);
    trimToMaximum();
    changedByProgram(old);
    if (was_at_end) scrollToEnd();
}

void PlainTextEdit::clear() { setText({}); }

void PlainTextEdit::setMaximumLineCount(int n) {
    max_lines_ = std::max(0, n);
    const std::size_t old = u16Length();
    const std::int64_t before = dropped_;
    trimToMaximum();
    if (dropped_ != before) changedByProgram(old);
}

void PlainTextEdit::setWordWrap(bool on) {
    if (on == wrap_) return;
    wrap_ = on;
    dirty_ = true;
    updateBars();
    ensureCaretVisible();
    update();
}

void PlainTextEdit::setMonospace(bool on) {
    if (on == mono_) return;
    mono_ = on;
    dirty_ = true;
    updateBars();
    update();
}

// -- keys and mouse -----------------------------------------------------------------------------------------------------

bool PlainTextEdit::enterPressed() {
    if (model_.readOnly()) return false;
    const std::size_t old = u16Length();
    if (model_.insert("\n")) changed(old);
    return true;
}

void PlainTextEdit::moveVertical(bool down, bool page, bool extend) {
    const RectF c = caretRectDoc(model_.caret());
    if (!goal_valid_) goal_x_ = c.x, goal_valid_ = true;
    layout();
    const float ty = page ? c.y + (down ? view_h_ : -view_h_) : (down ? c.y + c.height + 0.5f : c.y - 0.5f);
    std::size_t off;
    if (ty < 0) off = 0;  // Qt: Up on the first line goes to the start
    else if (ty >= content_h_) off = model_.text().size();
    else off = offsetAtDoc({goal_x_, ty});
    if (page) setScroll(static_cast<float>(hbar_->value()), static_cast<float>(vbar_->value()) + (down ? view_h_ : -view_h_));
    model_.setSelection(extend ? model_.anchor() : off, off);
    selectionMoved();
}

void PlainTextEdit::visualHomeEnd(bool end, bool extend) {
    const RectF c = caretRectDoc(model_.caret());
    std::size_t off = offsetAtDoc({end ? 1.0e6f : -1.0e6f, c.y + c.height / 2});
    if (end && off < model_.lineEnd(off) && off > 0) off = model_.prevStop(off);  // before a soft break, not after it
    model_.setSelection(extend ? model_.anchor() : off, off);
    selectionMoved();
}

bool PlainTextEdit::beforeKey(const KeyEvent& e) {
    const bool ctrl = any(e.mods & Mod::Ctrl), shift = any(e.mods & Mod::Shift);
    switch (e.vk) {
        case keys::Up:
        case keys::Down:
            if (ctrl) break;
            moveVertical(e.vk == keys::Down, false, shift);
            return true;
        case keys::PageUp:
        case keys::PageDown:
            if (ctrl) break;
            moveVertical(e.vk == keys::PageDown, true, shift);
            return true;
        case keys::Home:
        case keys::End:
            goal_valid_ = false;
            if (ctrl) {
                const std::size_t off = e.vk == keys::Home ? 0 : model_.text().size();
                model_.setSelection(shift ? model_.anchor() : off, off);
                selectionMoved();
            } else {
                visualHomeEnd(e.vk == keys::End, shift);
            }
            return true;
        default: break;
    }
    goal_valid_ = false;
    return false;
}

void PlainTextEdit::stopDragScroll() {
    if (drag_timer_) stopTimer(drag_timer_);
    drag_timer_ = 0;
}

bool PlainTextEdit::dragOutside(const UiMouseEvent& e) {
    drag_pos_ = e.pos;
    const bool outside = e.pos.y < 1 || e.pos.y > view_h_ || (!wrap_ && (e.pos.x < 1 || e.pos.x > view_w_));
    if (!outside) {
        stopDragScroll();
        return false;
    }
    if (!drag_timer_)  // keep scrolling while the pointer rests outside the view
        drag_timer_ = startTimer(kDragScrollMs, true, [this] {
            const float lh = std::max(1.0f, lineHeight());
            const float dy = drag_pos_.y < 1 ? -lh : drag_pos_.y > view_h_ ? lh : 0;
            const float dx = !wrap_ ? (drag_pos_.x < 1 ? -20.0f : drag_pos_.x > view_w_ ? 20.0f : 0) : 0;
            setScroll(static_cast<float>(hbar_->value()) + dx, static_cast<float>(vbar_->value()) + dy);
            model_.setSelection(model_.anchor(), offsetAt(drag_pos_));
            update();
        });
    return true;
}

bool PlainTextEdit::mouseEvent(const UiMouseEvent& e) {
    using platform::MouseButton;
    using platform::MouseType;
    if (e.type == MouseType::Wheel) {
        if (!vbar_->isNeeded() || e.wheel_steps == 0) return false;
        int d = static_cast<int>(std::lround(e.wheel_steps * 3 * vbar_->singleStep()));
        if (d == 0) d = e.wheel_steps > 0 ? 1 : -1;
        vbar_->setValue(vbar_->value() - d);  // away from the user = up
        return true;
    }
    if (e.type == MouseType::Down) goal_valid_ = false;
    if (e.type == MouseType::Up && e.button == MouseButton::Left) stopDragScroll();
    return TextInput::mouseEvent(e);
}

// -- accessibility ----------------------------------------------------------------------------------------------------

AccessibleScroll PlainTextEdit::accessibleScroll() const {
    AccessibleScroll a;
    a.valid = true;
    a.vertical = vbar_->isNeeded();
    a.horizontal = hbar_->isNeeded();
    a.v_percent = a.vertical ? 100.0 * vbar_->value() / vbar_->maximum() : 0;
    a.h_percent = a.horizontal ? 100.0 * hbar_->value() / hbar_->maximum() : 0;
    layout();
    const float ch = content_h_ + 2 * kPadV, cw = content_w_ + 2 * kPadding;
    a.v_view = ch > 0 ? std::min(100.0, 100.0 * view_h_ / ch) : 100;
    a.h_view = wrap_ || cw <= 0 ? 100 : std::min(100.0, 100.0 * view_w_ / cw);
    return a;
}

void PlainTextEdit::accessibleScrollBy(int h, int v) {
    auto by = [](ScrollBar* b, int amount) {
        if (amount == 0 || !b->isNeeded()) return;
        const int d = (amount == 1 || amount == -1) ? b->singleStep() : b->pageStep();
        b->setValue(b->value() + (amount > 0 ? d : -d));
    };
    by(hbar_, h);
    by(vbar_, v);
}

void PlainTextEdit::accessibleSetScrollPercent(double h, double v) {
    if (h >= 0 && hbar_->isNeeded()) hbar_->setValue(static_cast<int>(std::lround(std::clamp(h, 0.0, 100.0) / 100.0 * hbar_->maximum())));
    if (v >= 0 && vbar_->isNeeded()) vbar_->setValue(static_cast<int>(std::lround(std::clamp(v, 0.0, 100.0) / 100.0 * vbar_->maximum())));
}

// -- painting ---------------------------------------------------------------------------------------------------------

void PlainTextEdit::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    const auto border = p.crisp({0, 0, s.width, s.height}, 1.0f);
    p.strokeRect(border.rect, token(hasFocus() ? T::Focus : T::BorderStrong), border.width);
    if (vbar_->isVisibleSelf() && hbar_->isVisibleSelf()) {  // the corner where the two bars meet
        const RectI g = geometry();
        const double k = scale();
        p.fillRect({static_cast<float>(vbar_->geometry().x / k), static_cast<float>(hbar_->geometry().y / k),
                    static_cast<float>(vbar_->geometry().width / k), static_cast<float>(hbar_->geometry().height / k)},
                   token(T::AlternateBase));
        (void)g;
    }
    layout();
    TextEngine* te = textEngine();
    p.save();
    p.clipRect({1, 1, std::max(0.0f, view_w_), std::max(0.0f, view_h_)});
    const float ox = kPadding - static_cast<float>(hbar_->value()), oy = kPadV - static_cast<float>(vbar_->value());
    const float ww = wrap_ ? wrapWidth() : 100000.0f;
    const int first = firstVisibleLine();
    auto visible = [&](const Line& l) { return l.top + l.height + oy > 0 && l.top + oy < view_h_ + 1; };
    auto drawLines = [&](const Color* override_color) {
        for (std::size_t i = static_cast<std::size_t>(std::max(first, 0)); i < lines_.size(); ++i) {
            const Line& l = lines_[i];
            if (l.top + oy > view_h_ + 1) break;
            if (!visible(l) || l.end == l.start) continue;
            TextStyle st = lineStyle(static_cast<int>(i));
            if (override_color) st.color = *override_color;
            p.drawText({ox, oy + l.top, ww, l.height}, lineView(static_cast<int>(i)), st);
        }
    };
    std::vector<RectF> sel;
    if (te && model_.hasSelection()) {
        const auto [a, b] = model_.selection();
        for (RectF r : selectionRectsDoc(a, b)) sel.push_back({r.x + ox, r.y + oy, r.width, r.height});
        fillSelection(p, sel, selectionActive());
    }
    if (model_.text().empty() && !placeholder_.empty() && !model_.composing() && te) {
        TextStyle st = style();
        st.color = token(T::TextFaint);
        st.wrap = true;
        const float w = std::max(1.0f, view_w_ - 2 * kPadding);
        p.drawText({kPadding, kPadV, w, te->measureWrapped(placeholder_, st, w).height}, placeholder_, st);
    }
    drawLines(nullptr);
    if (!sel.empty()) redrawSelectedText(p, sel, selectionActive(), [&](Color c) { drawLines(&c); });
    if (model_.composing() && te) {  // the IME's composition, underlined as Windows edits do
        const auto [a, b] = model_.composition();
        for (const RectF& r : selectionRectsDoc(a, b)) p.drawLine({r.x + ox, r.bottom() + oy - 1}, {r.right() + ox, r.bottom() + oy - 1}, token(T::Text), 1.0f);
    }
    if (te && hasFocus() && caretVisible() && !model_.readOnly()) {
        const RectF c = caretRectDoc(model_.caret());
        const float k = static_cast<float>(p.scale());
        p.fillRect({std::round((c.x + ox) * k) / k, c.y + oy, 1.0f / k, c.height}, token(T::Text));
    }
    p.restore();
}

}  // namespace tcad::ui

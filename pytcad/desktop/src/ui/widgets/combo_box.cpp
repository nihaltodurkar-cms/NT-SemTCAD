#include "ui/widgets/combo_box.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace tcad::ui {

using platform::Mod;
using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

namespace {

constexpr int kF4 = 0x73;

float lineHeightOf(const Widget& w) {
    TextStyle ts;
    ts.size = Style::standard().font_size;
    if (TextEngine* te = w.textEngine()) return te->measure("Ag", ts).height;
    return 15.0f;
}

float textWidthOf(const Widget& w, const std::string& s) {
    TextStyle ts;
    ts.size = Style::standard().font_size;
    if (TextEngine* te = w.textEngine()) return te->measure(s, ts).width;
    return 6.0f * static_cast<float>(s.size());
}

std::string utf8(char32_t c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
    return s;
}

// ASCII letters fold; everything else compares as bytes.
bool startsWithFold(const std::string& text, const std::string& prefix) {
    if (prefix.size() > text.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto a = static_cast<unsigned char>(text[i]), b = static_cast<unsigned char>(prefix[i]);
        if (a < 0x80 && b < 0x80 ? std::tolower(a) != std::tolower(b) : a != b) return false;
    }
    return true;
}

}  // namespace

// -- the list ---------------------------------------------------------------------------------------------------

ComboPopupList::ComboPopupList(std::vector<std::string> items, int current) : items_(std::move(items)) {
    highlight_ = current >= 0 && current < static_cast<int>(items_.size()) ? current : (items_.empty() ? -1 : 0);
    if (current < 0) highlight_ = -1;
}

float ComboPopupList::rowHeight() const { return std::ceil(lineHeightOf(*this)) + 6.0f; }

int ComboPopupList::visibleRows() const {
    const float inner = sizeDips().height - 2.0f;
    return std::max(1, static_cast<int>(std::floor(inner / rowHeight())));
}

void ComboPopupList::clampScroll() {
    const int n = static_cast<int>(items_.size());
    first_ = std::clamp(first_, 0, std::max(0, n - visibleRows()));
}

int ComboPopupList::rowAt(float y) const {
    const float inner = y - 1.0f;
    if (inner < 0) return -1;
    const int row = first_ + static_cast<int>(std::floor(inner / rowHeight()));
    return row >= 0 && row < static_cast<int>(items_.size()) && row < first_ + visibleRows() + 1 ? row : -1;
}

void ComboPopupList::setHighlighted(int row) {
    const int n = static_cast<int>(items_.size());
    row = std::clamp(row, -1, n - 1);
    if (row >= 0) {
        if (row < first_) first_ = row;
        else if (row >= first_ + visibleRows()) first_ = row - visibleRows() + 1;
        clampScroll();
    }
    if (row != highlight_) {
        highlight_ = row;
        if (on_highlighted) on_highlighted(row);
    }
    update();
}

bool ComboPopupList::moveHighlight(int step) {
    const int n = static_cast<int>(items_.size());
    if (n == 0) return false;
    const int from = highlight_ < 0 ? (step > 0 ? -1 : n) : highlight_;
    const int to = std::clamp(from + step, 0, n - 1);
    if (to == highlight_) return false;
    setHighlighted(to);
    return true;
}

SizeF ComboPopupList::sizeHint() const {
    float w = 0;
    for (const auto& s : items_) w = std::max(w, textWidthOf(*this, s));
    const int rows = std::clamp(static_cast<int>(items_.size()), 1, kMaxVisibleRows);
    return {std::ceil(w) + 2 * kPadding + 2.0f, static_cast<float>(rows) * rowHeight() + 2.0f};
}

void ComboPopupList::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    p.save();
    p.clipRect({1, 1, std::max(0.0f, s.width - 2), std::max(0.0f, s.height - 2)});
    const float rh = rowHeight();
    const int n = static_cast<int>(items_.size());
    for (int i = first_; i < n; ++i) {
        const float y = 1.0f + static_cast<float>(i - first_) * rh;
        if (y >= s.height) break;
        const bool hot = i == highlight_;
        if (hot) p.fillRect({1, y, s.width - 2, rh}, token(T::Accent));
        TextStyle ts;
        ts.color = token(hot ? T::OnAccent : T::Text);
        p.drawText({1 + kPadding, y, std::max(0.0f, s.width - 2 - 2 * kPadding), rh}, items_[static_cast<std::size_t>(i)], ts);
    }
    p.restore();
    if (n > visibleRows()) {  // a thin indicator, not a control (the dragging scroll bar arrives with N3e's lists)
        const float track = s.height - 4.0f;
        const float thumb = std::max(12.0f, track * static_cast<float>(visibleRows()) / static_cast<float>(n));
        const float top = 2.0f + (track - thumb) * static_cast<float>(first_) / static_cast<float>(n - visibleRows());
        p.fillRoundedRect({s.width - 6.0f, top, 3.0f, thumb}, 1.5f, token(T::BorderStrong));
    }
    const auto c = p.crisp({0, 0, s.width, s.height}, st.border_width);
    p.strokeRect(c.rect, token(T::BorderStrong), c.width);
}

bool ComboPopupList::mouseEvent(const UiMouseEvent& e) {
    switch (e.type) {
        case MouseType::Move: {
            const int row = rowAt(e.pos.y);
            if (row >= 0 && row != highlight_) setHighlighted(row);
            return true;
        }
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (e.button != MouseButton::Left) return false;
            pressed_ = rowAt(e.pos.y);
            if (pressed_ >= 0 && pressed_ != highlight_) setHighlighted(pressed_);
            return true;
        case MouseType::Up: {
            if (e.button != MouseButton::Left) return false;
            const int row = rowAt(e.pos.y);
            const bool pick = pressed_ >= 0 && row == pressed_;
            pressed_ = -1;
            if (pick && on_chosen) on_chosen(row);
            return true;
        }
        case MouseType::Wheel: {
            const int n = static_cast<int>(items_.size());
            if (n <= visibleRows() || e.wheel_steps == 0) return true;
            const int notches = std::max(1, static_cast<int>(std::lround(std::fabs(e.wheel_steps))));
            first_ += e.wheel_steps > 0 ? -3 * notches : 3 * notches;
            clampScroll();
            update();
            return true;
        }
        default: return false;
    }
}

void ComboPopupList::hoverChanged(bool) { update(); }

// -- the combo box ----------------------------------------------------------------------------------------------

ComboBox::ComboBox() {
    setFocusPolicy(FocusPolicy::Strong);
    setSizePolicy({SizePolicy::Minimum, SizePolicy::Fixed});  // QComboBox's default
}

ComboBox::~ComboBox() { hidePopup(); }

const std::string& ComboBox::itemText(int i) const {
    static const std::string none;
    return i >= 0 && i < count() ? items_[static_cast<std::size_t>(i)].text : none;
}

const ComboData& ComboBox::itemData(int i) const {
    static const ComboData none;
    return i >= 0 && i < count() ? items_[static_cast<std::size_t>(i)].data : none;
}

void ComboBox::setCurrent(int i, bool notify) {
    if (i == current_) return;
    current_ = i;
    update();
    notifyRangeChanged();  // the UIA value changed
    if (!notify) return;
    if (on_current_index_changed) on_current_index_changed(i);
    if (on_current_text_changed) on_current_text_changed(currentText());
}

void ComboBox::addItem(std::string text, ComboData data) {
    hidePopup();
    items_.push_back({std::move(text), std::move(data)});
    updateGeometry();
    if (current_ < 0 && count() == 1) setCurrent(0, true);  // Qt: the first item becomes current
}

void ComboBox::addItems(const std::vector<std::string>& texts) {
    for (const auto& t : texts) addItem(t);
}

void ComboBox::clear() {
    hidePopup();
    items_.clear();
    updateGeometry();
    setCurrent(-1, true);
}

void ComboBox::setCurrentIndex(int i) {
    if (i < -1 || i >= count()) return;
    setCurrent(i, true);
}

void ComboBox::setCurrentIndexSilent(int i) {
    if (i < -1 || i >= count()) return;
    setCurrent(i, false);
}

void ComboBox::setCurrentText(const std::string& text) {
    const int i = findText(text);
    if (i >= 0) setCurrent(i, true);
}

int ComboBox::findText(const std::string& text) const {
    for (int i = 0; i < count(); ++i)
        if (items_[static_cast<std::size_t>(i)].text == text) return i;
    return -1;
}

int ComboBox::findData(const ComboData& data) const {
    for (int i = 0; i < count(); ++i)
        if (items_[static_cast<std::size_t>(i)].data == data) return i;
    return -1;
}

SizeF ComboBox::sizeHint() const {
    float w = 0;
    for (const auto& it : items_) w = std::max(w, textWidthOf(*this, it.text));
    w = std::max(w, textWidthOf(*this, "000000"));  // an empty combo is not a sliver
    return {std::ceil(w) + 2 * kPadding + kArrowWidth, std::ceil(lineHeightOf(*this) + 8.0f)};  // a line edit's height
}

void ComboBox::paint(Painter& p) {
    const Style& st = Style::standard();
    const SizeF s = sizeDips();
    const bool on = isEnabled(), focus = hasFocus() && on, open = popup_ != nullptr;
    const RectF all{0, 0, s.width, s.height};
    p.fillRect(all, token(on ? T::Base : T::AlternateBase));
    const auto c = p.crisp(all, focus || open ? st.focus_width : st.border_width);
    p.strokeRect(c.rect, token(!on ? T::Border : focus || open ? T::Focus : isHovered() ? T::Accent : T::BorderStrong), c.width);
    TextStyle ts;
    ts.color = token(on ? T::Text : T::TextFaint);
    p.save();
    p.clipRect({1, 1, std::max(0.0f, s.width - 2), std::max(0.0f, s.height - 2)});
    p.drawText({kPadding, 0, std::max(0.0f, s.width - kArrowWidth - kPadding), s.height}, currentText(), ts);
    p.restore();
    const float cx = s.width - kArrowWidth / 2, cy = s.height / 2;
    p.fillPolygon({{cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 2.5f}}, token(on ? T::Text : T::TextFaint));
}

// -- the drop-down ------------------------------------------------------------------------------------------------

void ComboBox::showPopup() {
    if (popup_ || count() == 0 || !isEnabled() || !host()) return;
    PopupService* svc = host()->popups();
    if (!svc) return;
    std::vector<std::string> texts;
    for (const auto& it : items_) texts.push_back(it.text);
    auto list = std::make_unique<ComboPopupList>(std::move(texts), current_);
    ComboPopupList* raw = list.get();
    raw->on_chosen = [this](int row) { choose(row); };
    raw->on_highlighted = [this](int) { highlightChanged(); };
    PopupHandle* h = svc->show(this, std::move(list));
    if (!h) return;
    popup_ = h;
    list_ = raw;
    h->on_dismissed = [this] {  // the window closed it: deactivated, moved, resized
        popup_ = nullptr;
        list_ = nullptr;
        if (InputRouter* in = host() ? host()->input() : nullptr) in->clearPressFilter(this);
        notifyExpandChanged();
        update();
    };
    if (InputRouter* in = host()->input())
        in->setPressFilter(this, [this](Widget* hit) {  // a press in the owner window, outside the popup
            if (!popup_) return false;
            hidePopup();
            return hit == this;  // on the combo itself it only closes
        });
    notifyExpandChanged();
    update();
}

void ComboBox::hidePopup() {
    if (!popup_) return;
    PopupHandle* h = popup_;
    popup_ = nullptr;
    list_ = nullptr;
    if (UiHost* uh = host())
        if (InputRouter* in = uh->input()) in->clearPressFilter(this);
    h->close();
    notifyExpandChanged();
    update();
}

void ComboBox::highlightChanged() {
    if (!list_) return;
    const int row = list_->highlighted();
    if (row >= 0 && row < static_cast<int>(list_->items().size())) announce(list_->items()[static_cast<std::size_t>(row)]);
}

void ComboBox::choose(int row) {
    hidePopup();
    if (row < 0 || row >= count()) return;
    setCurrent(row, true);
    if (on_activated) on_activated(row);
}

void ComboBox::userStep(int to) {
    if (count() == 0) return;
    to = std::clamp(to, 0, count() - 1);
    if (to == current_) return;
    setCurrent(to, true);
    if (on_activated) on_activated(to);
}

// -- input ------------------------------------------------------------------------------------------------------

bool ComboBox::mouseEvent(const UiMouseEvent& e) {
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (e.button != MouseButton::Left || !isEnabled()) return false;
            showPopup();  // (an open one was closed by the press filter, which also swallowed this press)
            return true;
        case MouseType::Wheel: {
            if (!hasFocus() || !isEnabled() || e.wheel_steps == 0 || popup_) return false;
            const int notches = std::max(1, static_cast<int>(std::lround(std::fabs(e.wheel_steps))));
            userStep(current_ + (e.wheel_steps > 0 ? -notches : notches));  // wheel up: the item above
            return true;
        }
        default: return false;
    }
}

bool ComboBox::overridesShortcut(const platform::KeyEvent& e) const {
    if (!popup_ || any(e.mods & Mod::Ctrl)) return false;
    switch (e.vk) {
        case keys::Escape: case keys::Return: case keys::Up: case keys::Down: case keys::Home: case keys::End:
        case keys::PageUp: case keys::PageDown: case keys::Space: case kF4: return true;
        default: return false;
    }
}

bool ComboBox::keyEvent(const platform::KeyEvent& e) {
    if (!e.down || !isEnabled() || any(e.mods & Mod::Ctrl)) return false;
    const bool alt = any(e.mods & Mod::Alt);
    if (popup_ && list_) {
        switch (e.vk) {
            case keys::Up: if (alt) { hidePopup(); return true; } list_->moveHighlight(-1); return true;
            case keys::Down: if (alt) { hidePopup(); return true; } list_->moveHighlight(1); return true;
            case keys::Home: list_->setHighlighted(0); return true;
            case keys::End: list_->setHighlighted(count() - 1); return true;
            case keys::PageUp: list_->moveHighlight(-ComboPopupList::kMaxVisibleRows); return true;
            case keys::PageDown: list_->moveHighlight(ComboPopupList::kMaxVisibleRows); return true;
            case keys::Return:
            case keys::Tab: choose(list_->highlighted()); return true;
            case keys::Space:
                if (!typed_.empty()) return false;  // part of a typed word (charEvent gets it)
                choose(list_->highlighted());
                return true;
            case keys::Escape:
            case kF4: hidePopup(); return true;
            default: return false;
        }
    }
    if (alt) {
        if (e.vk == keys::Down) {
            showPopup();
            return true;
        }
        return false;
    }
    switch (e.vk) {
        case keys::Up: case keys::Left: userStep(current_ - 1); return true;
        case keys::Down: case keys::Right: userStep(current_ + 1); return true;
        case keys::Home: userStep(0); return true;
        case keys::End: userStep(count() - 1); return true;
        case keys::PageUp: userStep(current_ - 10); return true;
        case keys::PageDown: userStep(current_ + 10); return true;
        case kF4: showPopup(); return true;
        case keys::Space:
            if (!typed_.empty()) return false;
            showPopup();
            return true;
        default: return false;
    }
}

bool ComboBox::charEvent(char32_t c) {
    if (!isEnabled() || c < 0x20 || c == 0x7F) return false;
    if (c == U' ' && typed_.empty()) return false;  // Space opens the drop-down (keyEvent)
    typeahead(utf8(c));
    return true;
}

int ComboBox::searchFrom(const std::string& prefix, int start, bool /*single_letter*/) const {
    const int n = count();
    for (int k = 0; k < n; ++k) {
        const int i = ((start + k) % n + n) % n;
        if (startsWithFold(items_[static_cast<std::size_t>(i)].text, prefix)) return i;
    }
    return -1;
}

void ComboBox::typeahead(const std::string& typed) {
    if (count() == 0) return;
    typed_ += typed;
    if (typed_timer_) stopTimer(typed_timer_);
    typed_timer_ = startTimer(kTypeaheadMs, false, [this] {
        typed_.clear();
        typed_timer_ = 0;
    });
    // "aaa" is the same letter again (cycle); anything else is a growing prefix
    bool same = true;
    for (char ch : typed_) same = same && ch == typed_[0];
    const bool cycle = same;
    const std::string prefix = cycle ? typed_.substr(0, 1) : typed_;
    const int base = popup_ && list_ ? list_->highlighted() : current_;
    const int hit = searchFrom(prefix, cycle ? base + 1 : std::max(base, 0), cycle);
    if (hit >= 0) {
        if (popup_ && list_) list_->setHighlighted(hit);
        else userStep(hit);
    }
    if (!typed_timer_) typed_.clear();  // no clock: each letter stands alone
}

bool ComboBox::accessibleSetValue(std::string_view v) {
    const int i = findText(std::string(v));
    if (i < 0) return false;
    if (i != current_) {
        setCurrent(i, true);
        if (on_activated) on_activated(i);
    }
    return true;
}

}  // namespace tcad::ui

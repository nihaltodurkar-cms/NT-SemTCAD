#include "ui/widgets/message_box.hpp"

#include "ui/core/clipboard.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using tcad::desktop::theme::T;

const char* standardButtonText(StandardButton b) {
    switch (b) {
        case StandardButton::Ok: return "OK";
        case StandardButton::Cancel: return "Cancel";
        case StandardButton::Yes: return "&Yes";
        case StandardButton::No: return "&No";
        case StandardButton::Save: return "&Save";
        case StandardButton::Discard: return "&Discard";
        case StandardButton::Close: return "&Close";
        default: return "";
    }
}

MessageBoxContent::MessageBoxContent(MessageBoxSpec spec) : spec_(std::move(spec)) {
    setFocusPolicy(FocusPolicy::None);
    accessibleName = spec_.title;
    label_ = addChild<Label>(spec_.text);
    label_->setWordWrap(true);
    label_->accessibleName = spec_.text;
    for (StandardButton b : spec_.buttons) {
        auto* pb = addChild<PushButton>(standardButtonText(b));
        pb->on_clicked = [this, b] { press(b); };
        buttons_.emplace_back(b, pb);
    }
    default_ = spec_.default_button != StandardButton::None && button(spec_.default_button) ? spec_.default_button
                                                                                           : (buttons_.empty() ? StandardButton::None : buttons_.front().first);
    if (PushButton* d = button(default_)) d->setDefault(true);
    // Escape: the named button; else Cancel; else the only button; else No; else Close
    auto has = [&](StandardButton b) { return button(b) != nullptr; };
    if (spec_.escape_button != StandardButton::None && has(spec_.escape_button)) escape_ = spec_.escape_button;
    else if (has(StandardButton::Cancel)) escape_ = StandardButton::Cancel;
    else if (buttons_.size() == 1) escape_ = buttons_.front().first;
    else if (has(StandardButton::No)) escape_ = StandardButton::No;
    else if (has(StandardButton::Close)) escape_ = StandardButton::Close;
}

PushButton* MessageBoxContent::button(StandardButton b) const {
    for (const auto& [id, pb] : buttons_)
        if (id == b) return pb;
    return nullptr;
}

void MessageBoxContent::press(StandardButton b) {
    if (finished_) return;
    finished_ = true;
    if (on_finished) on_finished(b);
}

float MessageBoxContent::textHeight() const {
    return label_ ? std::max(label_->heightForWidth(kTextWidth), spec_.icon == MessageIcon::None ? 0.0f : kIconSize) : 0.0f;
}

SizeF MessageBoxContent::sizeHint() const {
    float buttons_w = 0;
    for (const auto& [id, pb] : buttons_) buttons_w += std::max(kMinButton, pb->sizeHint().width) + kButtonGap;
    buttons_w = std::max(0.0f, buttons_w - kButtonGap);
    const float icon_w = spec_.icon == MessageIcon::None ? 0.0f : kIconSize + kGap;
    TextEngine* te = textEngine();
    const float text_w = label_ && te ? std::min(kTextWidth, std::ceil(te->measure(spec_.text, TextStyle{}).width)) : kTextWidth;
    const float inner = std::max(icon_w + text_w, buttons_w);
    return {std::ceil(2 * kMargin + inner), std::ceil(2 * kMargin + textHeight() + kGap + kButtonHeight)};
}

void MessageBoxContent::layoutParts() {
    const double s = scale();
    const SizeF size = sizeDips();
    const float icon_w = spec_.icon == MessageIcon::None ? 0.0f : kIconSize + kGap;
    const float text_w = std::max(0.0f, size.width - 2 * kMargin - icon_w);
    const float th = label_ ? label_->heightForWidth(text_w) : 0;
    label_->setGeometry({roundPx(kMargin + icon_w, s), roundPx(kMargin + std::max(0.0f, (std::max(textHeight(), th) - th) / 2), s), ceilPx(text_w, s), ceilPx(th, s)});
    // the buttons: right-aligned along the bottom
    float x = size.width - kMargin;
    const float y = size.height - kMargin - kButtonHeight;
    for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
        const float w = std::max(kMinButton, it->second->sizeHint().width);
        x -= w;
        it->second->setGeometry({roundPx(x, s), roundPx(y, s), ceilPx(w, s), ceilPx(kButtonHeight, s)});
        x -= kButtonGap;
    }
}

void MessageBoxContent::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));
    // the lower band, where the buttons are: Windows' dialogs shade it
    const float band_top = s.height - kMargin - kButtonHeight - 12;
    p.fillRect({0, band_top, s.width, s.height - band_top}, token(T::AlternateBase));
    p.fillRect({0, band_top, s.width, 1}, token(T::Border));
    if (spec_.icon == MessageIcon::None) return;
    const float cx = kMargin + kIconSize / 2, cy = kMargin + kIconSize / 2;
    const T tone = spec_.icon == MessageIcon::Warning ? T::Warning : spec_.icon == MessageIcon::Critical ? T::Error : T::Accent;
    p.fillEllipse({cx - kIconSize / 2, cy - kIconSize / 2, kIconSize, kIconSize}, token(tone));
    TextStyle g;
    g.size = 20;
    g.bold = true;
    g.color = token(T::OnAccent);
    g.halign = HAlign::Center;
    g.valign = VAlign::Center;
    const char* glyph = spec_.icon == MessageIcon::Information ? "i" : spec_.icon == MessageIcon::Warning ? "!" : spec_.icon == MessageIcon::Critical ? "x" : "?";
    p.drawText({cx - kIconSize / 2, cy - kIconSize / 2, kIconSize, kIconSize}, glyph, g);
}

bool MessageBoxContent::overridesShortcut(const KeyEvent& e) const {
    return e.mods == Mod::Ctrl && e.vk == 'C';
}

std::string MessageBoxContent::copyText() const {
    std::string s = "---------------------------\n" + spec_.title + "\n---------------------------\n" + spec_.text + "\n---------------------------\n";
    bool first = true;
    for (const auto& [id, pb] : buttons_) {
        s += (first ? "" : "   ") + pb->text();
        first = false;
    }
    return s + "\n---------------------------\n";
}

bool MessageBoxContent::keyEvent(const KeyEvent& e) {
    if (!e.down || finished_) return false;
    if (e.mods == Mod::Ctrl && e.vk == 'C') {
        if (Clipboard* cb = host() ? host()->clipboard() : nullptr) cb->setText(copyText());
        return true;
    }
    if (e.mods != Mod::None) return false;
    if (e.vk == keys::Escape) {
        if (escape_ != StandardButton::None) press(escape_);
        return escape_ != StandardButton::None;
    }
    if (e.vk == keys::Return) {  // a focused button handled its own Enter before this; here the focus is not on a button
        if (default_ != StandardButton::None) press(default_);
        return default_ != StandardButton::None;
    }
    if (e.vk == keys::Left || e.vk == keys::Right) {  // between the buttons
        const int n = static_cast<int>(buttons_.size());
        int at = -1;
        for (int i = 0; i < n; ++i)
            if (buttons_[static_cast<std::size_t>(i)].second->hasFocus()) at = i;
        if (n == 0) return false;
        const int to = at < 0 ? 0 : ((at + (e.vk == keys::Right ? 1 : n - 1)) % n);
        buttons_[static_cast<std::size_t>(to)].second->setFocus(FocusReason::Other);
        return true;
    }
    return false;
}

}  // namespace tcad::ui

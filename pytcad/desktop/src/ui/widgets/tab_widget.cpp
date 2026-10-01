#include "ui/widgets/tab_widget.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"
#include "ui/widgets/mnemonic.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

// -- a tab --------------------------------------------------------------------------------------------------------------

int TabButton::accessibleSelectionState() const { return tabs_->isTabEnabled(index_) ? (tabs_->currentIndex() == index_ ? 1 : 0) : -1; }
void TabButton::accessibleSelect() { tabs_->setCurrentIndex(index_); }
Widget* TabButton::accessibleSelectionContainer() const { return tabs_; }
bool TabButton::accessibleFocused() const { return tabs_->hasFocus() && tabs_->currentIndex() == index_; }

void TabButton::activateMnemonic() {
    if (!tabs_->isTabEnabled(index_)) return;
    tabs_->setCurrentIndex(index_);
    tabs_->setFocus(FocusReason::Mnemonic);
}

bool TabButton::mouseEvent(const UiMouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    if (e.type == MouseType::Down || e.type == MouseType::DoubleClick) {
        if (tabs_->isTabEnabled(index_)) tabs_->setCurrentIndex(index_);
        return true;  // (the press also gives the tab widget the focus: a bubbled press finds its Strong policy)
    }
    return e.type == MouseType::Up;
}

void TabButton::paint(Painter& p) {
    const SizeF s = sizeDips();
    const bool sel = tabs_->currentIndex() == index_, on = tabs_->isTabEnabled(index_);
    const bool hc = highContrast().on;
    p.fillRect({0, 0, s.width, s.height}, token(sel ? T::Base : T::AlternateBase));
    const auto c = p.crisp({0, 0, s.width, s.height}, 1.0f);
    p.strokeRect(c.rect, token(T::BorderStrong), c.width);
    if (sel) {
        p.fillRect({1, 0, std::max(0.0f, s.width - 2), 2}, token(T::Accent));  // the selected tab's top bar
        p.fillRect({1, s.height - 1, std::max(0.0f, s.width - 2), 1}, token(T::Base));  // open at the bottom into the page
    }
    TextStyle st;
    st.valign = VAlign::Center;
    st.halign = HAlign::Center;
    st.color = token(!on ? T::TextFaint : (sel && hc) ? T::Text : T::Text);
    const MnemonicText m = parseMnemonic(tabs_->tabText(index_));
    drawMnemonicText(p, textEngine(), {TabWidget::kPadH / 2, 1, std::max(0.0f, s.width - TabWidget::kPadH), s.height - 1}, m, st, mnemonicCuesVisible());
    if (sel && tabs_->hasFocus()) {
        const auto f = p.crisp({3, 4, std::max(0.0f, s.width - 6), std::max(0.0f, s.height - 7)}, 1.0f);
        p.strokeRect(f.rect, token(T::Focus), f.width);
    }
}

// -- the widget ---------------------------------------------------------------------------------------------------------

TabWidget::TabWidget() {
    setFocusPolicy(FocusPolicy::Strong);
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Expanding});
}

int TabWidget::indexOf(const Widget* page) const {
    for (int i = 0; i < count(); ++i)
        if (tabs_[static_cast<std::size_t>(i)].page == page) return i;
    return -1;
}

int TabWidget::addTab(std::unique_ptr<Widget> page, std::string text) { return insertTab(count(), std::move(page), std::move(text)); }

int TabWidget::insertTab(int index, std::unique_ptr<Widget> page, std::string text) {
    if (!page) return -1;
    index = std::clamp(index, 0, count());
    Tab t;
    t.page = adopt(std::move(page));
    if (!t.page) return -1;
    t.text = std::move(text);
    t.button = addChild<TabButton>(this, index);
    tabs_.insert(tabs_.begin() + index, t);
    for (int i = 0; i < count(); ++i) tabs_[static_cast<std::size_t>(i)].button->setIndex(i);
    const MnemonicText m = parseMnemonic(tabs_[static_cast<std::size_t>(index)].text);
    tabs_[static_cast<std::size_t>(index)].button->setMnemonic(m.key);
    tabs_[static_cast<std::size_t>(index)].button->accessibleName = m.text;
    if (current_ >= index && current_ >= 0) ++current_;  // the current tab moved up with its page
    if (current_ < 0) select(index, true);              // the first tab is current
    else layoutParts();
    t.page->setVisible(index == current_);
    updateGeometry();
    return index;
}

std::unique_ptr<Widget> TabWidget::removeTab(int index) {
    if (index < 0 || index >= count()) return nullptr;
    Tab t = tabs_[static_cast<std::size_t>(index)];
    const bool was_current = index == current_;
    tabs_.erase(tabs_.begin() + index);
    release(t.button);
    auto page = release(t.page);
    for (int i = 0; i < count(); ++i) tabs_[static_cast<std::size_t>(i)].button->setIndex(i);
    if (was_current) {
        current_ = -1;
        if (count() > 0) select(std::min(index, count() - 1), true);
        else if (on_current_changed) on_current_changed(-1);
    } else if (current_ > index) {
        --current_;
    }
    layoutParts();
    updateGeometry();
    return page;
}

void TabWidget::select(int i, bool notify) {
    if (i < 0 || i >= count()) return;
    const bool changed = i != current_;
    // a focus inside the page about to be hidden would be dropped: the tab widget takes it, so Ctrl+Tab keeps working
    bool focus_in_old = false;
    if (changed && current_ >= 0 && current_ < count() && host() && host()->input()) {
        for (Widget* f = host()->input()->focusWidget(); f; f = f->parent())
            if (f == tabs_[static_cast<std::size_t>(current_)].page) focus_in_old = true;
    }
    current_ = i;
    for (int k = 0; k < count(); ++k) tabs_[static_cast<std::size_t>(k)].page->setVisible(k == i);
    if (focus_in_old) setFocus(FocusReason::Other);
    layoutParts();
    update();
    for (auto& t : tabs_) t.button->update();
    if (changed) {
        notifyCurrentChanged();
        announce(tabs_[static_cast<std::size_t>(i)].button->accessibleName);
        if (notify && on_current_changed) on_current_changed(i);
    }
}

void TabWidget::setCurrentIndex(int i) {
    if (i < 0 || i >= count() || !tabs_[static_cast<std::size_t>(i)].enabled) return;
    select(i, true);
}

void TabWidget::setCurrentIndexSilent(int i) {
    if (i < 0 || i >= count() || !tabs_[static_cast<std::size_t>(i)].enabled) return;
    select(i, false);
}

std::string TabWidget::tabText(int i) const { return i >= 0 && i < count() ? tabs_[static_cast<std::size_t>(i)].text : std::string(); }

void TabWidget::setTabText(int i, std::string text) {
    if (i < 0 || i >= count()) return;
    Tab& t = tabs_[static_cast<std::size_t>(i)];
    t.text = std::move(text);
    const MnemonicText m = parseMnemonic(t.text);
    t.button->setMnemonic(m.key);
    t.button->accessibleName = m.text;
    layoutParts();
    update();
}

void TabWidget::setTabEnabled(int i, bool on) {
    if (i < 0 || i >= count()) return;
    tabs_[static_cast<std::size_t>(i)].enabled = on;
    tabs_[static_cast<std::size_t>(i)].button->setEnabled(on);
    tabs_[static_cast<std::size_t>(i)].button->update();
}

bool TabWidget::isTabEnabled(int i) const { return i >= 0 && i < count() && tabs_[static_cast<std::size_t>(i)].enabled; }

void TabWidget::setTabToolTip(int i, std::string tip) {
    if (i >= 0 && i < count()) tabs_[static_cast<std::size_t>(i)].button->toolTip = std::move(tip);
}

float TabWidget::tabBarHeight() const {
    TextEngine* te = textEngine();
    return std::ceil((te ? te->measure("", TextStyle{}).height : 16.0f) + 2 * kPadV);
}

RectF TabWidget::tabRect(int i) const {
    TextEngine* te = textEngine();
    if (i < 0 || i >= count()) return {};
    std::vector<float> w;
    float total = 0;
    for (const Tab& t : tabs_) {
        const float tw = te ? std::ceil(te->measure(parseMnemonic(t.text).text, TextStyle{}).width) + 2 * kPadH : 80.0f;
        w.push_back(std::max(tw, kMinTab));
        total += w.back();
    }
    const float avail = sizeDips().width;
    if (total > avail && avail > 0) {  // too many tabs: they share the width (down to the minimum)
        const float k = avail / total;
        for (float& x : w) x = std::max(kMinTab, std::floor(x * k));
    }
    float x = 0;
    for (int k = 0; k < i; ++k) x += w[static_cast<std::size_t>(k)];
    return {x, 0, w[static_cast<std::size_t>(i)], tabBarHeight()};
}

RectF TabWidget::pageRect() const {
    const SizeF s = sizeDips();
    const float top = tabBarHeight() - 1;
    return {0, top, s.width, std::max(0.0f, s.height - top)};
}

void TabWidget::layoutParts() {
    const double s = scale();
    const RectF pr = pageRect();
    for (int i = 0; i < count(); ++i) {
        const RectF r = tabRect(i);
        tabs_[static_cast<std::size_t>(i)].button->setGeometry({roundPx(r.x, s), 0, std::max(1, roundPx(r.right(), s) - roundPx(r.x, s)), ceilPx(r.height, s)});
        // the page sits inside the one-DIP frame
        tabs_[static_cast<std::size_t>(i)].page->setGeometry(
            {roundPx(pr.x + 1, s), roundPx(pr.y + 1, s), std::max(0, roundPx(pr.width - 2, s)), std::max(0, roundPx(pr.height - 2, s))});
    }
}

SizeF TabWidget::sizeHint() const {
    float w = 0, h = 0, bar = 0;
    for (int i = 0; i < count(); ++i) {
        const SizeF p = tabs_[static_cast<std::size_t>(i)].page->sizeHint();
        w = std::max(w, p.width);
        h = std::max(h, p.height);
        bar += tabRect(i).width;
    }
    return {std::max(w + 2, bar), h + tabBarHeight() + 1};
}

SizeF TabWidget::minimumSizeHint() const {
    float w = 0, h = 0;
    for (const Tab& t : tabs_) {
        const SizeF p = t.page->minimumSizeHint();
        w = std::max(w, p.width);
        h = std::max(h, p.height);
    }
    return {w + 2, h + tabBarHeight() + 1};
}

void TabWidget::paint(Painter& p) {
    const SizeF s = sizeDips();
    const RectF pr = pageRect();
    p.fillRect({0, 0, s.width, tabBarHeight()}, token(T::Window));
    p.fillRect({pr.x, pr.y, pr.width, pr.height}, token(T::Base));
    const auto c = p.crisp({pr.x, pr.y, pr.width, pr.height}, 1.0f);
    p.strokeRect(c.rect, token(T::BorderStrong), c.width);
}

int TabWidget::stepEnabled(int from, int dir) const {
    for (int i = from + dir; i >= 0 && i < count(); i += dir)
        if (tabs_[static_cast<std::size_t>(i)].enabled) return i;
    return -1;
}

bool TabWidget::overridesShortcut(const KeyEvent& e) const {
    // Ctrl+Tab and Ctrl+Page keys are the tab widget's even when a field inside has the focus: the field's own handling
    // (a multi-line edit's Tab) must not get them
    return (e.vk == keys::Tab && any(e.mods & Mod::Ctrl)) || ((e.vk == keys::PageUp || e.vk == keys::PageDown) && e.mods == Mod::Ctrl);
}

bool TabWidget::keyEvent(const KeyEvent& e) {
    if (!e.down || count() == 0) return false;
    const bool ctrl = any(e.mods & Mod::Ctrl), shift = any(e.mods & Mod::Shift);
    int dir = 0;
    if (ctrl && e.vk == keys::Tab) dir = shift ? -1 : +1;
    else if (ctrl && e.vk == keys::PageDown) dir = +1;
    else if (ctrl && e.vk == keys::PageUp) dir = -1;
    if (dir != 0) {  // cycle, wrapping, over the enabled tabs
        int i = current_;
        for (int k = 0; k < count(); ++k) {
            i = ((i + dir) % count() + count()) % count();
            if (tabs_[static_cast<std::size_t>(i)].enabled) {
                setCurrentIndex(i);
                return true;
            }
        }
        return true;
    }
    if (ctrl || any(e.mods & Mod::Alt)) return false;
    int to = -1;
    switch (e.vk) {
        case keys::Left: to = stepEnabled(current_, -1); break;
        case keys::Right: to = stepEnabled(current_, +1); break;
        case keys::Home: to = stepEnabled(-1, +1); break;
        case keys::End: to = stepEnabled(count(), -1); break;
        default: return false;
    }
    if (to >= 0) setCurrentIndex(to);
    return true;
}

std::vector<Widget*> TabWidget::accessibleSelection() const {
    std::vector<Widget*> out;
    if (TabButton* b = tabButton(current_)) out.push_back(b);
    return out;
}

}  // namespace tcad::ui

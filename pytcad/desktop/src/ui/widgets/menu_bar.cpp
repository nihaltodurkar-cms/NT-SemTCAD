#include "ui/widgets/menu_bar.hpp"

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

// -- a title ------------------------------------------------------------------------------------------------------------

void MenuBarItem::refresh() {
    const MnemonicText m = parseMnemonic(menu_->title());
    accessibleName = m.text;
    setMnemonic(m.key);
    updateGeometry();
    update();
}

SizeF MenuBarItem::sizeHint() const {
    TextEngine* te = textEngine();
    const MnemonicText m = parseMnemonic(menu_->title());
    const SizeF t = te ? te->measure(m.text, TextStyle{}) : SizeF{40, 16};
    return {std::ceil(t.width) + 2 * kPadH, std::ceil(t.height) + 2 * MenuBar::kPadV};
}

void MenuBarItem::paint(Painter& p) {
    const SizeF s = sizeDips();
    int idx = 0;
    for (; idx < bar_->count() && bar_->item(idx) != this; ++idx) {}
    const bool open = menu_->isOpen(), lit = open || bar_->activeIndex() == idx;
    const bool hc = highContrast().on;
    if (lit) p.fillRect({0, 0, s.width, s.height}, token(T::Selection));
    TextStyle st;
    st.valign = VAlign::Center;
    st.color = token(lit && hc ? T::OnAccent : T::Text);
    const MnemonicText m = parseMnemonic(menu_->title());
    drawMnemonicText(p, textEngine(), {kPadH, 0, std::max(0.0f, s.width - 2 * kPadH), s.height}, m, st, bar_->isActive() || mnemonicCuesVisible());
}

void MenuBarItem::hoverChanged(bool entered) {
    if (entered) {
        int idx = 0;
        for (; idx < bar_->count() && bar_->item(idx) != this; ++idx) {}
        bar_->titleHovered(idx);
    }
    update();
}

bool MenuBarItem::mouseEvent(const UiMouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    if (e.type == MouseType::Down || e.type == MouseType::DoubleClick) {
        int idx = 0;
        for (; idx < bar_->count() && bar_->item(idx) != this; ++idx) {}
        bar_->titleClicked(idx);
        return true;
    }
    return e.type == MouseType::Up;
}

void MenuBarItem::activateMnemonic() {
    int idx = 0;
    for (; idx < bar_->count() && bar_->item(idx) != this; ++idx) {}
    bar_->openMenu(idx, true);
}

bool MenuBarItem::accessibleInvoke() {
    activateMnemonic();
    return true;
}

void MenuBarItem::accessibleExpand(bool open) {
    int idx = 0;
    for (; idx < bar_->count() && bar_->item(idx) != this; ++idx) {}
    if (open) bar_->openMenu(idx, false);
    else menu_->close();
}

// -- the bar ------------------------------------------------------------------------------------------------------------

MenuBar::MenuBar() {
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Fixed});
    name = "menu_bar";
    accessibleName = "Menu bar";
}

MenuBar::~MenuBar() {
    for (MenuBarItem* it : items_) {
        it->menu()->on_navigate = nullptr;
        it->menu()->close();  // an open menu must not outlive the widget it hangs from
    }
    removeFilters();
}

void MenuBar::addMenu(Menu* menu) {
    if (!menu) return;
    auto* it = addChild<MenuBarItem>(this, menu);
    items_.push_back(it);
    it->refresh();
    const int index = static_cast<int>(items_.size()) - 1;
    auto prev = menu->on_closed;
    menu->on_closed = [this, prev, index] {
        if (prev) prev();
        menuClosed(index);
    };
    menu->on_navigate = [this, index](int dir) {
        const int n = count();
        openMenu(((index + dir) % n + n) % n, true);
        return true;
    };
    layoutItems();
    updateGeometry();
}

int MenuBar::openIndex() const {
    for (int i = 0; i < count(); ++i)
        if (items_[static_cast<std::size_t>(i)]->menu()->isOpen()) return i;
    return -1;
}

SizeF MenuBar::sizeHint() const {
    float w = 0, h = 0;
    for (MenuBarItem* it : items_) {
        const SizeF s = it->sizeHint();
        w += s.width;
        h = std::max(h, s.height);
    }
    return {w, h > 0 ? h : 24};
}

void MenuBar::layoutItems() {
    const double s = scale();
    float x = 0;
    for (MenuBarItem* it : items_) {
        const SizeF h = it->sizeHint();
        const int x0 = roundPx(x, s), x1 = roundPx(x + h.width, s);
        it->setGeometry({x0, 0, std::max(1, x1 - x0), geometry().height});
        x += h.width;
    }
}

void MenuBar::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));
    ensureAltHandler();
}

void MenuBar::ensureAltHandler() {
    if (alt_registered_ || !host() || !host()->input()) return;
    // the window's router exists once the bar is laid out. A lone Alt press-and-release is the bar's (not the system's).
    alt_registered_ = true;
    host()->input()->setAltTapHandler(this, [this] {
        if (isActive()) deactivate();
        else activate();
        return true;
    });
}

void MenuBar::titleClicked(int i) {
    MenuBarItem* it = item(i);
    if (!it) return;
    if (it->menu()->isOpen()) {
        it->menu()->close();
        return;
    }
    openMenu(i, false);
}

void MenuBar::titleHovered(int i) {
    if (openIndex() >= 0 && openIndex() != i) openMenu(i, false);  // while a menu is open the pointer switches menus
    else if (isActive() && openIndex() < 0 && active_ != i) {
        active_ = i;
        for (MenuBarItem* it : items_) it->update();
    }
}

void MenuBar::openMenu(int i, bool select_first) {
    MenuBarItem* it = item(i);
    if (!it) return;
    switching_to_ = i;
    for (int k = 0; k < count(); ++k)
        if (k != i && items_[static_cast<std::size_t>(k)]->menu()->isOpen()) items_[static_cast<std::size_t>(k)]->menu()->close();
    switching_to_ = -1;
    active_ = i;
    if (!it->menu()->isOpen()) it->menu()->showBelow(this, it);
    if (select_first && it->menu()->popup()) it->menu()->popup()->first();
    for (MenuBarItem* x : items_) x->update();
    if (!it->menu()->isOpen()) {  // an empty menu: nothing opened
        active_ = -1;
    }
}

void MenuBar::closeMenus() {
    for (MenuBarItem* it : items_) it->menu()->close();
    deactivate();
}

void MenuBar::menuClosed(int) {
    if (switching_to_ >= 0) return;  // closing one menu to open its neighbour
    deactivate();
}

void MenuBar::activate() {
    if (count() == 0) return;
    active_ = 0;
    installFilters();
    for (MenuBarItem* it : items_) it->update();
    announce(items_[0]->accessibleName);
}

void MenuBar::deactivate() {
    if (active_ < 0 && !filters_) return;
    active_ = -1;
    removeFilters();
    for (MenuBarItem* it : items_) it->update();
}

void MenuBar::installFilters() {
    if (filters_ || !host() || !host()->input()) return;
    filters_ = true;
    host()->input()->setKeyFilter(this, [this](const KeyEvent& e) { return keyFilter(e); });
}

void MenuBar::removeFilters() {
    if (!filters_) return;
    filters_ = false;
    if (host() && host()->input()) host()->input()->clearKeyFilter(this);
}

bool MenuBar::keyFilter(const KeyEvent& e) {
    if (!isActive() || openIndex() >= 0) return false;  // (a menu that opened has its own filter)
    if (!e.down) return true;
    const int n = count();
    switch (e.vk) {
        case keys::Left:
            active_ = (active_ - 1 + n) % n;
            break;
        case keys::Right:
            active_ = (active_ + 1) % n;
            break;
        case keys::Down:
        case keys::Up:
        case keys::Return:
        case keys::Space: openMenu(active_, true); return true;
        case keys::Escape: deactivate(); return true;
        default:
            if (e.mods == Mod::None && e.vk >= 'A' && e.vk <= 'Z') {
                for (int i = 0; i < n; ++i) {
                    const char32_t k = parseMnemonic(items_[static_cast<std::size_t>(i)]->menu()->title()).key;
                    if (k && (k == static_cast<char32_t>(e.vk) || k == static_cast<char32_t>(e.vk + 32))) {
                        openMenu(i, true);
                        return true;
                    }
                }
            }
            return !any(e.mods & Mod::Alt);
    }
    for (MenuBarItem* it : items_) it->update();
    announce(items_[static_cast<std::size_t>(active_)]->accessibleName);
    return true;
}

}  // namespace tcad::ui

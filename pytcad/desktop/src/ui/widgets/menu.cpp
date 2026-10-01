#include "ui/widgets/menu.hpp"

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

// -- the model ----------------------------------------------------------------------------------------------------------

Menu::~Menu() { close(); }

void Menu::addAction(Action* a) {
    if (a) entries_.push_back({a, nullptr});
}

void Menu::addSeparator() {
    owned_.push_back(Action::makeSeparator());
    entries_.push_back({owned_.back().get(), nullptr});
}

void Menu::addMenu(Menu* sub) {
    if (sub) entries_.push_back({nullptr, sub});
}

void Menu::clear() {
    close();
    entries_.clear();
    owned_.clear();
}

std::vector<Menu::Entry> Menu::visibleEntries() const {
    std::vector<Entry> out;
    for (const Entry& e : entries_) {
        if (e.action && !e.action->isSeparator() && !e.action->isVisible()) continue;
        const bool sep = e.action && e.action->isSeparator();
        if (sep && (out.empty() || (out.back().action && out.back().action->isSeparator()))) continue;  // not first, not doubled
        out.push_back(e);
    }
    while (!out.empty() && out.back().action && out.back().action->isSeparator()) out.pop_back();  // not last
    return out;
}

bool Menu::open(Widget* owner, const PopupRequest& request, MenuPopup* parent) {
    if (!owner || !owner->host() || !owner->host()->popups()) return false;
    if (isOpen()) close();
    if (on_about_to_show) on_about_to_show();
    std::vector<Entry> entries = visibleEntries();
    if (entries.empty()) return false;
    auto content = std::make_unique<MenuPopup>(this, parent, std::move(entries));
    MenuPopup* raw = content.get();
    PopupHandle* h = owner->host()->popups()->showRequest(request, std::move(content));
    if (!h) return false;
    popup_ = raw;
    handle_ = h;
    owner_ = owner;
    ++shown_;
    root_ = parent == nullptr;
    h->on_dismissed = [this] { popupClosed(); };
    if (root_ && owner->host()->input()) {
        InputRouter* r = owner->host()->input();
        // a press anywhere in the window closes the menus and is swallowed (Windows' own menus do)
        r->setPressFilter(owner, [this](Widget*) {
            close();
            return true;
        });
        // while a menu is open every key goes to it first
        r->setKeyFilter(owner, [this](const KeyEvent& e) {
            if (!popup_) return false;
            if (!e.down) return true;  // the releases of keys it took
            MenuPopup* m = popup_->innermost();
            switch (e.vk) {
                case keys::Up: m->move(-1); return true;
                case keys::Down: m->move(+1); return true;
                case keys::Home: m->first(); return true;
                case keys::End: m->last(); return true;
                case keys::Right:
                    if (m->highlighted() >= 0 && m->entries()[static_cast<std::size_t>(m->highlighted())].submenu) m->openSubmenu(m->highlighted(), true);
                    else if (!m->parentPopup() && on_navigate) on_navigate(+1);
                    return true;
                case keys::Left:
                    if (m->parentPopup()) m->parentPopup()->closeSubmenu();
                    else if (on_navigate) on_navigate(-1);
                    else close();
                    return true;
                case keys::Escape:
                    if (m->parentPopup()) m->parentPopup()->closeSubmenu();
                    else close();
                    return true;
                case keys::Return:
                case keys::Space: m->activate(); return true;
                case keys::Tab: return true;
                default: break;
            }
            if (e.mods == Mod::None && e.vk >= 'A' && e.vk <= 'Z') {
                m->typeMnemonic(static_cast<char32_t>(e.vk));
                return true;
            }
            return any(e.mods & Mod::Alt) ? false : true;  // an Alt chord can still reach the window; other keys are the menu's
        });
    }
    return true;
}

bool Menu::showBelow(Widget* owner, Widget* anchor) {
    PopupRequest rq;
    rq.anchor = anchor;
    return open(owner, rq, nullptr);
}

bool Menu::showAt(Widget* owner, PointF window_dips) {
    PopupRequest rq;
    rq.anchor = owner;
    rq.side = PopupSide::AtPoint;
    rq.point = window_dips;
    return open(owner, rq, nullptr);
}

void Menu::popupClosed() {
    if (popup_) {  // a closed popup may sit in the window's graveyard past its actions and its menu
        popup_->detachActions();
        popup_->menu_ = nullptr;
    }
    popup_ = nullptr;
    handle_ = nullptr;
    if (root_ && owner_ && owner_->host() && owner_->host()->input()) {
        owner_->host()->input()->clearPressFilter(owner_);
        owner_->host()->input()->clearKeyFilter(owner_);
    }
    if (root_ && on_closed) on_closed();
}

void Menu::close() {
    if (!popup_) return;
    MenuPopup* p = popup_;
    p->detachActions();
    if (p->child_ && p->child_->menu_) p->child_->menu_->close();
    p->menu_ = nullptr;  // (the popup may be freed with the window long after this menu is gone)
    PopupHandle* h = handle_;
    popup_ = nullptr;
    handle_ = nullptr;
    if (h) h->close();  // (no on_dismissed: the opener asked)
    if (root_ && owner_ && owner_->host() && owner_->host()->input()) {
        owner_->host()->input()->clearPressFilter(owner_);
        owner_->host()->input()->clearKeyFilter(owner_);
    }
    if (root_ && on_closed) on_closed();
}

// -- the popup ----------------------------------------------------------------------------------------------------------

MenuPopup::MenuPopup(Menu* menu, MenuPopup* parent, std::vector<Menu::Entry> entries)
    : menu_(menu), parent_(parent), entries_(std::move(entries)) {
    if (parent_) parent_->child_ = this;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        auto* it = addChild<MenuItemWidget>(this, static_cast<int>(i));
        items_.push_back(it);
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        // an entry's own changes (checked, enabled, text) repaint the popup while it is open
        listener_ids_.push_back(entries_[i].action ? entries_[i].action->addListener([this] { update(); }) : -1);
    }
}

void MenuPopup::detachActions() {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].action && i < listener_ids_.size()) entries_[i].action->removeListener(listener_ids_[i]);
    listener_ids_.clear();
}

MenuPopup::~MenuPopup() {
    if (submenu_timer_) stopTimer(submenu_timer_);
    detachActions();
    if (parent_ && parent_->child_ == this) parent_->child_ = nullptr;
    if (menu_ && menu_->popup_ == this) {
        menu_->popup_ = nullptr;
        menu_->handle_ = nullptr;
    }
}

MenuPopup* MenuPopup::innermost() {
    MenuPopup* m = this;
    while (m->child_) m = m->child_;
    return m;
}

bool MenuPopup::entryUsable(int i) const {
    if (i < 0 || i >= static_cast<int>(entries_.size())) return false;
    const Menu::Entry& e = entries_[static_cast<std::size_t>(i)];
    if (e.submenu) return true;
    return e.action && !e.action->isSeparator() && e.action->isEnabled();
}

bool MenuPopup::isCheckedEntry(int i) const {
    if (i < 0 || i >= static_cast<int>(entries_.size())) return false;
    const Menu::Entry& e = entries_[static_cast<std::size_t>(i)];
    return e.action && e.action->isCheckable() && e.action->isChecked();
}

std::string MenuPopup::entryText(int i) const {
    if (i < 0 || i >= static_cast<int>(entries_.size())) return {};
    const Menu::Entry& e = entries_[static_cast<std::size_t>(i)];
    return parseMnemonic(e.submenu ? e.submenu->title() : e.action ? e.action->text() : std::string()).text;
}

std::string MenuPopup::entryShortcut(int i) const {
    if (i < 0 || i >= static_cast<int>(entries_.size())) return {};
    const Menu::Entry& e = entries_[static_cast<std::size_t>(i)];
    return e.action ? e.action->shortcutText() : std::string();
}

float MenuPopup::rowHeight(int i) const {
    const Menu::Entry& e = entries_[static_cast<std::size_t>(i)];
    return e.action && e.action->isSeparator() ? kSeparatorHeight : row_h_;
}

float MenuPopup::rowTop(int i) const {
    float y = 1 + kPadV;
    for (int k = 0; k < i; ++k) y += rowHeight(k);
    return y;
}

SizeF MenuPopup::sizeHint() const {
    TextEngine* te = textEngine();
    TextStyle st;
    float text_w = 0, short_w = 0;
    bool any_sub = false;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (te) {
            text_w = std::max(text_w, te->measure(entryText(i), st).width);
            short_w = std::max(short_w, te->measure(entryShortcut(i), st).width);
        }
        any_sub = any_sub || entries_[static_cast<std::size_t>(i)].submenu;
    }
    const float row = te ? std::ceil(te->measure("", st).height) + 8 : 22;
    float h = 2 + 2 * kPadV;
    for (const Menu::Entry& e : entries_) h += e.action && e.action->isSeparator() ? kSeparatorHeight : row;
    const float w = 2 + kCheckColumn + text_w + (short_w > 0 ? kGap + short_w : 0) + (any_sub ? kArrowColumn : 0) + kPadH + 6;
    return {std::ceil(std::max(w, 120.0f)), std::ceil(h)};
}

void MenuPopup::resized() {
    TextEngine* te = textEngine();
    row_h_ = te ? std::ceil(te->measure("", TextStyle{}).height) + 8 : 22;
    layoutItems();
}

void MenuPopup::layoutItems() {
    const double s = scale();
    const RectI g = geometry();
    int i = 0;
    for (MenuItemWidget* it : items_) {
        const float top = rowTop(i), h = rowHeight(i);
        const int y0 = roundPx(top, s), y1 = roundPx(top + h, s);
        const int bpx = std::max(1, roundPx(1.0f, s));
        it->setGeometry({bpx, y0, std::max(0, g.width - 2 * bpx), std::max(1, y1 - y0)});
        it->accessibleName = entryText(i);
        ++i;
    }
}

void MenuPopup::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    const auto b = p.crisp({0, 0, s.width, s.height}, 1.0f);
    p.strokeRect(b.rect, token(T::BorderStrong), b.width);
}

void MenuPopup::setHighlighted(int i) {
    if (i != -1 && !entryUsable(i)) return;
    if (i == highlight_) return;
    if (highlight_ >= 0 && child_ && i != highlight_) closeSubmenu();  // a different entry: its submenu goes
    highlight_ = i;
    if (submenu_timer_) stopTimer(submenu_timer_), submenu_timer_ = 0;
    for (MenuItemWidget* it : items_) it->update();
    if (i >= 0) announce(entryText(i));  // the screen reader speaks the highlighted entry
}

bool MenuPopup::move(int dir) {
    const int n = static_cast<int>(entries_.size());
    if (n == 0) return false;
    int i = highlight_;
    for (int k = 0; k < n; ++k) {
        i = ((i + dir) % n + n) % n;
        if (entryUsable(i)) {
            setHighlighted(i);
            return true;
        }
    }
    return false;
}

bool MenuPopup::activate(int i) {
    if (i == -2) i = highlight_;
    if (!entryUsable(i)) return false;
    const Menu::Entry e = entries_[static_cast<std::size_t>(i)];
    if (e.submenu) return openSubmenu(i, false);
    Action* a = e.action;
    closeAll();   // the menus close first...
    a->trigger();  // ...then the action runs (Qt's order)
    return true;
}

void MenuPopup::closeAll() {
    MenuPopup* root = this;
    while (root->parent_) root = root->parent_;
    Menu* m = root->menu_;
    if (m) m->close();
}

bool MenuPopup::openSubmenu(int i, bool from_keyboard) {
    if (i < 0 || i >= static_cast<int>(entries_.size())) return false;
    Menu* sub = entries_[static_cast<std::size_t>(i)].submenu;
    if (!sub || !entryUsable(i)) return false;
    if (child_ && child_->menu_ == sub) {
        if (from_keyboard) child_->first();
        return true;
    }
    closeSubmenu();
    setHighlighted(i);
    PopupRequest rq;
    rq.anchor = items_[static_cast<std::size_t>(i)];
    rq.side = PopupSide::Right;
    rq.child = true;
    if (!menu_ || !sub->open(menu_->owner_, rq, this)) return false;
    child_ = sub->popup_;
    if (from_keyboard && child_) child_->first();
    return true;
}

void MenuPopup::closeSubmenu() {
    if (child_ && child_->menu_) child_->menu_->close();
    child_ = nullptr;
}

void MenuPopup::armSubmenu(int i) {
    if (submenu_timer_) stopTimer(submenu_timer_);
    pending_submenu_ = i;
    submenu_timer_ = startTimer(Menu::kSubmenuDelayMs, false, [this] {
        submenu_timer_ = 0;
        if (pending_submenu_ == highlight_) openSubmenu(pending_submenu_, false);
    });
}

void MenuPopup::hoverItem(int i, bool entered) {
    if (entered) {
        if (!entryUsable(i)) return;
        setHighlighted(i);
        if (entries_[static_cast<std::size_t>(i)].submenu && !(child_ && child_->menu_ == entries_[static_cast<std::size_t>(i)].submenu)) armSubmenu(i);
    } else if (!child_ && highlight_ == i) {
        highlight_ = -1;  // the pointer left the entry and nothing is open from it
        for (MenuItemWidget* it : items_) it->update();
    }
}

bool MenuPopup::typeMnemonic(char32_t c) {
    auto fold = [](char32_t x) { return x >= U'a' && x <= U'z' ? x - 32 : x; };
    const int n = static_cast<int>(entries_.size());
    std::vector<int> hits;
    for (int i = 0; i < n; ++i) {
        const Menu::Entry& e = entries_[static_cast<std::size_t>(i)];
        const MnemonicText m = parseMnemonic(e.submenu ? e.submenu->title() : e.action ? e.action->text() : std::string());
        if (m.key && fold(m.key) == fold(c) && entryUsable(i)) hits.push_back(i);
    }
    if (hits.empty()) return false;
    if (hits.size() == 1) return activate(hits[0]);
    // several: highlight the next one after the current, activating on the next press
    auto it = std::find_if(hits.begin(), hits.end(), [&](int h) { return h > highlight_; });
    setHighlighted(it == hits.end() ? hits.front() : *it);
    return true;
}

// -- an entry -----------------------------------------------------------------------------------------------------------

void MenuItemWidget::hoverChanged(bool entered) { popup_->hoverItem(index_, entered); }

bool MenuItemWidget::mouseEvent(const UiMouseEvent& e) {
    const bool left = e.button == MouseButton::Left;
    switch (e.type) {
        case MouseType::Move: return false;
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (!left) return false;
            if (popup_->entryUsable(index_)) {
                popup_->pressed_ = index_;
                popup_->setHighlighted(index_);
                if (popup_->entries_[static_cast<std::size_t>(index_)].submenu) popup_->openSubmenu(index_, false);  // a click opens it at once
            }
            return true;
        case MouseType::Up:
            if (!left) return false;
            if (popup_->pressed_ == index_ && popup_->entryUsable(index_)) {
                popup_->pressed_ = -1;
                popup_->activate(index_);
            }
            popup_->pressed_ = -1;
            return true;
        default: return false;
    }
}

bool MenuItemWidget::accessibleEnabled() const { return isEnabled() && popup_->entryUsable(index_); }
Role MenuItemWidget::accessibleRole() const {
    const auto& es = popup_->entries();
    const bool sep = index_ >= 0 && index_ < static_cast<int>(es.size()) && es[static_cast<std::size_t>(index_)].action && es[static_cast<std::size_t>(index_)].action->isSeparator();
    return sep ? Role::Separator : Role::MenuItem;
}
bool MenuItemWidget::accessibleInvoke() { return popup_->activate(index_); }

int MenuItemWidget::accessibleToggleState() const {
    const Menu::Entry& e = popup_->entries_[static_cast<std::size_t>(index_)];
    return e.action && e.action->isCheckable() ? (e.action->isChecked() ? 1 : 0) : -1;
}

int MenuItemWidget::accessibleExpandState() const {
    const Menu::Entry& e = popup_->entries_[static_cast<std::size_t>(index_)];
    if (!e.submenu) return -1;
    return popup_->childPopup() && popup_->childPopup()->menu() == e.submenu ? 1 : 0;
}

void MenuItemWidget::accessibleExpand(bool open) {
    if (open) popup_->openSubmenu(index_, false);
    else popup_->closeSubmenu();
}

void MenuItemWidget::paint(Painter& p) {
    const SizeF s = sizeDips();
    const Menu::Entry& e = popup_->entries_[static_cast<std::size_t>(index_)];
    if (e.action && e.action->isSeparator()) {
        const float y = std::floor(s.height / 2);
        p.fillRect({MenuPopup::kCheckColumn - 4, y, std::max(0.0f, s.width - MenuPopup::kCheckColumn + 4 - 6), 1}, token(T::Border));
        return;
    }
    const bool usable = popup_->entryUsable(index_);
    const bool hot = popup_->highlighted() == index_ && usable;
    const bool hc = highContrast().on;
    if (hot) p.fillRect({0, 0, s.width, s.height}, token(T::Selection));
    const Color fg = token(!usable ? T::TextFaint : (hot && hc) ? T::OnAccent : T::Text);
    if (popup_->isCheckedEntry(index_)) {
        const float cx = 8, cy = std::round(s.height / 2);
        p.drawLine({cx, cy}, {cx + 3, cy + 3}, fg, 1.6f);
        p.drawLine({cx + 3, cy + 3}, {cx + 9, cy - 3}, fg, 1.6f);
    }
    TextStyle st;
    st.color = fg;
    st.valign = VAlign::Center;
    const std::string raw = e.submenu ? e.submenu->title() : e.action->text();
    const MnemonicText m = parseMnemonic(raw);
    drawMnemonicText(p, textEngine(), {MenuPopup::kCheckColumn, 0, std::max(0.0f, s.width - MenuPopup::kCheckColumn), s.height}, m, st, true);
    const std::string sc = popup_->entryShortcut(index_);
    if (!sc.empty()) {
        TextStyle ss = st;
        ss.color = token(!usable ? T::TextFaint : (hot && hc) ? T::OnAccent : T::TextDim);
        ss.halign = HAlign::Right;
        p.drawText({0, 0, s.width - MenuPopup::kPadH, s.height}, sc, ss);
    }
    if (e.submenu) {
        const float cx = s.width - 12, cy = std::round(s.height / 2);
        p.fillPolygon({{cx - 2, cy - 4}, {cx - 2, cy + 4}, {cx + 3, cy}}, fg);
    }
}

}  // namespace tcad::ui

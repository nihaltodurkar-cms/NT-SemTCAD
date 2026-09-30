#include "ui/core/input_router.hpp"

#include "ui/core/keys.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using platform::MouseButton;
using platform::MouseEvent;
using platform::MouseType;

namespace {

void collect(Widget* w, std::vector<Widget*>& out) {
    out.push_back(w);
    for (auto& c : w->children()) collect(c.get(), out);
}

char32_t upper(char32_t c) { return c < 0x10000 ? static_cast<char32_t>(std::towupper(static_cast<wint_t>(c))) : c; }

}  // namespace

InputRouter::InputRouter(Widget& root) : root_(root) {}

InputRouter::~InputRouter() {
    if (tooltip_timer_ && timers_) timers_->stop(tooltip_timer_);  // its callback points at this router
}

// -- the tree -------------------------------------------------------------------------------------------------

std::vector<Widget*> InputRouter::chainAt(PointF p) const {
    std::vector<Widget*> chain;
    const double s = root_.scale();
    const int px = static_cast<int>(std::floor(p.x * s)), py = static_cast<int>(std::floor(p.y * s));
    if (!root_.isVisibleSelf() || !root_.geometry().contains(px, py)) return chain;
    Widget* cur = &root_;
    chain.push_back(cur);
    for (;;) {
        Widget* next = nullptr;
        const auto& kids = cur->children();
        for (auto it = kids.rbegin(); it != kids.rend() && !next; ++it)  // later children paint on top
            if ((*it)->isVisibleSelf() && (*it)->windowRect().contains(px, py)) next = it->get();
        if (!next) break;
        chain.push_back(next);
        cur = next;
    }
    return chain;
}

Widget* InputRouter::widgetAt(PointF p) const {
    const auto c = chainAt(p);
    return c.empty() ? nullptr : c.back();
}

bool InputRouter::alive(const Widget* w) const {
    return w && std::find(dead_.begin(), dead_.end(), w) == dead_.end();
}

bool InputRouter::isHovered(const Widget* w) const { return std::find(hover_.begin(), hover_.end(), w) != hover_.end(); }

void InputRouter::widgetGone(Widget* w) {
    std::vector<Widget*> sub;
    collect(w, sub);
    dead_.insert(dead_.end(), sub.begin(), sub.end());
    auto dead = [&](Widget* x) { return x && std::find(sub.begin(), sub.end(), x) != sub.end(); };
    if (dead(focus_)) focus_ = nullptr;  // no focusChanged on a dying widget
    if (dead(grab_)) grab_ = nullptr, dragging_ = false;
    auto it = std::find_if(hover_.begin(), hover_.end(), dead);
    hover_.erase(it, hover_.end());
    if (dead(tooltip_target_) || dead(tooltip_shown_)) hideTooltip();
}

void InputRouter::checkFocusStillValid() {
    if (focus_ && (!focus_->isVisible() || !focus_->isEnabled())) setFocus(nullptr, FocusReason::Other);
}

// -- hover and tooltips -----------------------------------------------------------------------------------------

void InputRouter::setHover(std::vector<Widget*> chain) {
    const Widget* old_deepest = hoverWidget();
    std::vector<Widget*> old = hover_;
    hover_ = chain;
    for (auto it = old.rbegin(); it != old.rend(); ++it)  // leaving: deepest first
        if (std::find(chain.begin(), chain.end(), *it) == chain.end() && alive(*it)) (*it)->hoverChanged(false);
    for (Widget* w : chain)  // entering: outermost first
        if (std::find(old.begin(), old.end(), w) == old.end() && alive(w) && isHovered(w)) w->hoverChanged(true);
    if (hoverWidget() != old_deepest) {
        hideTooltip();
        armTooltip();
    }
}

void InputRouter::armTooltip() {
    Widget* target = nullptr;
    for (auto it = hover_.rbegin(); it != hover_.rend() && !target; ++it)
        if (!(*it)->toolTip.empty()) target = *it;
    if (!target || !timers_) return;
    tooltip_target_ = target;
    tooltip_timer_ = timers_->start(tooltip_delay_ms_, false, [this] {
        tooltip_timer_ = 0;
        if (!tooltip_target_) return;
        tooltip_shown_ = tooltip_target_;
        if (on_tooltip) on_tooltip(tooltip_shown_, last_pos_);
    });
}

void InputRouter::hideTooltip() {
    if (tooltip_timer_ && timers_) timers_->stop(tooltip_timer_);
    tooltip_timer_ = 0;
    tooltip_target_ = nullptr;
    if (tooltip_shown_) {
        tooltip_shown_ = nullptr;
        if (on_tooltip) on_tooltip(nullptr, {});
    }
}

Cursor InputRouter::cursor() const {
    const Widget* w = grab_ ? grab_ : hoverWidget();
    for (; w; w = w->parent())
        if (w->cursor() != Cursor::Inherit) return w->cursor();
    return Cursor::Arrow;
}

// -- mouse ------------------------------------------------------------------------------------------------------

bool InputRouter::deliverMouse(Widget* target, const MouseEvent& e, PointF p, int clicks, Widget** accepted) {
    // The chain is fixed before any handler runs: a handler may delete widgets, and widgetGone() then clears them.
    std::vector<Widget*> chain;
    for (Widget* w = target; w; w = w->parent()) chain.push_back(w);
    for (Widget* w : chain) {
        if (!alive(w)) return false;  // gone (released or deleted) during this delivery
        if (!w->isEnabled()) return false;        // a disabled widget swallows the event
        UiMouseEvent ev;
        ev.type = e.type;
        ev.button = e.button;
        ev.pos = w->mapFromWindow(p);
        ev.window_pos = p;
        ev.mods = e.mods;
        ev.buttons = e.buttons_down;
        ev.wheel_steps = e.wheel_steps;
        ev.clicks = clicks;
        ev.dragging = dragging_;
        if (w->mouseEvent(ev)) {
            if (accepted) *accepted = w;
            return true;
        }
        if (w == grab_) return false;  // a grab does not bubble
    }
    return false;
}

bool InputRouter::mouse(const MouseEvent& e) {
    beginEvent();
    const PointF p{static_cast<float>(e.x), static_cast<float>(e.y)};
    last_pos_ = p;
    switch (e.type) {
        case MouseType::Leave:
            if (!grab_) setHover({});
            hideTooltip();
            return false;
        case MouseType::Move: {
            if (grab_) {
                const double s = root_.scale();
                if (e.buttons_down && !dragging_ &&
                    std::hypot((p.x - press_pos_.x) * s, (p.y - press_pos_.y) * s) >= drag_threshold_px_)
                    dragging_ = true;
                return deliverMouse(grab_, e, p, 1, nullptr);
            }
            setHover(chainAt(p));
            Widget* w = hoverWidget();
            return w && deliverMouse(w, e, p, 1, nullptr);
        }
        case MouseType::Down:
        case MouseType::DoubleClick: {
            hideTooltip();
            const int clicks = e.type == MouseType::DoubleClick ? 2 : 1;
            if (grab_) return deliverMouse(grab_, e, p, clicks, nullptr);  // another button during a grab
            const auto chain = chainAt(p);
            setHover(chain);  // a press can arrive without a move first
            if (chain.empty()) return false;
            if (e.button == MouseButton::Left || e.button == MouseButton::Right)
                for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                    if ((*it)->acceptsFocus(FocusReason::Mouse)) {
                        setFocus(*it, FocusReason::Mouse);
                        break;
                    }
            Widget* accepted = nullptr;
            if (!alive(chain.back())) return false;  // a focus handler removed it
            const bool used = deliverMouse(chain.back(), e, p, clicks, &accepted);
            if (accepted && alive(accepted)) {
                grab_ = accepted;
                press_pos_ = p;
                dragging_ = false;
            }
            return used;
        }
        case MouseType::Up: {
            if (grab_) {
                Widget* g = grab_;
                const bool used = deliverMouse(g, e, p, 1, nullptr);
                if (e.buttons_down == 0) {
                    grab_ = nullptr;
                    dragging_ = false;
                    setHover(chainAt(p));  // hover was frozen during the grab
                }
                return used;
            }
            Widget* w = widgetAt(p);
            return w && deliverMouse(w, e, p, 1, nullptr);
        }
        case MouseType::Wheel: {
            hideTooltip();
            Widget* w = widgetAt(p);
            return w && deliverMouse(w, e, p, 1, nullptr);
        }
    }
    return false;
}

// -- focus and keys ---------------------------------------------------------------------------------------------

void InputRouter::setFocus(Widget* w, FocusReason why) {
    if (w == focus_) return;
    Widget* old = focus_;
    focus_ = w;
    if (old && active_ && alive(old)) {
        old->focusChanged(false, why);
        old->update();
    }
    if (w && focus_ == w && active_ && alive(w)) {
        w->focusChanged(true, why);
        w->update();
    }
    if (active_ && on_focus_changed && focus_ == w) on_focus_changed(w);
}

std::vector<Widget*> InputRouter::tabChain() const {
    std::vector<Widget*> out;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (!w->isVisibleSelf() || !w->isEnabled()) return;  // a hidden or disabled subtree is out of the order
        if (w->acceptsFocus(FocusReason::Tab) && w->isTabStop()) out.push_back(w);
        for (auto& c : w->children()) walk(c.get());
    };
    walk(&root_);
    return out;
}

bool InputRouter::focusNext(bool forward) {
    const auto chain = tabChain();
    if (chain.empty()) return false;
    const auto it = std::find(chain.begin(), chain.end(), focus_);
    std::size_t next;
    if (it == chain.end()) next = forward ? 0 : chain.size() - 1;
    else {
        const std::size_t i = static_cast<std::size_t>(it - chain.begin());
        next = forward ? (i + 1) % chain.size() : (i + chain.size() - 1) % chain.size();
    }
    setFocus(chain[next], forward ? FocusReason::Tab : FocusReason::Backtab);
    return true;
}

bool InputRouter::key(const KeyEvent& e) {
    beginEvent();
    checkFocusStillValid();
    Widget* f = active_ ? focus_ : nullptr;
    auto deliver = [&]() {
        std::vector<Widget*> chain;
        for (Widget* w = f; w; w = w->parent()) chain.push_back(w);
        for (Widget* w : chain) {
            if (!alive(w) || !w->isEnabled()) return false;
            if (w->keyEvent(e)) return true;
        }
        return false;
    };
    if (!e.down) return f && deliver();
    hideTooltip();
    if (e.vk == keys::Menu && !cues_) {  // Windows shows the keyboard cues from now on (WM_CHANGEUISTATE)
        cues_ = true;
        root_.update();
    }
    if (f && f->overridesShortcut(e) && deliver()) return true;             // (1)
    if (shortcuts_.dispatch(e)) return true;                                // (2)
    if (e.vk == 0x09 && (e.mods == Mod::None || e.mods == Mod::Shift) && !(f && f->wantsTab()))
        return focusNext(e.mods == Mod::None);                             // (3)
    if (e.mods == Mod::Alt && ((e.vk >= 'A' && e.vk <= 'Z') || (e.vk >= '0' && e.vk <= '9'))) {  // (4)
        std::vector<Widget*> all;
        std::function<void(Widget*)> walk = [&](Widget* w) {
            if (!w->isVisibleSelf() || !w->isEnabled()) return;
            if (w->mnemonic() && upper(w->mnemonic()) == static_cast<char32_t>(e.vk)) all.push_back(w);
            for (auto& c : w->children()) walk(c.get());
        };
        walk(&root_);
        if (!all.empty()) {
            // several widgets with one mnemonic: each Alt+key moves to the next (Qt cycles them)
            auto it = std::find(all.begin(), all.end(), focus_);
            Widget* target = it == all.end() || it + 1 == all.end() ? all.front() : *(it + 1);
            target->activateMnemonic();
            return true;
        }
    }
    return f && deliver();                                                  // (5)
}

bool InputRouter::character(char32_t c) {
    beginEvent();
    checkFocusStillValid();
    if (!active_ || !focus_ || !focus_->isEnabled()) return false;
    return focus_->charEvent(c);
}

void InputRouter::windowActivated(bool active) {
    beginEvent();
    if (active == active_) return;
    active_ = active;
    if (!active) {
        hideTooltip();
        grab_ = nullptr;  // Windows takes the capture away with the activation
        dragging_ = false;
    }
    if (focus_ && alive(focus_)) {
        focus_->focusChanged(active, FocusReason::Window);
        focus_->update();
        if (active && on_focus_changed) on_focus_changed(focus_);
    }
}

}  // namespace tcad::ui

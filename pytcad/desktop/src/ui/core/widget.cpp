#include "ui/core/widget.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/layout.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

Widget::Widget() = default;

Widget::~Widget() {
    notifyGone();  // first, while the subtree is intact: the router forgets focus/hover/grab inside it
    layout_.reset();  // the layout refers to children: gone first
    children_.clear();
}

void Widget::notifyGone() {
    UiHost* h = host();
    if (!h) return;
    // Timers belong to the tree's timer service: a widget leaving the tree (released or destroyed) stops its own and
    // its descendants' now, while the service is still reachable -- a detached widget has no host to stop them later.
    std::function<void(Widget*)> stop = [&](Widget* w) {
        if (h->timers())
            for (TimerId id : w->timers_) h->timers()->stop(id);
        w->timers_.clear();
        for (auto& c : w->children_) stop(c.get());
    };
    stop(this);
    h->widgetGone(this);
}

Widget* Widget::adopt(std::unique_ptr<Widget> child) {
    if (!child) return nullptr;
    if (child->parent_) {
        // already owned elsewhere: a programming error the tree must not silently tolerate
        return nullptr;
    }
    child->parent_ = this;
    children_.push_back(std::move(child));
    updateGeometry();
    return children_.back().get();
}

std::unique_ptr<Widget> Widget::release(Widget* child) {
    auto it = std::find_if(children_.begin(), children_.end(), [&](const auto& c) { return c.get() == child; });
    if (it == children_.end()) return nullptr;
    child->notifyGone();  // it leaves this tree (and its host): nothing may point into it
    std::unique_ptr<Widget> out = std::move(*it);
    children_.erase(it);
    out->parent_ = nullptr;
    update();
    updateGeometry();
    return out;
}

Widget* Widget::root() {
    Widget* w = this;
    while (w->parent_) w = w->parent_;
    return w;
}

Widget* Widget::findChild(std::string_view n) {
    if (name == n) return this;
    for (auto& c : children_)
        if (Widget* f = c->findChild(n)) return f;
    return nullptr;
}

UiHost* Widget::host() const {
    const Widget* w = this;
    while (w->parent_) w = w->parent_;
    return w->host_;
}

double Widget::scale() const {
    UiHost* h = host();
    return h ? h->scale() : 1.0;
}

TextEngine* Widget::textEngine() const {
    UiHost* h = host();
    return h ? &h->textEngine() : nullptr;
}

void Widget::setGeometry(const RectI& r) {
    const bool resized_now = r.width != geometry_.width || r.height != geometry_.height;
    if (r != geometry_) update();  // the old area
    geometry_ = r;
    if (layout_) layout_->setGeometryPx(contentsRectPx());
    if (resized_now) resized();
    update();  // the new area
}

RectI Widget::windowRect() const {
    RectI r = geometry_;
    for (const Widget* p = parent_; p; p = p->parent_) r = r.translated(p->geometry_.x, p->geometry_.y);
    return r;
}

SizeF Widget::sizeDips() const {
    const double s = scale();
    return {static_cast<float>(geometry_.width / s), static_cast<float>(geometry_.height / s)};
}

void Widget::setVisible(bool v) {
    if (visible_ == v) return;
    if (!v) update();  // the area it leaves
    visible_ = v;
    if (v) update();
    if (parent_) parent_->updateGeometry();  // a hidden widget takes no space
    else updateGeometry();
    if (!v)
        if (UiHost* h = host(); h && h->input()) h->input()->checkFocusStillValid();
}

bool Widget::isVisible() const {
    for (const Widget* w = this; w; w = w->parent_)
        if (!w->visible_) return false;
    return true;
}

void Widget::setEnabled(bool e) {
    if (enabled_ == e) return;
    enabled_ = e;
    update();
    if (!e)
        if (UiHost* h = host(); h && h->input()) h->input()->checkFocusStillValid();
}

bool Widget::isEnabled() const {
    for (const Widget* w = this; w; w = w->parent_)
        if (!w->enabled_) return false;
    return true;
}

namespace {
int marginsHPx(const Margins& m, double s) { return roundPx(m.left, s) + roundPx(m.right, s); }
int marginsVPx(const Margins& m, double s) { return roundPx(m.top, s) + roundPx(m.bottom, s); }
}  // namespace

SizeF Widget::sizeHint() const {
    if (!layout_) return {0, 0};
    const SizeI p = layout_->sizeHintPx();
    const double s = scale();
    return {static_cast<float>((p.width + marginsHPx(contentsMargins(), s)) / s),
            static_cast<float>((p.height + marginsVPx(contentsMargins(), s)) / s)};
}

SizeF Widget::minimumSizeHint() const {
    if (!layout_) return {0, 0};
    const SizeI p = layout_->minimumSizePx();
    const double s = scale();
    return {static_cast<float>((p.width + marginsHPx(contentsMargins(), s)) / s),
            static_cast<float>((p.height + marginsVPx(contentsMargins(), s)) / s)};
}

bool Widget::hasHeightForWidth() const { return layout_ && layout_->hasHeightForWidth(); }

float Widget::heightForWidth(float width) const {
    if (!layout_) return sizeHint().height;
    const double s = scale();
    const int inner = std::max(0, static_cast<int>(std::lround(width * s)) - marginsHPx(contentsMargins(), s));
    return static_cast<float>((layout_->heightForWidthPx(inner) + marginsVPx(contentsMargins(), s)) / s);
}

void Widget::setContentsMargins(Margins m) {
    if (m == contents_margins_) return;
    contents_margins_ = m;
    if (layout_) layout_->setGeometryPx(contentsRectPx());
    updateGeometry();
}

RectI Widget::contentsRectPx() const {
    const double s = scale();
    const Margins m = contentsMargins();
    const int l = roundPx(m.left, s), t = roundPx(m.top, s);
    return {l, t, std::max(0, geometry_.width - marginsHPx(contentsMargins(), s)),
            std::max(0, geometry_.height - marginsVPx(contentsMargins(), s))};
}

void Widget::setMinimumSize(SizeF s) {
    min_size_ = s;
    updateGeometry();
}

void Widget::setMaximumSize(SizeF s) {
    max_size_ = s;
    updateGeometry();
}

void Widget::setSizePolicy(Policy p) {
    policy_ = p;
    updateGeometry();
}

void Widget::installLayout(std::unique_ptr<Layout> l) {
    layout_ = std::move(l);
    updateGeometry();
}

void Widget::paint(Painter&) {}

void Widget::paintTree(Painter& p) {
    if (!visible_ || geometry_.empty()) return;
    const double s = p.scale();
    p.save();
    p.translate(static_cast<float>(geometry_.x / s), static_cast<float>(geometry_.y / s));
    p.clipRect({0, 0, static_cast<float>(geometry_.width / s), static_cast<float>(geometry_.height / s)});
    paint(p);
    for (auto& c : children_) c->paintTree(p);
    p.restore();
}

void Widget::update() {
    UiHost* h = host();
    if (h && isVisible() && !geometry_.empty()) h->invalidate(windowRect());
}

void Widget::updateGeometry() {
    if (UiHost* h = host()) h->scheduleLayout();
}

// -- input (N2c) --------------------------------------------------------------------------------------------------

bool Widget::acceptsFocus(FocusReason why) const {
    bool policy = false;
    switch (why) {
        case FocusReason::Mouse: policy = focus_policy_ == FocusPolicy::Click || focus_policy_ == FocusPolicy::Strong; break;
        case FocusReason::Tab:
        case FocusReason::Backtab: policy = focus_policy_ == FocusPolicy::Tab || focus_policy_ == FocusPolicy::Strong; break;
        default: policy = focus_policy_ != FocusPolicy::None; break;
    }
    return policy && isVisible() && isEnabled();
}

void Widget::setFocus(FocusReason why) {
    if (focus_proxy_) {
        focus_proxy_->setFocus(why);
        return;
    }
    UiHost* h = host();
    if (h && h->input() && focus_policy_ != FocusPolicy::None && isVisible() && isEnabled()) h->input()->setFocus(this, why);
}

void Widget::announce(std::string_view text) {
    if (UiHost* h = host()) h->announce(this, text);
}

void Widget::clearFocus() {
    UiHost* h = host();
    if (h && h->input() && h->input()->focusWidget() == this) h->input()->setFocus(nullptr, FocusReason::Other);
}

bool Widget::hasFocus() const {
    if (focus_proxy_) return focus_proxy_->hasFocus();
    UiHost* h = host();
    return h && h->input() && h->input()->focusWidget() == this && h->input()->windowActive();
}

bool Widget::mnemonicCuesVisible() const {
    UiHost* h = host();
    return !h || !h->input() || h->input()->mnemonicCuesVisible();
}

bool Widget::isHovered() const {
    UiHost* h = host();
    return h && h->input() && h->input()->isHovered(this);
}

PointF Widget::mapFromWindow(PointF p) const {
    const RectI r = windowRect();
    const double s = scale();
    return {static_cast<float>(p.x - r.x / s), static_cast<float>(p.y - r.y / s)};
}

TimerId Widget::startTimer(int interval_ms, bool repeat, std::function<void()> fn) {
    UiHost* h = host();
    if (!h || !h->timers()) return 0;
    auto id_slot = std::make_shared<TimerId>(0);
    const TimerId id = h->timers()->start(interval_ms, repeat, [this, repeat, id_slot, fn = std::move(fn)] {
        if (!repeat) std::erase(timers_, *id_slot);  // a single-shot is gone once it fires
        fn();
    });
    *id_slot = id;
    if (id) timers_.push_back(id);
    return id;
}

void Widget::stopTimer(TimerId id) {
    UiHost* h = host();
    if (h && h->timers() && std::find(timers_.begin(), timers_.end(), id) != timers_.end()) h->timers()->stop(id);
    std::erase(timers_, id);
}

}  // namespace tcad::ui

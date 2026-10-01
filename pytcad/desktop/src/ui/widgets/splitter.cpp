#include "ui/widgets/splitter.hpp"

#include "ui/core/keys.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

namespace {

// The line that follows the pointer during a drag without opaque resize: a child on top of the others.
class GhostLine : public Widget {
public:
    void paint(Painter& p) override {
        const SizeF s = sizeDips();
        p.fillRect({0, 0, s.width, s.height}, token(T::Focus));
    }
    bool accessibleIsStructural() const override { return true; }
};

}  // namespace

// -- a handle -----------------------------------------------------------------------------------------------------------

void SplitterHandle::paint(Painter& p) {
    const SizeF s = sizeDips();
    const bool horiz = splitter_->orientation() == Orientation::Horizontal;
    const bool lit = isHovered() || dragging_;
    p.fillRect({0, 0, s.width, s.height}, token(lit ? T::AccentSoft : T::Window));
    if (horiz) p.fillRect({std::floor(s.width / 2), 0, 1, s.height}, token(T::Border));
    else p.fillRect({0, std::floor(s.height / 2), s.width, 1}, token(T::Border));
    for (int k = -1; k <= 1; ++k) {  // three grip dots
        const float cx = std::floor(s.width / 2), cy = std::floor(s.height / 2);
        const float dx = horiz ? 0.0f : static_cast<float>(k) * 6.0f, dy = horiz ? static_cast<float>(k) * 6.0f : 0.0f;
        p.fillRect({cx + dx - 1, cy + dy - 1, 2, 2}, token(T::BorderStrong));
    }
    if (hasFocus()) {
        const auto f = p.crisp({0, 0, s.width, s.height}, 1.0f);
        p.strokeRect(f.rect, token(T::Focus), f.width);
    }
}

bool SplitterHandle::mouseEvent(const UiMouseEvent& e) {
    const bool horiz = splitter_->orientation() == Orientation::Horizontal;
    auto axis = [&](PointF p) { return horiz ? p.x : p.y; };
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (e.button != MouseButton::Left) return false;
            dragging_ = true;
            start_pos_ = axis(e.window_pos);
            splitter_->beginDrag(index_);
            update();
            return true;
        case MouseType::Move:
            if (!dragging_) return false;
            splitter_->dragTo(index_, axis(e.window_pos) - start_pos_, false);
            return true;
        case MouseType::Up:
            if (!dragging_ || e.button != MouseButton::Left) return false;
            dragging_ = false;
            splitter_->endDrag(index_);
            update();
            return true;
        default: return false;
    }
}

bool SplitterHandle::keyEvent(const KeyEvent& e) {
    if (!e.down || e.mods != Mod::None) return false;
    const bool horiz = splitter_->orientation() == Orientation::Horizontal;
    const int less = horiz ? keys::Left : keys::Up, more = horiz ? keys::Right : keys::Down;
    float d = 0;
    if (e.vk == less) d = -10;
    else if (e.vk == more) d = 10;
    else if (e.vk == keys::PageUp) d = -50;
    else if (e.vk == keys::PageDown) d = 50;
    else if (e.vk == keys::Home) d = splitter_->minBefore(index_) - splitter_->sizeBefore(index_);
    else if (e.vk == keys::End) d = splitter_->maxBefore(index_) - splitter_->sizeBefore(index_);
    else return false;
    splitter_->moveHandleBy(index_, d);
    return true;
}

AccessibleRange SplitterHandle::accessibleRange() const {
    AccessibleRange r;
    r.valid = true;
    r.value = splitter_->sizeBefore(index_);
    r.minimum = splitter_->minBefore(index_);
    r.maximum = splitter_->maxBefore(index_);
    r.small_step = 10;
    r.large_step = 50;
    return r;
}

bool SplitterHandle::accessibleSetRangeValue(double v) {
    if (!std::isfinite(v) || v < splitter_->minBefore(index_) || v > splitter_->maxBefore(index_)) return false;  // refused, not clamped
    splitter_->moveHandleBy(index_, static_cast<float>(v) - splitter_->sizeBefore(index_));
    return true;
}

// -- the splitter -------------------------------------------------------------------------------------------------------

Splitter::Splitter(Orientation o) : orientation_(o) { setSizePolicy({SizePolicy::Expanding, SizePolicy::Expanding}); }

int Splitter::indexOf(const Widget* w) const {
    for (int i = 0; i < count(); ++i)
        if (children_w_[static_cast<std::size_t>(i)] == w) return i;
    return -1;
}

int Splitter::addWidget(std::unique_ptr<Widget> w) {
    Widget* raw = adopt(std::move(w));
    if (!raw) return -1;
    if (!children_w_.empty()) {
        auto* h = addChild<SplitterHandle>(this, count() - 1);
        h->setFocusPolicy(FocusPolicy::Tab);
        h->setCursor(orientation_ == Orientation::Horizontal ? Cursor::SizeWE : Cursor::SizeNS);
        h->accessibleName = orientation_ == Orientation::Horizontal ? "Splitter (vertical bar)" : "Splitter (horizontal bar)";
        handles_.push_back(h);
    }
    children_w_.push_back(raw);
    sizes_.push_back(0.0f);
    stretch_.push_back(0);
    collapsible_.push_back(children_collapsible_);
    sizes_set_ = false;  // a new child: the initial sizes come from the hints again
    layoutParts();
    updateGeometry();
    return count() - 1;
}

float Splitter::totalAvailable() const {
    const SizeF s = sizeDips();
    return std::max(0.0f, axisOf(s) - static_cast<float>(std::max(0, count() - 1)) * handle_w_);
}

float Splitter::minSize(int i) const {
    const SizeF m = children_w_[static_cast<std::size_t>(i)]->minimumSizeHint();
    return std::max(0.0f, std::max(axisOf(m), axisOf(children_w_[static_cast<std::size_t>(i)]->minimumSize())));
}

float Splitter::minBefore(int h) const { return collapsible_[static_cast<std::size_t>(h)] ? 0.0f : minSize(h); }

float Splitter::maxBefore(int h) const {
    const std::size_t a = static_cast<std::size_t>(h);
    return sizes_[a] + sizes_[a + 1] - (collapsible_[a + 1] ? 0.0f : minSize(h + 1));
}

void Splitter::setHandleWidth(float dips) {
    handle_w_ = std::max(1.0f, dips);
    layoutParts();
}

void Splitter::setStretchFactor(int i, int factor) {
    if (i >= 0 && i < count()) stretch_[static_cast<std::size_t>(i)] = std::max(0, factor);
}

void Splitter::setCollapsible(int i, bool on) {
    if (i >= 0 && i < count()) collapsible_[static_cast<std::size_t>(i)] = on;
}

void Splitter::setChildrenCollapsible(bool on) {
    children_collapsible_ = on;
    for (std::size_t i = 0; i < collapsible_.size(); ++i) collapsible_[i] = on;
}

std::vector<int> Splitter::sizes() const {
    // rounded on the running total, so the children's sizes add up to the whole (as the laid-out geometry does)
    std::vector<int> out;
    float cum = 0;
    long prev = 0;
    for (float s : sizes_) {
        cum += s;
        const long at = std::lround(cum);
        out.push_back(static_cast<int>(at - prev));
        prev = at;
    }
    return out;
}

void Splitter::setSizes(const std::vector<int>& dips) {
    const std::size_t given = std::min(sizes_.size(), dips.size());
    if (given == 0) return;
    if (given < sizes_.size() && sizes_set_) {  // fewer values than children: the ones left out share what remains, as they were
        float given_sum = 0, rest_old = 0;
        for (std::size_t i = 0; i < given; ++i) given_sum += static_cast<float>(std::max(0, dips[i]));
        for (std::size_t i = given; i < sizes_.size(); ++i) rest_old += sizes_[i];
        const float remain = std::max(0.0f, totalAvailable() - given_sum);
        for (std::size_t i = given; i < sizes_.size(); ++i)
            sizes_[i] = rest_old > 0 ? remain * sizes_[i] / rest_old : remain / static_cast<float>(sizes_.size() - given);
    }
    for (std::size_t i = 0; i < given; ++i) sizes_[i] = static_cast<float>(std::max(0, dips[i]));
    sizes_set_ = true;
    laid_total_ = -1;
    layoutParts();
    update();
}

void Splitter::distribute(float new_total) {
    const std::size_t n = sizes_.size();
    const float old_sum = std::accumulate(sizes_.begin(), sizes_.end(), 0.0f);
    float delta = new_total - old_sum;
    if (std::fabs(delta) < 0.01f || n == 0) return;
    std::vector<float> w(n, 0.0f);
    float sum_w = 0;
    int stretch_sum = 0;
    for (int s : stretch_) stretch_sum += s;
    for (std::size_t i = 0; i < n; ++i) {
        const bool collapsed = sizes_[i] < 0.5f && collapsible_[i];
        w[i] = collapsed ? 0.0f : stretch_sum > 0 ? static_cast<float>(stretch_[i]) : sizes_[i];
        sum_w += w[i];
    }
    if (sum_w <= 0)  // nothing to go by: the children that are not collapsed share it
        for (std::size_t i = 0; i < n; ++i) w[i] = (sizes_[i] < 0.5f && collapsible_[i]) ? 0.0f : 1.0f, sum_w += w[i];
    if (sum_w <= 0) return;
    for (std::size_t i = 0; i < n; ++i) sizes_[i] = std::max(0.0f, sizes_[i] + delta * w[i] / sum_w);
    // children that went under their minimum take it from the others (two passes settle a few children)
    for (int pass = 0; pass < 3; ++pass) {
        float deficit = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const float m = minSize(static_cast<int>(i));
            if (sizes_[i] >= 0.5f || !collapsible_[i]) {
                if (sizes_[i] < m) deficit += m - sizes_[i], sizes_[i] = m;
            }
        }
        if (deficit <= 0.01f) break;
        float room = 0;
        for (std::size_t i = 0; i < n; ++i) room += std::max(0.0f, sizes_[i] - minSize(static_cast<int>(i)));
        if (room <= 0) break;
        for (std::size_t i = 0; i < n; ++i) sizes_[i] -= deficit * std::max(0.0f, sizes_[i] - minSize(static_cast<int>(i))) / room;
    }
}

void Splitter::layoutParts() {
    const int n = count();
    if (n == 0) return;
    const SizeF s = sizeDips();
    if (s.width <= 0 || s.height <= 0) return;
    const float total = totalAvailable();
    if (!sizes_set_) {  // in proportion to the children's hints
        std::vector<float> hint(static_cast<std::size_t>(n));
        float sum = 0;
        for (int i = 0; i < n; ++i) {
            hint[static_cast<std::size_t>(i)] = std::max(1.0f, axisOf(children_w_[static_cast<std::size_t>(i)]->sizeHint()));
            sum += hint[static_cast<std::size_t>(i)];
        }
        for (int i = 0; i < n; ++i) sizes_[static_cast<std::size_t>(i)] = total * hint[static_cast<std::size_t>(i)] / sum;
        sizes_set_ = true;
        laid_total_ = total;
    } else if (std::fabs(laid_total_ - total) > 0.01f) {
        distribute(total);
        laid_total_ = total;
    }
    const double k = scale();
    const bool horiz = orientation_ == Orientation::Horizontal;
    float pos = 0;
    const int cross = horiz ? geometry().height : geometry().width;
    for (int i = 0; i < n; ++i) {
        const std::size_t a = static_cast<std::size_t>(i);
        const int p0 = roundPx(pos, k), p1 = roundPx(pos + sizes_[a], k);
        children_w_[a]->setGeometry(horiz ? RectI{p0, 0, std::max(0, p1 - p0), cross} : RectI{0, p0, cross, std::max(0, p1 - p0)});
        pos += sizes_[a];
        if (i < n - 1) {
            const int h0 = roundPx(pos, k), h1 = roundPx(pos + handle_w_, k);
            handles_[a]->setIndex(i);
            handles_[a]->setGeometry(horiz ? RectI{h0, 0, std::max(1, h1 - h0), cross} : RectI{0, h0, cross, std::max(1, h1 - h0)});
            pos += handle_w_;
        }
    }
}

void Splitter::applySizes(const std::vector<float>& s, int moved_handle) {
    sizes_ = s;
    laid_total_ = totalAvailable();
    layoutParts();
    update();
    if (on_splitter_moved) on_splitter_moved(moved_handle);
}

void Splitter::beginDrag(int) {
    dragging_ = true;
    drag_start_ = sizes_;
    pending_ = sizes_;
    if (on_drag_started) on_drag_started();
}

void Splitter::dragTo(int h, float delta, bool commit_now) {
    if (h < 0 || h >= static_cast<int>(sizes_.size()) - 1) return;
    const std::size_t a = static_cast<std::size_t>(h);
    const float sum = drag_start_[a] + drag_start_[a + 1];
    const float min_a = minSize(h), min_b = minSize(h + 1);
    float na = drag_start_[a] + delta;
    if (collapsible_[a] && na < min_a / 2) na = 0;           // pushed far enough: it collapses
    else if (na < min_a) na = std::min(min_a, sum);          // otherwise it stops at its minimum
    float nb = sum - na;
    if (collapsible_[a + 1] && nb < min_b / 2) nb = 0, na = sum;
    else if (nb < min_b) nb = std::min(min_b, sum), na = sum - nb;
    pending_ = drag_start_;
    pending_[a] = na;
    pending_[a + 1] = nb;
    if (opaque_ || commit_now) {
        applySizes(pending_, h);
        return;
    }
    // a line follows the pointer; the children change when the button is released
    float before = 0;
    for (std::size_t i = 0; i < a; ++i) before += pending_[i] + handle_w_;
    ghost_ = before + na + handle_w_ / 2;
    const bool horiz = orientation_ == Orientation::Horizontal;
    if (!ghost_w_) ghost_w_ = addChild<GhostLine>();
    const double k = scale();
    const int g = roundPx(ghost_ - 1, k), w = std::max(1, roundPx(2.0f, k));
    ghost_w_->setGeometry(horiz ? RectI{g, 0, w, geometry().height} : RectI{0, g, geometry().width, w});
    ghost_w_->setVisible(true);
    ghost_w_->update();
}

void Splitter::endDrag(int h) {
    if (!dragging_) return;
    dragging_ = false;
    if (!opaque_ && pending_.size() == sizes_.size() && pending_ != sizes_) applySizes(pending_, h);
    ghost_ = -1;
    if (ghost_w_) ghost_w_->setVisible(false);
    if (on_drag_finished) on_drag_finished();
}

void Splitter::moveHandleBy(int h, float dips) {
    drag_start_ = sizes_;
    pending_ = sizes_;
    dragTo(h, dips, true);
}

SizeF Splitter::sizeHint() const {
    float along = static_cast<float>(std::max(0, count() - 1)) * handle_w_, across = 0;
    for (Widget* w : children_w_) {
        const SizeF h = w->sizeHint();
        along += axisOf(h);
        across = std::max(across, orientation_ == Orientation::Horizontal ? h.height : h.width);
    }
    return orientation_ == Orientation::Horizontal ? SizeF{along, across} : SizeF{across, along};
}

SizeF Splitter::minimumSizeHint() const {
    float along = static_cast<float>(std::max(0, count() - 1)) * handle_w_, across = 0;
    for (int i = 0; i < count(); ++i) {
        const SizeF h = children_w_[static_cast<std::size_t>(i)]->minimumSizeHint();
        along += collapsible_[static_cast<std::size_t>(i)] ? 0.0f : axisOf(h);
        across = std::max(across, orientation_ == Orientation::Horizontal ? h.height : h.width);
    }
    return orientation_ == Orientation::Horizontal ? SizeF{along, across} : SizeF{across, along};
}

void Splitter::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));
}

}  // namespace tcad::ui

#include "ui/widgets/scroll_area.hpp"

#include "ui/core/input_router.hpp"
#include "ui/core/layout.hpp"
#include "ui/core/style.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using tcad::desktop::theme::T;

// The clipped area the content lives in (invisible to UI Automation: the content shows as the area's own child).
class ScrollArea::Viewport : public Widget {
public:
    bool accessibleIsStructural() const override { return true; }
};

ScrollArea::ScrollArea() {
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Expanding});
    viewport_ = addChild<Viewport>();
    vbar_ = addChild<ScrollBar>(Orientation::Vertical);
    hbar_ = addChild<ScrollBar>(Orientation::Horizontal);
    vbar_->setVisible(false);
    hbar_->setVisible(false);
    vbar_->name = "vertical_scroll_bar";
    hbar_->name = "horizontal_scroll_bar";
    vbar_->accessibleName = "Vertical scroll bar";
    hbar_->accessibleName = "Horizontal scroll bar";
    vbar_->setSingleStep(kStep);
    hbar_->setSingleStep(kStep);
    auto moved = [this](int) {
        if (in_layout_ || !content_) return;
        const double s = scale();
        const RectI g = content_->geometry();
        content_->setGeometry({-roundPx(static_cast<float>(hbar_->value()), s), -roundPx(static_cast<float>(vbar_->value()), s), g.width, g.height});
        update();
    };
    vbar_->on_value_changed = moved;
    hbar_->on_value_changed = moved;
}

ScrollArea::~ScrollArea() {
    if (observing_ && host() && host()->input()) host()->input()->removeFocusObserver(this);
}

Widget* ScrollArea::setWidget(std::unique_ptr<Widget> w) {
    if (content_) viewport_->release(content_);
    content_ = w ? viewport_->adopt(std::move(w)) : nullptr;
    vbar_->setValueSilent(0);
    hbar_->setValueSilent(0);
    relayout();
    return content_;
}

void ScrollArea::setWidgetResizable(bool on) {
    resizable_ = on;
    relayout();
}

void ScrollArea::setFrame(bool on) {
    frame_ = on;
    relayout();
}

RectF ScrollArea::viewportRect() const {
    const RectI g = viewport_->geometry();
    const double s = scale();
    return {static_cast<float>(g.x / s), static_cast<float>(g.y / s), static_cast<float>(g.width / s), static_cast<float>(g.height / s)};
}

void ScrollArea::ensureFocusObserver() {
    if (observing_ || !host() || !host()->input()) return;
    observing_ = true;
    host()->input()->addFocusObserver(this, [this](Widget* w) { focusMoved(w); });
}

void ScrollArea::focusMoved(Widget* now) {
    if (!content_ || !now || !isVisible()) return;
    for (const Widget* p = now; p; p = p->parent())
        if (p == content_) {
            ensureWidgetVisible(now, 6);
            return;
        }
}

void ScrollArea::relayout() {
    const RectI g = geometry();
    if (g.empty() || in_layout_) return;
    in_layout_ = true;
    ensureFocusObserver();
    const double s = scale();
    const int bpx = frame_ ? std::max(1, roundPx(1.0f, s)) : 0, tpx = roundPx(ScrollBar::kThickness, s);
    bool vneed = false, hneed = false;
    float vw = 0, vh = 0, cw = 0, ch = 0;
    for (int pass = 0; pass < 3; ++pass) {  // a bar takes room from the other axis: monotone, so it settles
        vw = static_cast<float>((g.width - 2 * bpx - (vneed ? tpx : 0)) / s);
        vh = static_cast<float>((g.height - 2 * bpx - (hneed ? tpx : 0)) / s);
        if (content_) {
            const SizeF hint = content_->sizeHint(), mn = content_->minimumSizeHint();
            if (resizable_) {
                cw = std::max(vw, mn.width);  // as wide as the viewport (a form reflows), but not narrower than it can be
                ch = content_->hasHeightForWidth() ? content_->heightForWidth(cw) : hint.height;
                ch = std::max(ch, std::max(vh, mn.height));
                if (content_->layout() && content_->layout()->hasHeightForWidth()) ch = std::max(ch, static_cast<float>(content_->layout()->heightForWidthPx(ceilPx(cw, s)) / s));
            } else {
                cw = hint.width;
                ch = hint.height;
            }
        } else {
            cw = ch = 0;
        }
        const bool nv = vneed || ch > vh + 0.01f, nh = hneed || cw > vw + 0.01f;
        if (nv == vneed && nh == hneed) break;
        vneed = nv;
        hneed = nh;
    }
    view_w_ = vw;
    view_h_ = vh;
    content_w_ = cw;
    content_h_ = ch;
    const int view_w = std::max(0, g.width - 2 * bpx - (vneed ? tpx : 0)), view_h = std::max(0, g.height - 2 * bpx - (hneed ? tpx : 0));
    vbar_->setPageStep(std::max(1, static_cast<int>(vh)));
    hbar_->setPageStep(std::max(1, static_cast<int>(vw)));
    vbar_->setRange(0, vneed ? static_cast<int>(std::ceil(ch - vh)) : 0);
    hbar_->setRange(0, hneed ? static_cast<int>(std::ceil(cw - vw)) : 0);
    vbar_->setVisible(vneed);
    hbar_->setVisible(hneed);
    viewport_->setGeometry({bpx, bpx, view_w, view_h});
    vbar_->setGeometry({g.width - bpx - tpx, bpx, tpx, view_h});
    hbar_->setGeometry({bpx, g.height - bpx - tpx, view_w, tpx});
    if (content_)
        content_->setGeometry({-roundPx(static_cast<float>(hbar_->value()), s), -roundPx(static_cast<float>(vbar_->value()), s), ceilPx(cw, s), ceilPx(ch, s)});
    in_layout_ = false;
    update();
}

void ScrollArea::ensureVisible(int x, int y, int margin) {
    int sx = hbar_->value(), sy = vbar_->value();
    if (x - margin < sx) sx = std::max(0, x - margin);
    else if (x + margin > sx + static_cast<int>(view_w_)) sx = x + margin - static_cast<int>(view_w_);
    if (y - margin < sy) sy = std::max(0, y - margin);
    else if (y + margin > sy + static_cast<int>(view_h_)) sy = y + margin - static_cast<int>(view_h_);
    hbar_->setValue(sx);
    vbar_->setValue(sy);
}

void ScrollArea::ensureWidgetVisible(const Widget* w, int margin) {
    if (!content_ || !w) return;
    // the widget's rectangle in the content's coordinates (device px to DIPs)
    const double s = scale();
    const RectI wr = w->windowRect(), cr = content_->windowRect();
    const float x0 = static_cast<float>((wr.x - cr.x) / s), y0 = static_cast<float>((wr.y - cr.y) / s);
    const float x1 = x0 + static_cast<float>(wr.width / s), y1 = y0 + static_cast<float>(wr.height / s);
    int sx = hbar_->value(), sy = vbar_->value();
    const int vw = static_cast<int>(view_w_), vh = static_cast<int>(view_h_);
    if (y1 + margin > sy + vh) sy = static_cast<int>(std::ceil(y1)) + margin - vh;
    if (y0 - margin < sy) sy = std::max(0, static_cast<int>(std::floor(y0)) - margin);  // (a widget taller than the view shows its top)
    if (x1 + margin > sx + vw) sx = static_cast<int>(std::ceil(x1)) + margin - vw;
    if (x0 - margin < sx) sx = std::max(0, static_cast<int>(std::floor(x0)) - margin);
    hbar_->setValue(sx);
    vbar_->setValue(sy);
}

SizeF ScrollArea::sizeHint() const {
    const float b = frame_ ? 2.0f : 0.0f;
    if (!content_) return {256, 192};
    const SizeF h = content_->sizeHint();
    return {std::min(h.width, 360.0f) + b, std::min(h.height, 400.0f) + b};  // (Qt caps it so a tall form does not ask for the screen)
}

void ScrollArea::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));
    if (frame_) {
        const auto c = p.crisp({0, 0, s.width, s.height}, 1.0f);
        p.strokeRect(c.rect, token(T::BorderStrong), c.width);
    }
    if (vbar_->isVisibleSelf() && hbar_->isVisibleSelf()) {  // the corner where the bars meet
        const double k = scale();
        p.fillRect({static_cast<float>(vbar_->geometry().x / k), static_cast<float>(hbar_->geometry().y / k),
                    static_cast<float>(vbar_->geometry().width / k), static_cast<float>(hbar_->geometry().height / k)},
                   token(T::AlternateBase));
    }
}

bool ScrollArea::mouseEvent(const UiMouseEvent& e) {
    if (e.type != platform::MouseType::Wheel || e.wheel_steps == 0) return false;
    ScrollBar* b = vbar_->isNeeded() ? vbar_ : hbar_->isNeeded() ? hbar_ : nullptr;
    if (!b) return false;
    int d = static_cast<int>(std::lround(e.wheel_steps * 3 * kStep));
    if (d == 0) d = e.wheel_steps > 0 ? 1 : -1;
    b->setValue(b->value() - d);  // away from the user = up
    return true;
}

AccessibleScroll ScrollArea::accessibleScroll() const {
    AccessibleScroll a;
    a.valid = true;
    a.vertical = vbar_->isNeeded();
    a.horizontal = hbar_->isNeeded();
    a.v_percent = a.vertical ? 100.0 * vbar_->value() / vbar_->maximum() : 0;
    a.h_percent = a.horizontal ? 100.0 * hbar_->value() / hbar_->maximum() : 0;
    a.v_view = content_h_ > 0 ? std::min(100.0, 100.0 * view_h_ / content_h_) : 100;
    a.h_view = content_w_ > 0 ? std::min(100.0, 100.0 * view_w_ / content_w_) : 100;
    return a;
}

void ScrollArea::accessibleScrollBy(int h, int v) {
    auto by = [](ScrollBar* b, int amount) {
        if (amount == 0 || !b->isNeeded()) return;
        const int d = (amount == 1 || amount == -1) ? b->singleStep() : b->pageStep();
        b->setValue(b->value() + (amount > 0 ? d : -d));
    };
    by(hbar_, h);
    by(vbar_, v);
}

void ScrollArea::accessibleSetScrollPercent(double h, double v) {
    if (h >= 0 && hbar_->isNeeded()) hbar_->setValue(static_cast<int>(std::lround(std::clamp(h, 0.0, 100.0) / 100.0 * hbar_->maximum())));
    if (v >= 0 && vbar_->isNeeded()) vbar_->setValue(static_cast<int>(std::lround(std::clamp(v, 0.0, 100.0) / 100.0 * vbar_->maximum())));
}

}  // namespace tcad::ui

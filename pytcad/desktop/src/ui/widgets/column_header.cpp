#include "ui/widgets/column_header.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

void HeaderCell::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));
    p.fillRect({s.width - 1, 0, 1, s.height}, token(T::Border));  // the divider on the right
    TextStyle st;
    st.color = token(T::Text);
    st.valign = VAlign::Center;
    p.drawText({ColumnHeaderBand::kCellPad, 0, std::max(0.0f, s.width - 2 * ColumnHeaderBand::kCellPad), s.height}, accessibleName, st);
}

void ColumnHeaderBand::sync() {
    ItemView* v = model_->headerView();
    const double s = v->scale();
    const RectI g = geometry();
    const int n = model_->columnCount();
    while (static_cast<int>(children().size()) > n) release(children().back().get());
    while (static_cast<int>(children().size()) < n) addChild<HeaderCell>(model_, static_cast<int>(children().size()));
    const float scroll = static_cast<float>(v->horizontalScrollBar()->value());
    int i = 0;
    for (auto& ch : children()) {
        auto* h = static_cast<HeaderCell*>(ch.get());
        h->setColumn(i);
        h->accessibleName = model_->headerLabel(i);
        const float x0 = model_->gutterWidth() + model_->columnX(i) - scroll, x1 = x0 + model_->columnWidth(i);
        const int px0 = roundPx(x0, s), px1 = roundPx(x1, s);
        h->setGeometry({px0, 0, std::max(1, px1 - px0), g.height});
        h->update();
        ++i;
    }
    update();
}

void ColumnHeaderBand::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Window));  // (and the corner above a table's row numbers)
    p.fillRect({0, s.height - 1, s.width, 1}, token(T::Border));
}

int ColumnHeaderBand::separatorAt(float x) const {
    const float scroll = static_cast<float>(model_->headerView()->horizontalScrollBar()->value());
    for (int c = 0; c < model_->columnCount(); ++c) {
        const float edge = model_->gutterWidth() + model_->columnX(c) + model_->columnWidth(c) - scroll;
        if (std::fabs(x - edge) <= 3.0f) return c;
    }
    return -1;
}

bool ColumnHeaderBand::mouseEvent(const UiMouseEvent& e) {
    const bool left = e.button == MouseButton::Left;
    switch (e.type) {
        case MouseType::Move:
            if (drag_col_ >= 0 && (e.buttons & (1u << static_cast<int>(MouseButton::Left)))) {
                model_->setColumnWidth(drag_col_, drag_start_w_ + (e.pos.x - drag_start_x_));
                return true;
            }
            setCursor(separatorAt(e.pos.x) >= 0 ? Cursor::SizeWE : Cursor::Arrow);
            return false;
        case MouseType::Down: {
            const int c = separatorAt(e.pos.x);
            if (!left || c < 0) return left;  // a press on a header takes the mouse (nothing sorts or selects)
            drag_col_ = c;
            drag_start_x_ = e.pos.x;
            drag_start_w_ = model_->columnWidth(c);
            return true;
        }
        case MouseType::DoubleClick: {
            const int c = separatorAt(e.pos.x);
            if (left && c >= 0) model_->resizeColumnToContents(c);
            return left;
        }
        case MouseType::Up:
            if (drag_col_ < 0) return false;
            drag_col_ = -1;
            return true;
        default: return false;
    }
}

}  // namespace tcad::ui

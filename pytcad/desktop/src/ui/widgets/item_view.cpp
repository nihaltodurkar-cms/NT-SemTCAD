#include "ui/widgets/item_view.hpp"

#include "ui/core/clipboard.hpp"
#include "ui/core/keys.hpp"
#include "ui/core/selection.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using platform::Mod;
using platform::MouseButton;
using platform::MouseType;
using tcad::desktop::theme::T;

// -- the row widget ---------------------------------------------------------------------------------------------------

void ItemView::repaintRows() {
    for (auto& [i, r] : rows_) r->update();
    update();
}

void ItemView::refreshRealizedRows() {
    for (auto& [i, r] : rows_) refreshRowLooks(r);
}

void ItemRow::paint(Painter& p) { view_->paintRowFor(p, row_, sizeDips(), isHovered()); }

bool ItemRow::mouseEvent(const UiMouseEvent& e) { return view_->rowMouse(row_, e); }

Role ItemRow::accessibleRole() const { return view_->rowRoleFor(row_); }

int ItemRow::accessibleSelectionState() const { return view_->rowSelectionStateFor(row_); }
bool ItemRow::accessibleFocused() const { return view_->hasFocus() && view_->currentRow() == row_; }

void ItemRow::accessibleSelect() { view_->accessibleSelectRow(row_); }
bool ItemRow::accessibleAddToSelection() { return view_->accessibleAddRow(row_); }
bool ItemRow::accessibleRemoveFromSelection() { return view_->accessibleRemoveRow(row_); }
Widget* ItemRow::accessibleSelectionContainer() const { return view_; }

int ItemRow::accessibleToggleState() const { return view_->rowToggleStateFor(row_); }
void ItemRow::accessibleToggle() { view_->rowToggleFor(row_); }
int ItemRow::accessibleExpandState() const { return view_->rowExpandStateFor(row_); }
void ItemRow::accessibleExpand(bool open) { view_->rowExpandFor(row_, open); }
void ItemRow::accessibleScrollIntoView() { view_->scrollToRow(row_); }
AccessibleCell ItemRow::accessibleCell() const { return view_->rowCellFor(row_); }

// -- the check mark ---------------------------------------------------------------------------------------------------

void paintCheckMark(Painter& p, const RectF& box, int state, bool enabled) {
    p.fillRect(box, token(T::Base));
    const auto edge = p.crisp(box, 1.0f);
    p.strokeRect(edge.rect, token(enabled ? T::BorderStrong : T::Border), edge.width);
    const Color mark = token(enabled ? T::Text : T::TextFaint);
    if (state == 1) {
        const float x = box.x, y = box.y, w = box.width, h = box.height;
        p.drawLine({x + w * 0.22f, y + h * 0.52f}, {x + w * 0.42f, y + h * 0.74f}, mark, 1.6f);
        p.drawLine({x + w * 0.42f, y + h * 0.74f}, {x + w * 0.80f, y + h * 0.28f}, mark, 1.6f);
    } else if (state == 2) {
        p.fillRect({box.x + box.width * 0.28f, box.y + box.height * 0.28f, box.width * 0.44f, box.height * 0.44f}, mark);
    }
}

// -- construction -----------------------------------------------------------------------------------------------------

ItemView::ItemView() {
    setFocusPolicy(FocusPolicy::Strong);
    setSizePolicy({SizePolicy::Expanding, SizePolicy::Expanding});
    viewport_ = addChild<ItemViewport>();
    vbar_ = addChild<ScrollBar>(Orientation::Vertical);
    hbar_ = addChild<ScrollBar>(Orientation::Horizontal);
    vbar_->setVisible(false);
    hbar_->setVisible(false);
    vbar_->name = "vertical_scroll_bar";
    hbar_->name = "horizontal_scroll_bar";
    vbar_->accessibleName = "Vertical scroll bar";
    hbar_->accessibleName = "Horizontal scroll bar";
    auto scrolled = [this] {
        if (!in_layout_) realize();
        update();
    };
    vbar_->on_value_changed = [scrolled](int) { scrolled(); };
    hbar_->on_value_changed = [this, scrolled](int) {
        scrolled();
        horizontalScrolled();
    };
}

ItemView::~ItemView() = default;

float ItemView::rowHeight() const {
    TextEngine* te = textEngine();
    const float line = te ? te->measure("", TextStyle{}).height : 16.0f;
    return std::ceil(line + kRowPad);
}

RectF ItemView::viewportRect() const {
    const RectI g = viewport_->geometry();
    const double s = scale();
    return {static_cast<float>(g.x / s), static_cast<float>(g.y / s), static_cast<float>(g.width / s), static_cast<float>(g.height / s)};
}

RectF ItemView::rowRect(int row) const {
    const RectF v = viewportRect();
    const float rh = rowHeight();
    const float w = std::max(v.width, contentWidth());
    return {v.x - static_cast<float>(hbar_->value()), v.y + static_cast<float>(row) * rh - static_cast<float>(vbar_->value()), w, rh};
}

int ItemView::rowAt(PointF local) const {
    const RectF v = viewportRect();
    if (local.x < v.x || local.x >= v.right() || local.y < v.y || local.y >= v.bottom()) return -1;
    const int row = static_cast<int>(std::floor((local.y - v.y + static_cast<float>(vbar_->value())) / rowHeight()));
    return row >= 0 && row < rowCount() ? row : -1;
}

int ItemView::firstVisibleRow() const {
    if (rowCount() == 0) return -1;
    return std::clamp(static_cast<int>(std::floor(static_cast<float>(vbar_->value()) / rowHeight())), 0, rowCount() - 1);
}

int ItemView::lastVisibleRow() const {
    if (rowCount() == 0) return -1;
    const float bottom = static_cast<float>(vbar_->value()) + viewport_h_;
    return std::clamp(static_cast<int>(std::ceil(bottom / rowHeight())) - 1, 0, rowCount() - 1);
}

ItemRow* ItemView::realizedRow(int row) const {
    const auto it = rows_.find(row);
    return it == rows_.end() ? nullptr : it->second;
}

// -- geometry and realization -----------------------------------------------------------------------------------------

void ItemView::resized() { relayout(); }

void ItemView::relayout() {
    layoutParts();
    realize();
    update();
}

void ItemView::contentChangedWidth() { relayout(); }

void ItemView::layoutParts() {
    const RectI g = geometry();
    if (g.empty() || in_layout_) return;
    in_layout_ = true;
    const double s = scale();
    const int bpx = std::max(1, roundPx(kBorder, s)), tpx = roundPx(ScrollBar::kThickness, s);
    const int hpx = ceilPx(headerHeight(), s);
    const float rh = rowHeight();
    bool vneed = false, hneed = false;
    float vw = 0, vh = 0;
    for (int pass = 0; pass < 3; ++pass) {  // a bar takes room, which can make the other needed: monotone
        vw = static_cast<float>((g.width - 2 * bpx - (vneed ? tpx : 0)) / s);
        vh = static_cast<float>((g.height - 2 * bpx - hpx - (hneed ? tpx : 0)) / s);
        const bool nv = vneed || static_cast<float>(rowCount()) * rh > vh + 0.01f;
        const bool nh = hneed || contentWidth() > vw + 0.01f;
        if (nv == vneed && nh == hneed) break;
        vneed = nv;
        hneed = nh;
    }
    viewport_w_ = vw;
    viewport_h_ = vh;
    const float content_h = static_cast<float>(rowCount()) * rh;
    vbar_->setSingleStep(std::max(1, static_cast<int>(std::lround(rh))));
    vbar_->setPageStep(std::max(1, static_cast<int>(vh)));
    vbar_->setRange(0, vneed ? static_cast<int>(std::ceil(content_h - vh)) : 0);
    hbar_->setSingleStep(20);
    hbar_->setPageStep(std::max(1, static_cast<int>(vw)));
    hbar_->setRange(0, hneed ? static_cast<int>(std::ceil(contentWidth() - vw)) : 0);
    vbar_->setVisible(vneed);
    hbar_->setVisible(hneed);
    const int view_w = std::max(0, g.width - 2 * bpx - (vneed ? tpx : 0));
    const int view_h = std::max(0, g.height - 2 * bpx - hpx - (hneed ? tpx : 0));
    viewport_->setGeometry({bpx, bpx + hpx, view_w, view_h});
    vbar_->setGeometry({g.width - bpx - tpx, bpx + hpx, tpx, view_h});
    hbar_->setGeometry({bpx, g.height - bpx - tpx, view_w, tpx});
    layoutHeader({bpx, bpx, view_w, hpx});
    in_layout_ = false;
}

void ItemView::realize() {
    const int n = rowCount();
    const double s = scale();
    const float rh = rowHeight();
    const int sy = vbar_->value(), sx = hbar_->value();
    int first = 0, last = -1;
    if (n > 0 && viewport_h_ > 0) {
        first = std::clamp(static_cast<int>(std::floor(sy / rh)), 0, n - 1);
        last = std::clamp(static_cast<int>(std::ceil((sy + viewport_h_) / rh)) - 1, 0, n - 1);
    }
    bool removed = false;
    for (auto it = rows_.begin(); it != rows_.end();) {  // rows that left the range, or that no longer exist
        if (it->first < first || it->first > last) {
            removed = true;
            ItemRow* r = it->second;
            it = rows_.erase(it);
            viewport_->release(r);  // the unique_ptr dies here: the row's widgets go, their UI Automation elements with them
        } else {
            ++it;
        }
    }
    const int width_px = std::max(viewport()->geometry().width, ceilPx(contentWidth(), s));
    bool created = false;
    for (int i = first; i <= last; ++i) {
        ItemRow*& slot = rows_[i];
        if (!slot) {
            created = true;
            slot = viewport_->addChild<ItemRow>(this, i);
            createRowChildren(slot);
            refreshRowLooks(slot);
        }
        const int top = roundPx(static_cast<float>(i) * rh - static_cast<float>(sy), s);
        const int bottom = roundPx(static_cast<float>(i + 1) * rh - static_cast<float>(sy), s);
        slot->setGeometry({-roundPx(static_cast<float>(sx), s), top, width_px, std::max(1, bottom - top)});
    }
    if (created) viewport_->sortChildren([](const Widget* a, const Widget* b) {
        const auto* ra = dynamic_cast<const ItemRow*>(a);
        const auto* rb = dynamic_cast<const ItemRow*>(b);
        if (ra && rb) return ra->row() < rb->row();
        return ra != nullptr && rb == nullptr;  // rows in row order, then the editor over them
    });
    if (created || removed) notifyStructureChanged();
}

void ItemView::refreshRowLooks(ItemRow* r) {
    r->accessibleName = rowAccessibleName(r->row());
    r->toolTip = rowToolTip(r->row());
    updateRowChildren(r);
    r->update();
}

void ItemView::scrollToRow(int row) {
    if (row < 0 || row >= rowCount() || viewport_h_ <= 0) return;
    const float rh = rowHeight();
    const int top = static_cast<int>(std::floor(static_cast<float>(row) * rh));
    const int bottom = static_cast<int>(std::ceil(static_cast<float>(row + 1) * rh));
    int sy = vbar_->value();
    if (top < sy) sy = top;
    else if (bottom > sy + static_cast<int>(viewport_h_)) sy = bottom - static_cast<int>(viewport_h_);
    vbar_->setValue(sy);
}

// -- model changes ----------------------------------------------------------------------------------------------------

void ItemView::rowsInserted(int first, int count) {
    std::set<int> moved;
    for (int r : selected_) moved.insert(r >= first ? r + count : r);
    selected_ = std::move(moved);
    if (current_ >= first) current_ += count;
    if (anchor_ >= first) anchor_ += count;
    if (editor_) cancelEdit();
    // the rows that stay keep their widgets (a UI Automation client holding one keeps a valid element): rows from `first`
    // on move down by `count`
    std::map<int, ItemRow*> reindexed;
    for (auto& [i, r] : rows_) {
        const int to = i >= first ? i + count : i;
        r->setRow(to);
        reindexed[to] = r;
    }
    rows_ = std::move(reindexed);
    relayout();
    refreshRealizedRows();
}

void ItemView::rowsRemoved(int first, int count) {
    std::set<int> moved;
    for (int r : selected_) {
        if (r >= first + count) moved.insert(r - count);
        else if (r < first) moved.insert(r);
    }
    selected_ = std::move(moved);
    const bool current_removed = current_ >= first && current_ < first + count;
    if (current_ >= first + count) current_ -= count;
    else if (current_ >= first) current_ = std::min(first, rowCount() - 1);  // Qt: the neighbour that took its place
    if (anchor_ >= first + count) anchor_ -= count;
    else if (anchor_ >= first) anchor_ = current_;
    if (editor_) cancelEdit();
    std::map<int, ItemRow*> kept;
    for (auto& [i, r] : rows_) {
        if (i >= first && i < first + count) {
            viewport_->release(r);  // its row is gone
        } else {
            const int to = i >= first + count ? i - count : i;
            r->setRow(to);
            kept[to] = r;
        }
    }
    rows_ = std::move(kept);
    relayout();
    refreshRealizedRows();
    // the current ITEM changed only when its row was the one removed; a row that merely moved up is the same item (Qt)
    if (current_removed && !blocked_ && on_current_row_changed) on_current_row_changed(current_);
    emitSelection();
}

void ItemView::modelReset() {
    const int old_current = current_;
    if (editor_) cancelEdit();
    current_ = -1;
    anchor_ = -1;
    selected_.clear();
    for (auto& [i, r] : rows_) viewport_->release(r);
    rows_.clear();
    vbar_->setValueSilent(0);
    hbar_->setValueSilent(0);
    relayout();
    for (auto& [i, r] : rows_) refreshRowLooks(r);
    if (old_current != -1 && !blocked_ && on_current_row_changed) on_current_row_changed(-1);
    emitSelection();
}

void ItemView::rowChanged(int row) {
    if (ItemRow* r = realizedRow(row)) refreshRowLooks(r);
    update();
}

// -- current row and selection ----------------------------------------------------------------------------------------

void ItemView::emitSelection() {
    for (auto& [i, r] : rows_) r->update();
    update();
    if (!blocked_ && on_selection_changed) on_selection_changed();
    notifyCurrentChanged();
}

std::vector<int> ItemView::selectedRows() const {
    if (mode_ == SelectionMode::Single) return current_ >= 0 ? std::vector<int>{current_} : std::vector<int>{};
    return {selected_.begin(), selected_.end()};
}

void ItemView::setSelectionMode(SelectionMode m) {
    if (m == mode_) return;
    mode_ = m;
    selected_.clear();
    if (m != SelectionMode::None && current_ >= 0) selected_.insert(current_);
    emitSelection();
}

void ItemView::setCurrentInternal(int row, bool notify, bool select) {
    const int n = rowCount();
    if (row < -1 || row >= n) row = -1;
    if (row >= 0 && !rowEnabled(row)) return;
    const bool changed = row != current_;
    current_ = row;
    if (changed) currentChanged(row);
    if (select && mode_ != SelectionMode::None) {
        selected_.clear();
        if (row >= 0) selected_.insert(row);
        anchor_ = row;
    }
    if (row >= 0) scrollToRow(row);
    if (changed && notify && !blocked_ && on_current_row_changed) on_current_row_changed(row);
    emitSelection();
}

void ItemView::setCurrentRow(int row) { setCurrentInternal(row, true, true); }
void ItemView::setCurrentRowSilent(int row) { setCurrentInternal(row, false, true); }

void ItemView::selectRow(int row, bool on) {
    if (mode_ != SelectionMode::Extended || row < 0 || row >= rowCount() || !rowEnabled(row)) return;
    if (on) selected_.insert(row);
    else selected_.erase(row);
    emitSelection();
}

void ItemView::selectRange(int from, int to) {
    if (mode_ != SelectionMode::Extended) return;
    if (from > to) std::swap(from, to);
    selected_.clear();
    for (int i = std::max(0, from); i <= std::min(to, rowCount() - 1); ++i)
        if (rowEnabled(i)) selected_.insert(i);
    emitSelection();
}

void ItemView::selectAll() {
    if (mode_ == SelectionMode::Extended) selectRange(0, rowCount() - 1);
}

void ItemView::clearSelection() {
    selected_.clear();
    emitSelection();
}

void ItemView::setCurrentAndSelect(int row, bool extend, bool toggle) {
    if (!rowSelectable(row)) return;
    if (mode_ == SelectionMode::Single || !(extend || toggle)) {
        setCurrentInternal(row, true, true);
    } else if (extend) {
        const int from = anchor_ >= 0 ? anchor_ : (current_ >= 0 ? current_ : row);
        const bool changed = row != current_;
        selectRange(from, row);
        current_ = row;
        if (changed) currentChanged(row);
        anchor_ = from;
        scrollToRow(row);
        if (changed && !blocked_ && on_current_row_changed) on_current_row_changed(row);
    } else {
        const bool changed = row != current_;
        if (selected_.count(row)) selected_.erase(row);
        else selected_.insert(row);
        current_ = row;
        if (changed) currentChanged(row);
        anchor_ = row;
        scrollToRow(row);
        if (changed && !blocked_ && on_current_row_changed) on_current_row_changed(row);
        emitSelection();
    }
}

int ItemView::step(int from, int dir) const {
    const int n = rowCount();
    for (int i = (from < 0 ? (dir > 0 ? 0 : n - 1) - dir : from) + dir; i >= 0 && i < n; i += dir)
        if (rowEnabled(i)) return i;
    return -1;
}

// -- painting ---------------------------------------------------------------------------------------------------------

void ItemView::paint(Painter& p) {
    const SizeF s = sizeDips();
    p.fillRect({0, 0, s.width, s.height}, token(T::Base));
    const auto border = p.crisp({0, 0, s.width, s.height}, kBorder);
    p.strokeRect(border.rect, token(hasFocus() ? T::Focus : T::BorderStrong), border.width);
    if (vbar_->isVisibleSelf() && hbar_->isVisibleSelf()) {  // the corner where the two bars meet
        const double k = scale();
        p.fillRect({static_cast<float>(vbar_->geometry().x / k), static_cast<float>(hbar_->geometry().y / k),
                    static_cast<float>(vbar_->geometry().width / k), static_cast<float>(hbar_->geometry().height / k)},
                   token(T::AlternateBase));
    }
}

void ItemView::paintRowFor(Painter& p, int row, const SizeF& size, bool hovered) {
    RowState st;
    st.selected = isRowSelected(row) && mode_ != SelectionMode::None;
    st.current = row == current_;
    st.enabled = rowEnabled(row);
    st.hovered = hovered;
    st.focused = hasFocus();
    const bool hc = highContrast().on;
    st.text = token(!st.enabled ? T::TextFaint : (st.selected && st.focused && hc) ? T::OnAccent : T::Text);
    if (st.selected && rowFillsSelection()) fillSelection(p, {{0, 0, size.width, size.height}}, st.focused);
    paintRowContent(p, row, size, st);
    if (st.current && st.focused && rowFocusFrame()) {
        const auto c = p.crisp({0, 0, size.width, size.height}, 1.0f);
        p.strokeRect(c.rect, token(T::Focus), c.width);
    }
}

void ItemView::focusChanged(bool, FocusReason) {
    for (auto& [i, r] : rows_) r->update();
    update();
}

// -- mouse ------------------------------------------------------------------------------------------------------------

bool ItemView::rowMouse(int row, const UiMouseEvent& e) {
    if (e.type == MouseType::Wheel) return mouseEvent(e);
    const PointF vl = mapFromWindow(e.window_pos);
    const bool left = e.button == MouseButton::Left;
    const bool right = e.button == MouseButton::Right;
    const bool shift = any(e.mods & Mod::Shift), ctrl = any(e.mods & Mod::Ctrl);
    switch (e.type) {
        case MouseType::Down:
        case MouseType::DoubleClick:
            if (!left && !right) return false;
            if (!rowEnabled(row)) return true;
            if (left && e.type == MouseType::Down && rowPressed(row, e, vl.x)) return true;  // (a double click's second press is not a new press)
            if (e.type == MouseType::DoubleClick && left) {
                setCurrentAndSelect(row, false, false);
                rowDoubleClicked(row, vl.x);
                return true;
            }
            if (right && isRowSelected(row)) {  // a right press inside the selection keeps it (for a context menu)
                if (current_ != row) setCurrentInternal(row, true, false);
                return true;
            }
            setCurrentAndSelect(row, left && shift, left && ctrl);
            return true;
        case MouseType::Move: {
            if (!(e.buttons & (1u << static_cast<int>(MouseButton::Left)))) return false;
            int r = rowAt({vl.x < 1 ? 1.0f : vl.x, vl.y});
            if (r < 0) {  // above or below the rows: the nearest end
                const RectF v = viewportRect();
                r = vl.y < v.y ? firstVisibleRow() : lastVisibleRow();
                if (vl.y >= v.bottom() && rowCount() > 0) r = std::min(rowCount() - 1, lastVisibleRow() + 1);
                if (vl.y < v.y) r = std::max(0, firstVisibleRow() - 1);
            }
            if (r >= 0 && rowDragged(r, vl.x)) return true;
            if (r < 0 || r == current_ || !rowSelectable(r)) return true;
            if (mode_ == SelectionMode::Extended) setCurrentAndSelect(r, true, false);
            else setCurrentInternal(r, true, true);
            return true;
        }
        case MouseType::Up: return left || right;
        default: return false;
    }
}

bool ItemView::mouseEvent(const UiMouseEvent& e) {
    if (e.type == MouseType::Wheel) {
        if (!vbar_->isNeeded() || e.wheel_steps == 0) return false;
        int d = static_cast<int>(std::lround(e.wheel_steps * 3 * vbar_->singleStep()));
        if (d == 0) d = e.wheel_steps > 0 ? 1 : -1;
        vbar_->setValue(vbar_->value() - d);  // away from the user = up
        return true;
    }
    return e.type == MouseType::Down || e.type == MouseType::Up;  // a press on the empty part still takes the focus
}

// -- keys -------------------------------------------------------------------------------------------------------------

bool ItemView::overridesShortcut(const KeyEvent& e) const {
    if (e.mods == Mod::Ctrl) return e.vk == 'A' || e.vk == 'C' || e.vk == keys::Insert;
    return false;
}

bool ItemView::keyEvent(const KeyEvent& e) {
    if (!e.down || editor_) return false;
    const bool ctrl = any(e.mods & Mod::Ctrl), shift = any(e.mods & Mod::Shift);
    if (any(e.mods & Mod::Alt)) return false;
    if (current_ >= 0 && rowKey(current_, e)) return true;
    auto move = [&](int target) {
        if (target < 0) return;
        if (ctrl && !shift) setCurrentInternal(target, true, false);  // the current row alone moves
        else setCurrentAndSelect(target, shift, false);
    };
    switch (e.vk) {
        case keys::Up: move(step(current_, -1)); return true;
        case keys::Down: move(step(current_, +1)); return true;
        case keys::Home: move(step(-1, +1)); return true;
        case keys::End: move(step(rowCount(), -1)); return true;
        case keys::PageUp:
        case keys::PageDown: {
            const int rows = std::max(1, static_cast<int>(viewport_h_ / rowHeight()) - 1);
            const int dir = e.vk == keys::PageDown ? 1 : -1;
            int to = std::clamp((current_ < 0 ? (dir > 0 ? 0 : rowCount() - 1) : current_ + dir * rows), 0, rowCount() - 1);
            if (rowCount() == 0) return true;
            if (!rowEnabled(to)) {
                const int s2 = step(to, dir);
                to = s2 >= 0 ? s2 : step(to, -dir);
            }
            move(to);
            return true;
        }
        case keys::Return:
            if (current_ >= 0 && !blocked_ && on_row_activated) on_row_activated(current_);
            return current_ >= 0;
        case keys::F2:
            if (current_ >= 0 && canEdit(current_)) {
                startEditing(current_);
                return true;
            }
            return false;
        case keys::Space:
            if (ctrl && mode_ == SelectionMode::Extended && current_ >= 0) {
                setCurrentAndSelect(current_, false, true);
                return true;
            }
            return false;
        default: break;
    }
    if (ctrl && e.vk == 'A' && mode_ == SelectionMode::Extended) return selectAll(), true;
    if (ctrl && (e.vk == 'C' || e.vk == keys::Insert)) return copy(), true;
    return false;
}

bool ItemView::charEvent(char32_t c) {
    if (editor_ || c < 0x20 || c == 0x7F || c > 0x7E) return false;
    if (c == U' ' && typed_.empty()) return false;
    typeahead(std::string(1, static_cast<char>(c)));
    return true;
}

void ItemView::typeahead(const std::string& typed) {
    auto fold = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        return s;
    };
    typed_ += typed;
    if (typed_timer_) stopTimer(typed_timer_);
    typed_timer_ = startTimer(kTypeaheadMs, false, [this] {
        typed_.clear();
        typed_timer_ = 0;
    });
    const int n = rowCount();
    if (n == 0) return;
    const bool cycle = typed_.size() >= 2 && typed_.find_first_not_of(typed_[0]) == std::string::npos;  // "aa": the same letter again
    const std::string prefix = fold(cycle ? typed_.substr(0, 1) : typed_);
    const int start = typed_.size() == 1 || cycle ? current_ + 1 : std::max(current_, 0);
    for (int k = 0; k < n; ++k) {
        const int i = (start + k) % n;
        if (rowEnabled(i) && fold(rowText(i)).rfind(prefix, 0) == 0) {
            setCurrentAndSelect(i, false, false);
            return;
        }
    }
}

// -- copy -------------------------------------------------------------------------------------------------------------

std::string ItemView::copyTextFor(const std::vector<int>& rows) const {
    std::string out;
    for (int r : rows) out += (out.empty() ? "" : "\n") + rowText(r);
    return out;
}

const std::string& ItemView::copyText() {
    copy_text_ = copyTextFor(selectedRows());
    return copy_text_;
}

bool ItemView::copy() {
    if (!hasSelection()) return false;
    Clipboard* cb = host() ? host()->clipboard() : nullptr;
    if (!cb) return false;
    cb->setText(copyText());
    return true;
}

// -- editing ----------------------------------------------------------------------------------------------------------

void ItemView::beginEdit(int row, int col, const RectF& cell, const std::string& text, bool select_all) {
    if (!host() || row < 0 || row >= rowCount()) return;
    if (editor_) cancelEdit();
    graveyard_.clear();
    std::unique_ptr<InlineEditor> ed = host()->createInlineEditor();
    if (!ed) return;
    scrollToRow(row);
    ItemRow* r = realizedRow(row);
    if (!r) return;
    const double s = scale();
    editor_ = ed.get();
    edit_row_ = row;
    edit_col_ = col;
    editor_->on_finished = [this](bool commit) { finishEdit(commit); };
    editor_->on_tab = [this](bool forward) {
        finishEdit(true);  // the editor reports Tab instead of finishing: commit, then the subclass goes on to the next cell
        editTab(forward);
    };
    viewport_->adopt(std::move(ed));
    const RectI g = r->geometry();
    editor_->setGeometry({g.x + roundPx(cell.x, s), g.y + roundPx(cell.y, s), ceilPx(cell.width, s), ceilPx(cell.height, s)});
    editor_->setText(text);
    if (select_all) editor_->selectAll();
    editor_->setFocus(FocusReason::Other);
}

void ItemView::finishEdit(bool commit) {
    if (!editor_) return;
    const std::string text = editor_->text();
    const bool had_focus = editor_->hasFocus();
    const int row = edit_row_, col = edit_col_;
    editor_->on_finished = nullptr;
    editor_->on_tab = nullptr;
    editor_->hide();
    graveyard_.push_back(viewport_->release(editor_));  // it may be inside its own handler right now: released later
    editor_ = nullptr;
    if (had_focus) setFocus(FocusReason::Other);
    if (commit) commitEdit(row, col, text);
}

void ItemView::cancelEdit() { finishEdit(false); }

// -- accessibility ----------------------------------------------------------------------------------------------------

std::vector<Widget*> ItemView::accessibleSelection() const {
    std::vector<Widget*> out;
    for (int r : selectedRows())
        if (ItemRow* w = realizedRow(r)) out.push_back(w);
    return out;
}

void ItemView::accessibleSelectRow(int row) {
    if (!rowSelectable(row)) return;
    setCurrentInternal(row, true, true);
    announce(rowAccessibleName(row));
}

bool ItemView::accessibleAddRow(int row) {
    if (!rowSelectable(row)) return false;
    if (mode_ == SelectionMode::Extended) {
        selectRow(row, true);
        return true;
    }
    if (current_ >= 0) return row == current_;  // a single-selection list already has its one
    setCurrentInternal(row, true, true);
    return true;
}

bool ItemView::accessibleRemoveRow(int row) {
    if (mode_ != SelectionMode::Extended || !isRowSelected(row)) return mode_ == SelectionMode::Extended;
    selectRow(row, false);
    return true;
}

AccessibleScroll ItemView::accessibleScroll() const {
    AccessibleScroll a;
    a.valid = true;
    a.vertical = vbar_->isNeeded();
    a.horizontal = hbar_->isNeeded();
    a.v_percent = a.vertical ? 100.0 * vbar_->value() / vbar_->maximum() : 0;
    a.h_percent = a.horizontal ? 100.0 * hbar_->value() / hbar_->maximum() : 0;
    const float ch = contentHeight();
    a.v_view = ch > 0 ? std::min(100.0, 100.0 * viewport_h_ / ch) : 100;
    a.h_view = contentWidth() > 0 ? std::min(100.0, 100.0 * viewport_w_ / contentWidth()) : 100;
    return a;
}

void ItemView::accessibleScrollBy(int h, int v) {
    auto by = [](ScrollBar* b, int amount) {
        if (amount == 0 || !b->isNeeded()) return;
        const int d = (amount == 1 || amount == -1) ? b->singleStep() : b->pageStep();
        b->setValue(b->value() + (amount > 0 ? d : -d));
    };
    by(hbar_, h);
    by(vbar_, v);
}

void ItemView::accessibleSetScrollPercent(double h, double v) {
    if (h >= 0 && hbar_->isNeeded()) hbar_->setValue(static_cast<int>(std::lround(std::clamp(h, 0.0, 100.0) / 100.0 * hbar_->maximum())));
    if (v >= 0 && vbar_->isNeeded()) vbar_->setValue(static_cast<int>(std::lround(std::clamp(v, 0.0, 100.0) / 100.0 * vbar_->maximum())));
}

}  // namespace tcad::ui

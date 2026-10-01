#include "ui/widgets/table_view.hpp"

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

namespace {

std::string utf8Of(char32_t c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) s += static_cast<char>(0xC0 | (c >> 6)), s += static_cast<char>(0x80 | (c & 0x3F));
    else s += static_cast<char>(0xE0 | (c >> 12)), s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), s += static_cast<char>(0x80 | (c & 0x3F));
    return s;
}

}  // namespace

// -- a cell's accessible widget -------------------------------------------------------------------------------------------

int CellWidget::accessibleSelectionState() const { return table_->cellSelectionState(row_, col_); }
void CellWidget::accessibleSelect() { table_->cellSelect(row_, col_); }
Widget* CellWidget::accessibleSelectionContainer() const { return table_; }

AccessibleCell CellWidget::accessibleCell() const {
    AccessibleCell c;
    c.valid = true;
    c.row = row_;
    c.column = col_;
    c.grid = table_;
    return c;
}

void CellWidget::accessibleScrollIntoView() { table_->scrollToRow(row_); }

// -- the table ----------------------------------------------------------------------------------------------------------

TableView::TableView() {
    band_ = addChild<ColumnHeaderBand>(static_cast<ColumnModel*>(this));
    band_->name = "header";
    setSelectionMode(SelectionMode::Extended);
}

const std::string& TableView::cellText(int row, int col) const {
    static const std::string kNone;
    return validCell(row, col) ? cells_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].text : kNone;
}

bool TableView::cellEditable(int row, int col) const {
    return validCell(row, col) && cells_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].editable;
}

void TableView::shapeChanged() {
    widths_.resize(static_cast<std::size_t>(cols_), kDefaultColumn);
    headers_.resize(static_cast<std::size_t>(cols_));
    for (auto& r : cells_) r.resize(static_cast<std::size_t>(cols_));
    if (cur_col_ >= cols_) cur_col_ = cols_ - 1;
    band_->sync();
    relayout();
    band_->sync();
    refreshRealizedRows();
}

void TableView::setColumnCount(int n) {
    n = std::max(0, n);
    if (n == cols_) return;
    cols_ = n;
    shapeChanged();
}

void TableView::setRowCount(int n) {
    n = std::max(0, n);
    if (n == rows_) return;
    const int old = rows_;
    cells_.resize(static_cast<std::size_t>(n), std::vector<Cell>(static_cast<std::size_t>(cols_)));
    rows_ = n;
    if (n > old) rowsInserted(old, n - old);
    else rowsRemoved(n, old - n);
    band_->sync();  // the gutter's width follows the digits
    refreshRealizedRows();
}

void TableView::insertRow(int row) {
    row = std::clamp(row, 0, rows_);
    cells_.insert(cells_.begin() + row, std::vector<Cell>(static_cast<std::size_t>(cols_)));
    ++rows_;
    rowsInserted(row, 1);
    band_->sync();
    refreshRealizedRows();
}

void TableView::removeRow(int row) {
    if (row < 0 || row >= rows_) return;
    cells_.erase(cells_.begin() + row);
    --rows_;
    rowsRemoved(row, 1);
    band_->sync();
    refreshRealizedRows();
}

void TableView::clearContents() {
    for (auto& r : cells_)
        for (auto& c : r) c = Cell{};
    refreshRealizedRows();
}

void TableView::setItem(int row, int col, std::string text) {
    if (!validCell(row, col)) return;
    Cell& c = cells_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
    const bool changed = c.text != text;
    c.text = std::move(text);
    rowChanged(row);
    if (changed && !signalsBlocked() && on_cell_changed) on_cell_changed(row, col);
}

void TableView::setCellToolTip(int row, int col, std::string tip) {
    if (!validCell(row, col)) return;
    cells_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].tooltip = std::move(tip);
    rowChanged(row);
}

void TableView::setCellEditable(int row, int col, bool on) {
    if (validCell(row, col)) cells_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].editable = on;
}

// -- headers and columns ------------------------------------------------------------------------------------------------

void TableView::setHeaderLabels(std::vector<std::string> labels) {
    labels.resize(static_cast<std::size_t>(cols_));
    headers_ = std::move(labels);
    band_->sync();
}

std::string TableView::headerLabel(int col) const {
    return col >= 0 && col < static_cast<int>(headers_.size()) ? headers_[static_cast<std::size_t>(col)] : std::string();
}

void TableView::setHeaderVisible(bool on) {
    if (on == header_visible_) return;
    header_visible_ = on;
    band_->setVisible(on);
    relayout();
}

void TableView::setRowHeadersVisible(bool on) {
    if (on == row_headers_) return;
    row_headers_ = on;
    contentChangedWidth();
    band_->sync();
    refreshRealizedRows();
}

float TableView::gutterWidth() const {
    if (!row_headers_) return 0;
    TextEngine* te = textEngine();
    const float digits = te ? te->measure(std::to_string(std::max(rows_, 1)), TextStyle{}).width : 12.0f;
    return std::max(30.0f, std::ceil(digits + 14.0f));
}

float TableView::baseWidth(int col) const { return col >= 0 && col < static_cast<int>(widths_.size()) ? widths_[static_cast<std::size_t>(col)] : 0.0f; }

float TableView::columnX(int col) const {
    float x = 0;
    for (int i = 0; i < col && i < cols_; ++i) x += baseWidth(i);  // the last column is the only stretched one: never before another
    return x;
}

float TableView::columnWidth(int col) const {
    if (col < 0 || col >= cols_) return 0;
    float w = baseWidth(col);
    if (stretch_last_ && col == cols_ - 1) w = std::max(w, viewportRect().width - gutterWidth() - columnX(col));
    return w;
}

float TableView::contentWidth() const {
    float w = gutterWidth();
    for (int i = 0; i < cols_; ++i) w += baseWidth(i);
    return w;
}

void TableView::setColumnWidth(int col, float dips) {
    if (col < 0 || col >= cols_) return;
    widths_[static_cast<std::size_t>(col)] = std::max(kMinColumn, dips);
    contentChangedWidth();
    band_->sync();
    refreshRealizedRows();
}

void TableView::setStretchLastColumn(bool on) {
    stretch_last_ = on;
    band_->sync();
    refreshRealizedRows();
    update();
}

void TableView::resizeColumnToContents(int col) {
    if (col < 0 || col >= cols_) return;
    TextEngine* te = textEngine();
    if (!te) return;
    TextStyle st;
    float w = te->measure(headerLabel(col), st).width;
    for (int r = 0; r < rows_; ++r) w = std::max(w, te->measure(cellText(r, col), st).width);
    setColumnWidth(col, std::ceil(w + 2 * kCellPad));
}

void TableView::resizeColumnsToContents() {
    for (int c = 0; c < cols_; ++c) {
        TextEngine* te = textEngine();
        if (!te) return;
        TextStyle st;
        float w = te->measure(headerLabel(c), st).width;
        for (int r = 0; r < rows_; ++r) w = std::max(w, te->measure(cellText(r, c), st).width);
        widths_[static_cast<std::size_t>(c)] = std::max(kMinColumn, std::ceil(w + 2 * kCellPad));
    }
    contentChangedWidth();
    band_->sync();
    refreshRealizedRows();
}

int TableView::columnAt(float view_x) const {
    const float rx = view_x - (viewportRect().x - scrollOffsetX()) - gutterWidth();
    if (rx < 0) return -1;
    for (int c = 0; c < cols_; ++c)
        if (rx < columnX(c) + columnWidth(c)) return c;
    return -1;
}

RectF TableView::cellRectInRow(int col) const { return {gutterWidth() + columnX(col), 0, columnWidth(col), rowHeight()}; }

void TableView::layoutHeader(const RectI& px) {
    band_->setGeometry(px);
    band_->sync();
}

// -- selection ------------------------------------------------------------------------------------------------------------

void TableView::setSelectionBehavior(SelectionBehavior b) {
    if (b == behavior_) return;
    behavior_ = b;
    repaintRows();
}

TableView::Block TableView::selectedBlock() const {
    Block b;
    if (behavior_ != SelectionBehavior::Cells || currentRow() < 0 || cur_col_ < 0 || anchor_row_ < 0 || anchor_col_ < 0) return b;
    b.row0 = std::min(anchor_row_, currentRow());
    b.row1 = std::max(anchor_row_, currentRow());
    b.col0 = std::min(anchor_col_, cur_col_);
    b.col1 = std::max(anchor_col_, cur_col_);
    return b;
}

bool TableView::isCellSelected(int row, int col) const {
    if (behavior_ == SelectionBehavior::Rows) return isRowSelected(row);
    const Block b = selectedBlock();
    return b.valid() && row >= b.row0 && row <= b.row1 && col >= b.col0 && col <= b.col1;
}

bool TableView::hasSelection() const { return behavior_ == SelectionBehavior::Cells ? selectedBlock().valid() : !selectedRows().empty(); }

int TableView::rowSelectionState(int row) const { return behavior_ == SelectionBehavior::Rows ? ItemView::rowSelectionState(row) : -1; }

int TableView::cellSelectionState(int row, int col) const {
    if (behavior_ != SelectionBehavior::Cells || !validCell(row, col)) return -1;
    return isCellSelected(row, col) ? 1 : 0;
}

void TableView::cellSelect(int row, int col) { setCurrentCell(row, col); }

void TableView::currentChanged(int row) {
    if (row >= 0 && cur_col_ < 0 && cols_ > 0) cur_col_ = 0;
    if (row < 0) cur_col_ = -1;
    if (behavior_ == SelectionBehavior::Cells) {
        anchor_row_ = row;
        anchor_col_ = row < 0 ? -1 : std::max(cur_col_, 0);
    }
}

void TableView::setCurrentCell(int row, int col) {
    if (row < 0 || col < 0) {
        setCurrentRow(-1);
        return;
    }
    if (!validCell(row, col)) return;
    setCurrentCellInternal(row, col, true);
}

void TableView::setCurrentCellInternal(int row, int col, bool select) {
    const int old_col = cur_col_;
    cur_col_ = col;
    if (behavior_ == SelectionBehavior::Cells && select) {
        anchor_row_ = row;
        anchor_col_ = col;
    }
    const int old_row = currentRow();
    setCurrentInternal(row, true, behavior_ == SelectionBehavior::Rows && select);
    if (behavior_ == SelectionBehavior::Cells && select) {
        anchor_row_ = row;  // (currentChanged set them from the stale column when the row changed)
        anchor_col_ = col;
    }
    // keep the column on screen
    if (horizontalScrollBar()->isNeeded()) {
        const float left = gutterWidth() + columnX(col), right = left + columnWidth(col);
        int sx = horizontalScrollBar()->value();
        const float view_w = viewportRect().width;
        if (left < static_cast<float>(sx) + gutterWidth()) sx = static_cast<int>(std::floor(left - gutterWidth()));
        else if (right > static_cast<float>(sx) + view_w) sx = static_cast<int>(std::ceil(right - view_w));
        horizontalScrollBar()->setValue(std::max(0, sx));
    }
    if ((row != old_row || col != old_col) && !signalsBlocked() && on_current_cell_changed) on_current_cell_changed(row, col);
    repaintRows();
}

void TableView::moveCurrentCell(int row, int col, bool extend) {
    if (rows_ == 0 || cols_ == 0) return;
    row = std::clamp(row, 0, rows_ - 1);
    col = std::clamp(col, 0, cols_ - 1);
    if (behavior_ == SelectionBehavior::Cells && extend) {
        const int ar = anchor_row_ >= 0 ? anchor_row_ : std::max(currentRow(), 0);
        const int ac = anchor_col_ >= 0 ? anchor_col_ : std::max(cur_col_, 0);
        setCurrentCellInternal(row, col, false);
        anchor_row_ = ar;
        anchor_col_ = ac;
        repaintRows();
    } else {
        setCurrentCellInternal(row, col, true);
    }
}

bool TableView::rowPressed(int row, const UiMouseEvent& e, float x) {
    const int col = columnAt(x);
    const bool shift = any(e.mods & Mod::Shift);
    if (behavior_ == SelectionBehavior::Rows) {
        if (col >= 0) cur_col_ = col;
        return false;  // the engine selects the rows
    }
    if (col < 0) {  // the row-number gutter selects the whole row
        setCurrentCellInternal(row, 0, true);
        anchor_row_ = row;
        anchor_col_ = 0;
        cur_col_ = cols_ - 1;
        repaintRows();
        return true;
    }
    moveCurrentCell(row, col, shift);
    return true;
}

bool TableView::rowDragged(int row, float x) {
    if (behavior_ != SelectionBehavior::Cells) return false;
    int col = columnAt(x);
    if (col < 0) col = x < viewportRect().x + gutterWidth() ? 0 : cols_ - 1;
    if (row == currentRow() && col == cur_col_) return true;
    moveCurrentCell(row, col, true);
    return true;
}

void TableView::rowDoubleClicked(int row, float x) {
    const int col = columnAt(x);
    if (col >= 0) {
        if (behavior_ == SelectionBehavior::Cells) setCurrentCellInternal(row, col, true);
        else cur_col_ = col;
    }
    if (col >= 0 && canEdit(row)) {
        startEditing(row);
        return;
    }
    if (col >= 0 && !signalsBlocked() && on_cell_double_clicked) on_cell_double_clicked(row, col);
    ItemView::rowDoubleClicked(row, x);
}

bool TableView::wantsTabKey(bool forward) const {
    if (behavior_ != SelectionBehavior::Cells || currentRow() < 0 || cur_col_ < 0 || rows_ == 0 || cols_ == 0) return false;
    if (forward) return !(currentRow() == rows_ - 1 && cur_col_ == cols_ - 1);
    return !(currentRow() == 0 && cur_col_ == 0);
}

bool TableView::rowKey(int row, const KeyEvent& e) {
    const bool ctrl = any(e.mods & Mod::Ctrl), shift = any(e.mods & Mod::Shift);
    if (behavior_ == SelectionBehavior::Rows) {
        if (e.vk == keys::Left || e.vk == keys::Right) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (e.vk == keys::Right ? 1 : -1) * horizontalScrollBar()->singleStep());
            return true;
        }
        return false;
    }
    const int col = std::max(cur_col_, 0);
    const int page = std::max(1, static_cast<int>(viewportRect().height / rowHeight()) - 1);
    switch (e.vk) {
        case keys::Left: moveCurrentCell(row, col - 1, shift); return true;
        case keys::Right: moveCurrentCell(row, col + 1, shift); return true;
        case keys::Up: moveCurrentCell(row - 1, col, shift); return true;
        case keys::Down: moveCurrentCell(row + 1, col, shift); return true;
        case keys::PageUp: moveCurrentCell(row - page, col, shift); return true;
        case keys::PageDown: moveCurrentCell(row + page, col, shift); return true;
        case keys::Home: moveCurrentCell(ctrl ? 0 : row, 0, shift); return true;
        case keys::End: moveCurrentCell(ctrl ? rows_ - 1 : row, cols_ - 1, shift); return true;
        case keys::Tab:
            if (ctrl) return false;
            if (shift) {
                if (col > 0) moveCurrentCell(row, col - 1, false);
                else if (row > 0) moveCurrentCell(row - 1, cols_ - 1, false);
                else return false;
            } else {
                if (col < cols_ - 1) moveCurrentCell(row, col + 1, false);
                else if (row < rows_ - 1) moveCurrentCell(row + 1, 0, false);
                else return false;
            }
            return true;
        default: break;
    }
    if (ctrl && e.vk == 'A') {
        anchor_row_ = 0;
        anchor_col_ = 0;
        setCurrentCellInternal(rows_ - 1, cols_ - 1, false);
        anchor_row_ = 0;
        anchor_col_ = 0;
        repaintRows();
        return true;
    }
    return false;
}

void TableView::focusChanged(bool in, FocusReason why) {
    ItemView::focusChanged(in, why);
    if (in && (why == FocusReason::Tab || why == FocusReason::Backtab) && currentRow() < 0 && rows_ > 0 && cols_ > 0)
        setCurrentCellInternal(why == FocusReason::Tab ? 0 : rows_ - 1, why == FocusReason::Tab ? 0 : cols_ - 1, true);  // Qt: a table entered by Tab has a current cell
}

// -- painting -----------------------------------------------------------------------------------------------------------

void TableView::paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) {
    const float gw = gutterWidth();
    TextStyle ts;
    ts.valign = VAlign::Center;
    const bool hc = highContrast().on;
    for (int c = 0; c < cols_; ++c) {
        const RectF r = cellRectInRow(c);
        if (r.right() < 0 || r.x > size.width) continue;
        const bool cell_sel = behavior_ == SelectionBehavior::Cells && isCellSelected(row, c);
        if (cell_sel) fillSelection(p, {r}, st.focused);
        ts.color = (cell_sel && st.focused && hc) || (behavior_ == SelectionBehavior::Rows && st.selected && st.focused && hc) ? token(T::OnAccent) : token(T::Text);
        p.drawText({r.x + kCellPad, 0, std::max(0.0f, r.width - 2 * kCellPad), size.height}, cellText(row, c), ts);
        p.fillRect({r.right() - 1, 0, 1, size.height}, token(T::Border));  // the grid
        if (behavior_ == SelectionBehavior::Cells && st.current && c == cur_col_ && st.focused) {
            const auto f = p.crisp(r, 1.0f);
            p.strokeRect(f.rect, token(T::Focus), f.width);
        }
    }
    p.fillRect({gw, size.height - 1, std::max(0.0f, size.width - gw), 1}, token(T::Border));
    if (gw > 0) {  // the row number
        p.fillRect({0, 0, gw, size.height}, token(T::Window));
        p.fillRect({gw - 1, 0, 1, size.height}, token(T::Border));
        p.fillRect({0, size.height - 1, gw, 1}, token(T::Border));
        TextStyle ns;
        ns.color = token(T::TextDim);
        ns.halign = HAlign::Center;
        ns.valign = VAlign::Center;
        p.drawText({0, 0, gw - 1, size.height}, std::to_string(row + 1), ns);
    }
}

// -- row children -------------------------------------------------------------------------------------------------------

void TableView::updateRowChildren(ItemRow* r) {
    const double s = scale();
    while (static_cast<int>(r->children().size()) > cols_) r->release(r->children().back().get());
    while (static_cast<int>(r->children().size()) < cols_) r->addChild<CellWidget>(this, r->row(), static_cast<int>(r->children().size()));
    const int h = std::max(1, r->geometry().height);
    int c = 0;
    for (auto& ch : r->children()) {
        auto* cw = static_cast<CellWidget*>(ch.get());
        cw->set(r->row(), c);
        cw->accessibleName = cellText(r->row(), c);
        cw->toolTip = validCell(r->row(), c) ? cells_[static_cast<std::size_t>(r->row())][static_cast<std::size_t>(c)].tooltip : std::string();
        const int x0 = roundPx(gutterWidth() + columnX(c), s), x1 = roundPx(gutterWidth() + columnX(c) + columnWidth(c), s);
        cw->setGeometry({x0, 0, std::max(1, x1 - x0), h});
        ++c;
    }
}

std::string TableView::rowName(int row) const {
    std::string s;
    for (int c = 0; c < cols_; ++c) {
        if (cellText(row, c).empty()) continue;
        s += (s.empty() ? "" : ", ") + cellText(row, c);
    }
    return s;
}

AccessibleCell TableView::rowCell(int) const { return {}; }

// -- editing ------------------------------------------------------------------------------------------------------------

bool TableView::canEdit(int row) const {
    return edit_triggers_ && host() && host()->canCreateInlineEditor() && validCell(row, cur_col_) && cellEditable(row, cur_col_);
}

void TableView::beginCellEdit(int row, int col, const std::string& text, bool select_all) {
    beginEdit(row, col, cellRectInRow(col), text, select_all);
}

void TableView::startEditing(int row) {
    if (canEdit(row)) beginCellEdit(row, cur_col_, cellText(row, cur_col_), true);
}

void TableView::editCell(int row, int col) {
    if (!validCell(row, col)) return;
    setCurrentCellInternal(row, col, true);
    startEditing(row);
}

bool TableView::charEvent(char32_t c) {
    if (isEditing() || c < 0x20 || c == 0x7F || currentRow() < 0 || !canEdit(currentRow())) return false;
    beginCellEdit(currentRow(), cur_col_, utf8Of(c), false);  // AnyKeyPressed: the character replaces the text
    return true;
}

void TableView::commitEdit(int row, int col, const std::string& text) {
    if (!validCell(row, col) || cellText(row, col) == text) return;
    cells_[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].text = text;
    rowChanged(row);
    if (!signalsBlocked() && on_cell_changed) on_cell_changed(row, col);
}

void TableView::editTab(bool forward) {
    int r = currentRow(), c = cur_col_;
    for (int guard = 0; guard < rows_ * cols_; ++guard) {
        if (forward) {
            if (++c >= cols_) c = 0, ++r;
        } else {
            if (--c < 0) c = cols_ - 1, --r;
        }
        if (r < 0 || r >= rows_) return;  // past the ends: editing stops
        if (cellEditable(r, c)) break;
    }
    setCurrentCellInternal(r, c, true);
    startEditing(r);
}

// -- copy ---------------------------------------------------------------------------------------------------------------

std::string TableView::cellsText(int r0, int c0, int r1, int c1) const {
    std::string out;
    for (int r = r0; r <= r1; ++r) {
        if (r > r0) out += '\n';
        for (int c = c0; c <= c1; ++c) {
            if (c > c0) out += '\t';
            std::string t = cellText(r, c);
            for (char& ch : t)
                if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';  // a cell holds one line, so the text stays a grid
            out += t;
        }
    }
    return out;
}

std::string TableView::copyTextFor(const std::vector<int>& rows) const {
    if (behavior_ == SelectionBehavior::Cells) {
        const Block b = selectedBlock();
        return b.valid() ? cellsText(b.row0, b.col0, b.row1, b.col1) : std::string();
    }
    std::string out;
    for (int r : rows) out += (out.empty() ? "" : "\n") + cellsText(r, 0, r, cols_ - 1);
    return out;
}

// -- accessibility ------------------------------------------------------------------------------------------------------

Widget* TableView::accessibleGridCell(int row, int col) const {
    if (!validCell(row, col)) return nullptr;
    auto* self = const_cast<TableView*>(this);
    self->scrollToRow(row);  // the row must exist as a widget: bring it into view
    ItemRow* r = realizedRow(row);
    if (!r || col >= static_cast<int>(r->children().size())) return nullptr;
    return r->children()[static_cast<std::size_t>(col)].get();
}

std::vector<Widget*> TableView::accessibleColumnHeaders() const {
    std::vector<Widget*> out;
    for (auto& c : band_->children()) out.push_back(c.get());
    return out;
}

std::vector<Widget*> TableView::accessibleSelection() const {
    if (behavior_ == SelectionBehavior::Rows) return ItemView::accessibleSelection();
    std::vector<Widget*> out;
    const Block b = selectedBlock();
    if (!b.valid()) return out;
    for (int r = b.row0; r <= b.row1; ++r)
        if (ItemRow* row = realizedRow(r))
            for (int c = b.col0; c <= b.col1 && c < static_cast<int>(row->children().size()); ++c) out.push_back(row->children()[static_cast<std::size_t>(c)].get());
    return out;
}

}  // namespace tcad::ui

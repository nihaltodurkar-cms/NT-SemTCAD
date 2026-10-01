#include "ui/widgets/tree_view.hpp"

#include "ui/core/keys.hpp"

#include <algorithm>
#include <cmath>

namespace tcad::ui {

using platform::KeyEvent;
using tcad::desktop::theme::T;

const std::string& TreeItem::text(int col) const {
    static const std::string kNone;
    return col >= 0 && col < static_cast<int>(texts_.size()) ? texts_[static_cast<std::size_t>(col)] : kNone;
}

const std::string& TreeItem::toolTip(int col) const {
    static const std::string kNone;
    return col >= 0 && col < static_cast<int>(tips_.size()) ? tips_[static_cast<std::size_t>(col)] : kNone;
}

int TreeItem::depth() const {
    int d = 0;
    for (const TreeItem* p = parent_; p; p = p->parent_) ++d;
    return d;
}

TreeView::TreeView() {
    band_ = addChild<ColumnHeaderBand>(static_cast<ColumnModel*>(this));
    band_->name = "header";
    widths_.assign(1, 0.0f);
    headers_.assign(1, std::string());
}

// -- items --------------------------------------------------------------------------------------------------------------

TreeItem* TreeView::make(std::vector<std::string> texts, TreeItem* parent) {
    auto it = std::make_unique<TreeItem>();
    texts.resize(std::max(texts.size(), static_cast<std::size_t>(cols_)));
    it->texts_ = std::move(texts);
    it->tips_.resize(it->texts_.size());
    it->parent_ = parent;
    TreeItem* raw = it.get();
    if (parent) parent->children_.push_back(std::move(it));
    else roots_.push_back(std::move(it));
    return raw;
}

TreeItem* TreeView::addTopLevelItem(std::vector<std::string> texts) {
    TreeItem* it = make(std::move(texts), nullptr);
    rebuild(true);
    return it;
}

TreeItem* TreeView::addItem(TreeItem* parent, std::vector<std::string> texts) {
    if (!parent) return addTopLevelItem(std::move(texts));
    TreeItem* it = make(std::move(texts), parent);
    rebuild(true);
    return it;
}

void TreeView::removeItem(TreeItem* item) {
    if (!item) return;
    auto& list = item->parent_ ? item->parent_->children_ : roots_;
    const auto it = std::find_if(list.begin(), list.end(), [&](const std::unique_ptr<TreeItem>& p) { return p.get() == item; });
    if (it == list.end()) return;
    TreeItem* cur = currentItem();
    bool cur_inside = false;
    for (const TreeItem* p = cur; p; p = p->parent_) cur_inside = cur_inside || p == item;
    // the visible list must not keep pointers to items about to die: rebuild it from the survivors, with the dying
    // subtree's rows reported as removed
    std::vector<TreeItem*> before = visible_;
    list.erase(it);
    std::vector<TreeItem*> after;
    for (auto& r : roots_) collectVisible(r.get(), after);
    const bool was_blocked = signalsBlocked();
    setSignalsBlocked(true);
    changeVisible(before, after);
    setSignalsBlocked(was_blocked);
    fit_cache_ = -1;
    band_->sync();
    if (cur_inside && !was_blocked) {
        if (on_current_row_changed) on_current_row_changed(currentRow());
        if (on_current_item_changed) on_current_item_changed(currentItem());
    }
}

void TreeView::clear() {
    const bool had_current = currentRow() >= 0;
    roots_.clear();
    visible_.clear();
    fit_cache_ = -1;
    modelReset();
    band_->sync();
    if (had_current && !signalsBlocked() && on_current_item_changed) on_current_item_changed(nullptr);
}

void TreeView::setItemText(TreeItem* item, int col, std::string text) {
    if (!item || col < 0) return;
    if (col >= static_cast<int>(item->texts_.size())) item->texts_.resize(static_cast<std::size_t>(col) + 1), item->tips_.resize(item->texts_.size());
    item->texts_[static_cast<std::size_t>(col)] = std::move(text);
    fit_cache_ = -1;
    const int row = rowOfItem(item);
    if (row >= 0) rowChanged(row);
    band_->sync();
}

void TreeView::setItemToolTip(TreeItem* item, int col, std::string tip) {
    if (!item || col < 0) return;
    if (col >= static_cast<int>(item->tips_.size())) item->tips_.resize(static_cast<std::size_t>(col) + 1);
    item->tips_[static_cast<std::size_t>(col)] = std::move(tip);
    const int row = rowOfItem(item);
    if (row >= 0) rowChanged(row);
}

void TreeView::setFirstColumnSpanned(TreeItem* item, bool on) {
    if (!item || item->span_first_ == on) return;
    item->span_first_ = on;
    const int row = rowOfItem(item);
    if (row >= 0) rowChanged(row);
}

int TreeView::rowOfItem(const TreeItem* item) const {
    const auto it = std::find(visible_.begin(), visible_.end(), item);
    return it == visible_.end() ? -1 : static_cast<int>(it - visible_.begin());
}

void TreeView::collectVisible(TreeItem* item, std::vector<TreeItem*>& out) const {
    out.push_back(item);
    if (item->expanded_)
        for (auto& c : item->children_) collectVisible(c.get(), out);
}

void TreeView::changeVisible(const std::vector<TreeItem*>& before, const std::vector<TreeItem*>& after) {
    std::size_t pre = 0;
    while (pre < before.size() && pre < after.size() && before[pre] == after[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < before.size() - pre && suf < after.size() - pre && before[before.size() - 1 - suf] == after[after.size() - 1 - suf]) ++suf;
    const std::size_t removed = before.size() - pre - suf, inserted = after.size() - pre - suf;
    if (removed) {  // the rows leave first: visible_ is `before` without them
        visible_.assign(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(pre));
        visible_.insert(visible_.end(), before.end() - static_cast<std::ptrdiff_t>(suf), before.end());
        rowsRemoved(static_cast<int>(pre), static_cast<int>(removed));
    }
    visible_ = after;
    if (inserted) rowsInserted(static_cast<int>(pre), static_cast<int>(inserted));
    else if (!removed) refreshRealizedRows();
}

void TreeView::rebuild(bool) {
    TreeItem* before_cur = currentItem();
    std::vector<TreeItem*> before = visible_, after;
    for (auto& r : roots_) collectVisible(r.get(), after);
    const bool was_blocked = signalsBlocked();
    setSignalsBlocked(true);
    changeVisible(before, after);
    // the current ITEM stays current wherever its row went (the diff may have taken its row out and put it back); one that
    // went into a collapsed parent hands the current place to that parent (Qt)
    if (before_cur) {
        TreeItem* a = before_cur;
        while (a && rowOfItem(a) < 0) a = a->parent_;
        if (a) setCurrentInternal(rowOfItem(a), false, true);
    }
    setSignalsBlocked(was_blocked);
    fit_cache_ = -1;
    band_->sync();
    if (currentItem() != before_cur && !was_blocked) {
        if (on_current_row_changed) on_current_row_changed(currentRow());
        if (on_current_item_changed) on_current_item_changed(currentItem());
    }
}

void TreeView::setExpanded(TreeItem* item, bool on) {
    if (!item || item->expanded_ == on) return;
    item->expanded_ = on;
    rebuild(true);
    if (item->childCount() == 0 || signalsBlocked()) return;
    if (on && on_item_expanded) on_item_expanded(item);
    if (!on && on_item_collapsed) on_item_collapsed(item);
}

void TreeView::expandAll() {
    std::vector<TreeItem*> changed;
    std::function<void(TreeItem*)> walk = [&](TreeItem* i) {
        if (i->childCount() > 0 && !i->expanded_) i->expanded_ = true, changed.push_back(i);
        for (auto& c : i->children_) walk(c.get());
    };
    for (auto& r : roots_) walk(r.get());
    rebuild(true);
    if (!signalsBlocked() && on_item_expanded)
        for (TreeItem* i : changed) on_item_expanded(i);
}

void TreeView::collapseAll() {
    std::vector<TreeItem*> changed;
    std::function<void(TreeItem*)> walk = [&](TreeItem* i) {
        if (i->childCount() > 0 && i->expanded_) i->expanded_ = false, changed.push_back(i);
        for (auto& c : i->children_) walk(c.get());
    };
    for (auto& r : roots_) walk(r.get());
    rebuild(true);
    if (!signalsBlocked() && on_item_collapsed)
        for (TreeItem* i : changed) on_item_collapsed(i);
}

void TreeView::setCurrentItem(TreeItem* item) {
    if (!item) {
        setCurrentRow(-1);
        return;
    }
    for (TreeItem* p = item->parent_; p; p = p->parent_) setExpanded(p, true);
    setCurrentRow(rowOfItem(item));
}

void TreeView::currentChanged(int row) {
    if (!signalsBlocked() && on_current_item_changed) on_current_item_changed(itemAtRow(row));
}

// -- columns ------------------------------------------------------------------------------------------------------------

void TreeView::shapeChanged() {
    widths_.resize(static_cast<std::size_t>(cols_), 0.0f);
    headers_.resize(static_cast<std::size_t>(cols_));
    fit_cache_ = -1;
    band_->sync();
    relayout();
    band_->sync();
    refreshRealizedRows();
}

void TreeView::setColumnCount(int n) {
    n = std::max(1, n);
    if (n == cols_) return;
    cols_ = n;
    shapeChanged();
}

void TreeView::setHeaderLabels(std::vector<std::string> labels) {
    if (static_cast<int>(labels.size()) > cols_) {
        cols_ = static_cast<int>(labels.size());
        headers_ = std::move(labels);
        shapeChanged();
        return;
    }
    labels.resize(static_cast<std::size_t>(cols_));
    headers_ = std::move(labels);
    fit_cache_ = -1;
    band_->sync();
    relayout();
    band_->sync();
}

std::string TreeView::headerLabel(int col) const {
    return col >= 0 && col < static_cast<int>(headers_.size()) ? headers_[static_cast<std::size_t>(col)] : std::string();
}

void TreeView::setHeaderVisible(bool on) {
    if (on == header_visible_) return;
    header_visible_ = on;
    band_->setVisible(on);
    relayout();
}

void TreeView::setFirstColumnFitsContents(bool on) {
    fit_first_ = on;
    fit_cache_ = -1;
    contentChangedWidth();
    band_->sync();
    refreshRealizedRows();
}

void TreeView::setStretchLastColumn(bool on) {
    stretch_last_ = on;
    band_->sync();
    refreshRealizedRows();
    update();
}

float TreeView::firstColumnFit() const {
    if (fit_cache_ >= 0) return fit_cache_;
    TextEngine* te = textEngine();
    if (!te) return kDefaultColumn;
    TextStyle st;
    float w = te->measure(headerLabel(0), st).width + 2 * ColumnHeaderBand::kCellPad;
    for (const TreeItem* it : visible_)
        w = std::max(w, static_cast<float>(it->depth()) * kIndent + kPad + kArrow + te->measure(it->text(0), st).width + kPad);
    fit_cache_ = std::max(kMinColumn, std::ceil(w));
    return fit_cache_;
}

float TreeView::baseWidth(int col) const {
    if (col < 0 || col >= cols_) return 0;
    const float set = widths_[static_cast<std::size_t>(col)];
    if (set > 0) return set;
    return col == 0 && fit_first_ ? firstColumnFit() : kDefaultColumn;
}

float TreeView::columnX(int col) const {
    float x = 0;
    for (int i = 0; i < col && i < cols_; ++i) x += baseWidth(i);
    return x;
}

float TreeView::columnWidth(int col) const {
    if (col < 0 || col >= cols_) return 0;
    float w = baseWidth(col);
    if (stretch_last_ && col == cols_ - 1) w = std::max(w, viewportRect().width - columnX(col));
    return w;
}

float TreeView::contentWidth() const {
    float w = 0;
    for (int i = 0; i < cols_; ++i) w += baseWidth(i);
    return w;
}

void TreeView::setColumnWidth(int col, float dips) {
    if (col < 0 || col >= cols_) return;
    widths_[static_cast<std::size_t>(col)] = std::max(kMinColumn, dips);
    contentChangedWidth();
    band_->sync();
    refreshRealizedRows();
}

void TreeView::resizeColumnToContents(int col) {
    if (col < 0 || col >= cols_) return;
    if (col == 0) {
        fit_cache_ = -1;
        setColumnWidth(0, firstColumnFit());
        return;
    }
    TextEngine* te = textEngine();
    if (!te) return;
    TextStyle st;
    float w = te->measure(headerLabel(col), st).width;
    for (const TreeItem* it : visible_) w = std::max(w, te->measure(it->text(col), st).width);
    setColumnWidth(col, std::ceil(w + 2 * ColumnHeaderBand::kCellPad));
}

int TreeView::columnAt(float view_x) const {
    const float rx = view_x - (viewportRect().x - scrollOffsetX());
    if (rx < 0) return -1;
    for (int c = 0; c < cols_; ++c)
        if (rx < columnX(c) + columnWidth(c)) return c;
    return -1;
}

void TreeView::layoutHeader(const RectI& px) {
    band_->setGeometry(px);
    band_->sync();
}

// -- rows ---------------------------------------------------------------------------------------------------------------

std::string TreeView::rowText(int row) const {
    const TreeItem* it = itemAtRow(row);
    return it ? it->text(0) : std::string();
}

std::string TreeView::rowAccessibleName(int row) const {
    const TreeItem* it = itemAtRow(row);
    if (!it) return {};
    std::string s;
    for (int c = 0; c < cols_; ++c) {
        if (it->text(c).empty()) continue;
        s += (s.empty() ? "" : ", ") + it->text(c);
    }
    return s;
}

std::string TreeView::rowToolTip(int row) const {
    const TreeItem* it = itemAtRow(row);
    if (!it) return {};
    for (int c = cols_ - 1; c >= 0; --c)
        if (!it->toolTip(c).empty()) return it->toolTip(c);
    return {};
}

std::string TreeView::copyTextFor(const std::vector<int>& rows) const {
    std::string out;
    for (int r : rows) {
        const TreeItem* it = itemAtRow(r);
        if (!it) continue;
        std::string line;
        for (int c = 0; c < cols_; ++c) line += (c ? "\t" : "") + it->text(c);
        out += (out.empty() ? "" : "\n") + line;
    }
    return out;
}

void TreeView::paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) {
    const TreeItem* it = itemAtRow(row);
    if (!it) return;
    const float indent_x = columnX(0) + kPad + static_cast<float>(it->depth()) * kIndent;
    const bool hc_sel = st.selected && st.focused && highContrast().on;
    if (it->childCount() > 0) {  // the disclosure triangle, in a 16-DIP box
        const float cx = indent_x + kArrow / 2 - 1, cy = std::round(size.height / 2);
        std::vector<PointF> tri = it->expanded_ ? std::vector<PointF>{{cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}}
                                                : std::vector<PointF>{{cx - 2, cy - 4}, {cx - 2, cy + 4}, {cx + 3, cy}};
        p.fillPolygon(tri, token(hc_sel ? T::OnAccent : T::TextDim));
    }
    TextStyle ts;
    ts.color = st.text;
    ts.valign = VAlign::Center;
    const float text_x = indent_x + kArrow;
    if (it->span_first_) {
        p.drawText({text_x, 0, std::max(0.0f, size.width - text_x - kPad), size.height}, it->text(0), ts);
        return;
    }
    p.drawText({text_x, 0, std::max(0.0f, columnX(0) + columnWidth(0) - text_x - kPad), size.height}, it->text(0), ts);
    for (int c = 1; c < cols_; ++c)
        p.drawText({columnX(c) + ColumnHeaderBand::kCellPad - 2, 0, std::max(0.0f, columnWidth(c) - 2 * ColumnHeaderBand::kCellPad + 2), size.height}, it->text(c), ts);
}

// -- expanding ----------------------------------------------------------------------------------------------------------

void TreeView::toggleRow(int row) {
    TreeItem* it = itemAtRow(row);
    if (it && it->childCount() > 0) setExpanded(it, !it->expanded_);
}

bool TreeView::rowPressed(int row, const UiMouseEvent&, float x) {
    const TreeItem* it = itemAtRow(row);
    if (!it || it->childCount() == 0) return false;
    const float left = viewportRect().x - scrollOffsetX() + columnX(0) + kPad + static_cast<float>(it->depth()) * kIndent;
    if (x >= left && x < left + kArrow) {
        toggleRow(row);
        return true;  // the triangle only opens or closes: the selection stays
    }
    return false;
}

void TreeView::rowDoubleClicked(int row, float x) {
    TreeItem* it = itemAtRow(row);
    if (it && it->childCount() > 0) {
        toggleRow(row);
        return;
    }
    if (it && !signalsBlocked() && on_item_activated) on_item_activated(it);
    ItemView::rowDoubleClicked(row, x);
}

bool TreeView::rowKey(int row, const KeyEvent& e) {
    TreeItem* it = itemAtRow(row);
    if (!it || e.mods != platform::Mod::None) return false;
    switch (e.vk) {
        case keys::Right:
            if (it->childCount() == 0) return true;
            if (!it->expanded_) setExpanded(it, true);
            else setCurrentRow(row + 1);
            return true;
        case keys::Left:
            if (it->childCount() > 0 && it->expanded_) setExpanded(it, false);
            else if (it->parent_) setCurrentRow(rowOfItem(it->parent_));
            return true;
        case keys::Add:
            setExpanded(it, true);
            return true;
        case keys::Subtract:
            setExpanded(it, false);
            return true;
        case keys::Multiply: {
            std::function<void(TreeItem*)> open = [&](TreeItem* i) {
                if (i->childCount() > 0) setExpanded(i, true);
                for (int k = 0; k < i->childCount(); ++k) open(i->child(k));
            };
            open(it);
            return true;
        }
        case keys::Return:
            if (!signalsBlocked() && on_item_activated) on_item_activated(it);
            if (!signalsBlocked() && on_row_activated) on_row_activated(row);
            return true;
        default: return false;
    }
}

int TreeView::rowExpandState(int row) const {
    const TreeItem* it = itemAtRow(row);
    if (!it || it->childCount() == 0) return -1;
    return it->expanded_ ? 1 : 0;
}

void TreeView::rowExpand(int row, bool open) {
    if (TreeItem* it = itemAtRow(row)) setExpanded(it, open);
}

}  // namespace tcad::ui

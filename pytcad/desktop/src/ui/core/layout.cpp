#include "ui/core/layout.hpp"

#include "ui/core/style.hpp"
#include "ui/widgets/label.hpp"

#include <algorithm>
#include <cstdint>

namespace tcad::ui {

namespace {

int sat(std::int64_t v) { return static_cast<int>(std::min<std::int64_t>(v, kMaxPx)); }

bool canShrink(SizePolicy p) { return p == SizePolicy::Preferred || p == SizePolicy::Expanding; }
bool canGrow(SizePolicy p) { return p != SizePolicy::Fixed; }

AxisItem widgetAxis(const Widget& w, Orientation o, double s) {
    const bool h = o == Orientation::Horizontal;
    const SizePolicy p = h ? w.sizePolicy().horizontal : w.sizePolicy().vertical;
    const SizeF hint = w.sizeHint(), minh = w.minimumSizeHint(), emin = w.minimumSize(), emax = w.maximumSize();
    AxisItem a;
    a.hint = ceilPx(h ? hint.width : hint.height, s);
    a.min = canShrink(p) ? ceilPx(h ? minh.width : minh.height, s) : a.hint;
    a.min = std::max(a.min, ceilPx(h ? emin.width : emin.height, s));
    a.max = canGrow(p) ? ceilPx(h ? emax.width : emax.height, s) : a.hint;
    a.max = std::max(a.max, a.min);
    a.hint = std::clamp(a.hint, a.min, a.max);
    a.expanding = p == SizePolicy::Expanding;
    return a;
}

// A widget's vertical needs at `width` px: with height-for-width, minimum = hint = heightForWidth(width), kept within
// its explicit minimum/maximum (Qt's rule); otherwise its plain vertical axis.
AxisItem widgetVerticalAt(const Widget& w, int width, double s) {
    AxisItem a = widgetAxis(w, Orientation::Vertical, s);
    if (!w.hasHeightForWidth()) return a;
    const int h = ceilPx(w.heightForWidth(static_cast<float>(width / s)), s);
    const int lo = ceilPx(w.minimumSize().height, s), hi = std::max(lo, ceilPx(w.maximumSize().height, s));
    a.min = a.hint = std::clamp(h, lo, hi);
    a.max = canGrow(w.sizePolicy().vertical) ? std::max(a.max, a.min) : a.min;
    return a;
}

// Along one axis inside `cell_pos/cell_len`: the item's size (clamped to its maximum) and position (left/top for
// `leading`, centred otherwise).
std::pair<int, int> fit(int cell_pos, int cell_len, const AxisItem& a, bool leading) {
    const int len = std::min(cell_len, a.max);
    return {leading ? cell_pos : cell_pos + (cell_len - len) / 2, len};
}

}  // namespace

// -- distribution ---------------------------------------------------------------------------------------------

std::vector<int> distribute(const std::vector<AxisItem>& items, int space) {
    const std::size_t n = items.size();
    std::vector<int> size(n);
    std::int64_t sum_min = 0, sum_hint = 0;
    for (const auto& it : items) sum_min += it.min, sum_hint += it.hint;
    if (n == 0) return size;
    if (space <= sum_min) {
        for (std::size_t i = 0; i < n; ++i) size[i] = items[i].min;
        return size;
    }
    if (space < sum_hint) {  // shrink: each gives up part of (hint - min), in proportion to it
        const std::int64_t deficit = sum_hint - space, room = sum_hint - sum_min;
        std::int64_t given = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::int64_t take = deficit * (items[i].hint - items[i].min) / room;
            size[i] = static_cast<int>(items[i].hint - take);
            given += take;
        }
        for (std::int64_t rem = deficit - given; rem > 0;)
            for (std::size_t i = 0; i < n && rem > 0; ++i)
                if (size[i] > items[i].min) --size[i], --rem;
        return size;
    }
    for (std::size_t i = 0; i < n; ++i) size[i] = items[i].hint;
    std::int64_t extra = space - sum_hint;
    // Stage 0: stretch > 0 (weight = stretch); 1: Expanding (weight 1); 2: anything that can grow (weight 1).
    for (int stage = 0; stage < 3 && extra > 0; ++stage) {
        auto weight = [&](std::size_t i) -> std::int64_t {
            if (size[i] >= items[i].max) return 0;
            if (stage == 0) return items[i].stretch > 0 ? items[i].stretch : 0;
            if (stage == 1) return items[i].expanding ? 1 : 0;
            return 1;
        };
        while (extra > 0) {
            std::int64_t total = 0;
            for (std::size_t i = 0; i < n; ++i) total += weight(i);
            if (total == 0) break;  // this stage has nobody left: the next one
            std::int64_t handed = 0;
            bool capped = false;
            std::vector<std::int64_t> w(n);
            for (std::size_t i = 0; i < n; ++i) w[i] = weight(i);
            for (std::size_t i = 0; i < n; ++i) {
                if (!w[i]) continue;
                const std::int64_t share = extra * w[i] / total, room = items[i].max - size[i];
                const std::int64_t give = std::min(share, room);
                size[i] += static_cast<int>(give);
                handed += give;
                if (give == room) capped = true;
            }
            extra -= handed;
            if (capped) continue;  // share again among those still below their maximum
            for (std::size_t i = 0; i < n && extra > 0; ++i)  // the whole pixels rounding left over
                if (w[i] && size[i] < items[i].max) ++size[i], --extra;
            break;
        }
    }
    return size;
}

// -- Layout base ----------------------------------------------------------------------------------------------

Layout::Layout(Widget* host) : host_(host) {
    const Style& st = Style::standard();
    margins_ = {st.layout_margin, st.layout_margin, st.layout_margin, st.layout_margin};
    spacing_ = st.layout_spacing;
}

Layout::~Layout() = default;

void Layout::setContentsMargins(float l, float t, float r, float b) {
    margins_ = {l, t, r, b};
    changed();
}

void Layout::setSpacing(float dips) {
    spacing_ = dips;
    changed();
}

RectI Layout::contentRect(const RectI& r) const {
    const double s = scale();
    const int l = roundPx(margins_.left, s), t = roundPx(margins_.top, s);
    return {r.x + l, r.y + t, std::max(0, r.width - marginsH()), std::max(0, r.height - marginsV())};
}

void Layout::changed() {
    if (host_) host_->updateGeometry();
}

bool Layout::Item::visible() const {
    if (spacer) return true;
    if (widget) return widget->isVisibleSelf();
    return layout && !layout->isEmpty();
}

AxisItem Layout::Item::axis(Orientation o, Orientation main, double s) const {
    if (spacer) {
        AxisItem a;
        if (o == main) a.stretch = stretch, a.expanding = true;
        return a;
    }
    AxisItem a;
    if (widget) {
        a = widgetAxis(*widget, o, s);
    } else {
        const bool h = o == Orientation::Horizontal;
        const SizeI mn = layout->minimumSizePx(), hi = layout->sizeHintPx(), mx = layout->maximumSizePx();
        a.min = h ? mn.width : mn.height;
        a.max = std::max(a.min, h ? mx.width : mx.height);
        a.hint = std::clamp(h ? hi.width : hi.height, a.min, a.max);
        a.expanding = layout->expanding(o);
    }
    if (o == main) a.stretch = stretch;
    return a;
}

bool Layout::Item::hasHeightForWidth() const {
    if (spacer) return false;
    if (widget) return widget->isVisibleSelf() && widget->hasHeightForWidth();
    return layout && layout->hasHeightForWidth();
}

int Layout::Item::widthIn(int cell_width, double s) const {
    if (!widget) return cell_width;  // a nested layout takes the whole cell
    return std::min(cell_width, widgetAxis(*widget, Orientation::Horizontal, s).max);
}

AxisItem Layout::Item::verticalAt(int width, Orientation main, double s) const {
    if (!hasHeightForWidth()) return axis(Orientation::Vertical, main, s);
    AxisItem a;
    if (widget) {
        a = widgetVerticalAt(*widget, width, s);
    } else {
        a = axis(Orientation::Vertical, main, s);
        a.min = a.hint = layout->heightForWidthPx(width);
        a.max = std::max(a.max, a.min);
    }
    if (main == Orientation::Vertical) a.stretch = stretch;
    return a;
}

void Layout::Item::place(const RectI& cell, double s) const {
    if (spacer) return;
    if (layout) {
        layout->setGeometryPx(cell);
        return;
    }
    const AxisItem ax = widgetAxis(*widget, Orientation::Horizontal, s), ay = widgetAxis(*widget, Orientation::Vertical, s);
    const auto [x, w] = fit(cell.x, cell.width, ax, true);
    const auto [y, h] = fit(cell.y, cell.height, ay, false);
    widget->setGeometry({x, y, w, h});
}

// -- BoxLayout ------------------------------------------------------------------------------------------------

BoxLayout::BoxLayout(Widget* host, Orientation o) : Layout(host), orientation_(o) {}

void BoxLayout::addWidget(Widget* w, int stretch) {
    if (!w) return;
    items_.push_back(Item::ofWidget(w, stretch));
    changed();
}

template <class L, class... A>
L* BoxLayout::nest(int stretch, A&&... a) {
    auto l = std::make_unique<L>(host_, std::forward<A>(a)...);
    l->markNested();
    L* raw = l.get();
    Item it;
    it.layout = std::move(l);
    it.stretch = stretch;
    items_.push_back(std::move(it));
    changed();
    return raw;
}

BoxLayout* BoxLayout::addBox(Orientation o, int stretch) { return nest<BoxLayout>(stretch, o); }
FormLayout* BoxLayout::addForm(int stretch) { return nest<FormLayout>(stretch); }
GridLayout* BoxLayout::addGrid(int stretch) { return nest<GridLayout>(stretch); }

void BoxLayout::addStretch(int stretch) {
    items_.push_back(Item::ofSpacer(stretch));
    changed();
}

SizeI BoxLayout::total(bool minimum) const {
    const double s = scale();
    const Orientation cross = orientation_ == Orientation::Horizontal ? Orientation::Vertical : Orientation::Horizontal;
    std::int64_t main = 0;
    int cr = 0, solid = 0;
    for (const Item& it : items_) {
        if (!it.visible()) continue;
        const AxisItem a = it.axis(orientation_, orientation_, s), c = it.axis(cross, orientation_, s);
        main += minimum ? a.min : a.hint;
        cr = std::max(cr, minimum ? c.min : c.hint);
        solid += !it.spacer;
    }
    main += static_cast<std::int64_t>(spacingPx()) * std::max(0, solid - 1);
    const bool h = orientation_ == Orientation::Horizontal;
    const int mm = h ? marginsH() : marginsV(), mc = h ? marginsV() : marginsH();
    return h ? SizeI{sat(main + mm), cr + mc} : SizeI{cr + mc, sat(main + mm)};
}

SizeI BoxLayout::minimumSizePx() const { return total(true); }
SizeI BoxLayout::sizeHintPx() const { return total(false); }

SizeI BoxLayout::maximumSizePx() const {
    const double s = scale();
    std::int64_t main = 0;
    int solid = 0;
    for (const Item& it : items_) {
        if (!it.visible()) continue;
        main += it.axis(orientation_, orientation_, s).max;
        solid += !it.spacer;
    }
    main += static_cast<std::int64_t>(spacingPx()) * std::max(0, solid - 1);
    const bool h = orientation_ == Orientation::Horizontal;
    main += h ? marginsH() : marginsV();
    return h ? SizeI{sat(main), kMaxPx} : SizeI{kMaxPx, sat(main)};
}

bool BoxLayout::expanding(Orientation o) const {
    const double s = scale();
    for (const Item& it : items_)
        if (it.visible() && !it.spacer && it.axis(o, orientation_, s).expanding) return true;
    return false;
}

bool BoxLayout::isEmpty() const {
    for (const Item& it : items_)
        if (!it.spacer && it.visible()) return false;
    return true;
}

std::vector<AxisItem> BoxLayout::mainAxes(int content_width, std::vector<const Item*>& vis, int& solid) const {
    const double s = scale();
    std::vector<AxisItem> ax;
    solid = 0;
    for (const Item& it : items_) {
        if (!it.visible()) continue;
        vis.push_back(&it);
        ax.push_back(orientation_ == Orientation::Vertical ? it.verticalAt(it.widthIn(content_width, s), orientation_, s)
                                                           : it.axis(orientation_, orientation_, s));
        solid += !it.spacer;
    }
    return ax;
}

bool BoxLayout::hasHeightForWidth() const {
    for (const Item& it : items_)
        if (it.visible() && it.hasHeightForWidth()) return true;
    return false;
}

int BoxLayout::heightForWidthPx(int width) const {
    const double s = scale();
    const int cw = std::max(0, width - marginsH()), sp = spacingPx();
    std::vector<const Item*> vis;
    int solid = 0;
    const std::vector<AxisItem> ax = mainAxes(cw, vis, solid);
    if (orientation_ == Orientation::Vertical) {
        std::int64_t t = 0;
        for (const AxisItem& a : ax) t += a.hint;
        return sat(t + static_cast<std::int64_t>(sp) * std::max(0, solid - 1) + marginsV());
    }
    // across a horizontal box: the widths it would give, and the tallest item at its width
    const std::vector<int> widths = distribute(ax, std::max(0, cw - sp * std::max(0, solid - 1)));
    int tallest = 0;
    for (std::size_t i = 0; i < vis.size(); ++i)
        if (!vis[i]->spacer) tallest = std::max(tallest, vis[i]->verticalAt(vis[i]->widthIn(widths[i], s), orientation_, s).hint);
    return tallest + marginsV();
}

void BoxLayout::setGeometryPx(const RectI& r) {
    const double s = scale();
    const RectI c = contentRect(r);
    const bool h = orientation_ == Orientation::Horizontal;
    const Orientation cross = h ? Orientation::Vertical : Orientation::Horizontal;
    std::vector<const Item*> vis;
    int solid = 0;
    const std::vector<AxisItem> ax = mainAxes(c.width, vis, solid);
    const int sp = spacingPx();
    const int space = std::max(0, (h ? c.width : c.height) - sp * std::max(0, solid - 1));
    const std::vector<int> sizes = distribute(ax, space);
    int pos = h ? c.x : c.y;
    bool seen_solid = false;
    for (std::size_t i = 0; i < vis.size(); ++i) {
        const Item& it = *vis[i];
        if (!it.spacer) {
            if (seen_solid) pos += sp;  // spacing only between real items (Qt: spacers are empty items)
            seen_solid = true;
        }
        if (it.layout) {
            it.layout->setGeometryPx(h ? RectI{pos, c.y, sizes[i], c.height} : RectI{c.x, pos, c.width, sizes[i]});
        } else if (it.widget) {
            const AxisItem cx = it.axis(cross, orientation_, s);
            const auto [cp, cl] = fit(h ? c.y : c.x, h ? c.height : c.width, cx, !h);  // across: centred vertically, left
            it.widget->setGeometry(h ? RectI{pos, cp, sizes[i], cl} : RectI{cp, pos, cl, sizes[i]});
        }
        pos += sizes[i];
    }
}

// -- FormLayout -----------------------------------------------------------------------------------------------

FormLayout::FormLayout(Widget* host) : Layout(host) {}

void FormLayout::addRow(Widget* label, Widget* field) {
    rows_.push_back({label, field, false});
    changed();
}

void FormLayout::addRow(Widget* spanning) {
    rows_.push_back({nullptr, spanning, true});
    changed();
}

Label* FormLayout::addRow(std::string_view text, Widget* field) {
    auto* label = host_->addChild<Label>(std::string(text));
    label->setBuddy(field);
    // what a screen reader says for the field, as Qt's buddy relation gives it, unless the field has a name already
    if (field && field->accessibleName.empty()) field->accessibleName = label->shownText();
    addRow(label, field);
    return label;
}

void FormLayout::insertRow(int index, Widget* label, Widget* field) {
    index = std::clamp(index, 0, rowCount());
    rows_.insert(rows_.begin() + index, Row{label, field, false});
    changed();
}

bool FormLayout::rowVisible(const Row& r) const {
    return (r.label && r.label->isVisibleSelf()) || (r.field && r.field->isVisibleSelf());
}

int FormLayout::labelColumnPx(bool) const {
    const double s = scale();
    int w = 0;
    for (const Row& r : rows_)
        if (!r.spanning && rowVisible(r) && r.label && r.label->isVisibleSelf())
            w = std::max(w, widgetAxis(*r.label, Orientation::Horizontal, s).hint);  // labels keep their width
    return w;
}

SizeI FormLayout::total(bool minimum) const {
    const double s = scale();
    const int label_col = labelColumnPx(minimum), sp = spacingPx();
    int field_w = 0, span_w = 0, rows = 0;
    std::int64_t height = 0;
    for (const Row& r : rows_) {
        if (!rowVisible(r)) continue;
        int row_h = 0;
        if (r.label && r.label->isVisibleSelf()) {
            const AxisItem v = widgetAxis(*r.label, Orientation::Vertical, s);
            row_h = std::max(row_h, minimum ? v.min : v.hint);
        }
        if (r.field && r.field->isVisibleSelf()) {
            const AxisItem fh = widgetAxis(*r.field, Orientation::Horizontal, s), fv = widgetAxis(*r.field, Orientation::Vertical, s);
            (r.spanning ? span_w : field_w) = std::max(r.spanning ? span_w : field_w, minimum ? fh.min : fh.hint);
            row_h = std::max(row_h, minimum ? fv.min : fv.hint);
        }
        height += row_h;
        ++rows;
    }
    height += static_cast<std::int64_t>(sp) * std::max(0, rows - 1);
    const int columns = label_col + (label_col > 0 ? sp : 0) + field_w;
    return {std::max(columns, span_w) + marginsH(), sat(height + marginsV())};
}

SizeI FormLayout::minimumSizePx() const { return total(true); }
SizeI FormLayout::sizeHintPx() const { return total(false); }

bool FormLayout::expanding(Orientation o) const {
    if (o == Orientation::Vertical) return false;  // rows are packed at the top: no vertical extra is taken
    for (const Row& r : rows_)
        if (r.field && r.field->isVisibleSelf() && r.field->sizePolicy().horizontal == SizePolicy::Expanding) return true;
    return false;
}

bool FormLayout::isEmpty() const {
    for (const Row& r : rows_)
        if (rowVisible(r)) return false;
    return true;
}

FormLayout::RowFit FormLayout::fitRow(const Row& row, const RectI& c) const {
    const double s = scale();
    const int sp = spacingPx(), label_col = labelColumnPx(false);
    const int field_x = c.x + label_col + (label_col > 0 ? sp : 0);
    RowFit f;
    const bool lv = row.label && row.label->isVisibleSelf(), fv = row.field && row.field->isVisibleSelf();
    if (lv) {
        f.label_w = std::min(widgetAxis(*row.label, Orientation::Horizontal, s).hint, label_col);
        f.height = std::max(f.height, widgetVerticalAt(*row.label, f.label_w, s).hint);
    }
    if (fv) {
        f.field_x = row.spanning ? c.x : field_x;
        const int col = row.spanning ? c.width : std::max(0, c.right() - field_x);
        // fields grow to the column up to their maximum: Qt's AllNonFixedFieldsGrow (the Fusion default), since a
        // Fixed field's maximum is its hint
        f.field_w = std::max(0, std::min(col, widgetAxis(*row.field, Orientation::Horizontal, s).max));
        f.height = std::max(f.height, widgetVerticalAt(*row.field, f.field_w, s).hint);
    }
    return f;
}

bool FormLayout::hasHeightForWidth() const {
    for (const Row& r : rows_)
        if ((r.label && r.label->isVisibleSelf() && r.label->hasHeightForWidth()) ||
            (r.field && r.field->isVisibleSelf() && r.field->hasHeightForWidth()))
            return true;
    return false;
}

int FormLayout::heightForWidthPx(int width) const {
    const RectI c = contentRect({0, 0, width, 0});
    std::int64_t h = 0;
    int rows = 0;
    for (const Row& row : rows_) {
        if (!rowVisible(row)) continue;
        h += fitRow(row, c).height;
        ++rows;
    }
    return sat(h + static_cast<std::int64_t>(spacingPx()) * std::max(0, rows - 1) + marginsV());
}

void FormLayout::setGeometryPx(const RectI& r) {
    const double s = scale();
    const RectI c = contentRect(r);
    const int sp = spacingPx();
    int y = c.y;
    bool first = true;
    for (const Row& row : rows_) {
        if (!rowVisible(row)) continue;
        if (!first) y += sp;
        first = false;
        const RowFit f = fitRow(row, c);
        if (row.label && row.label->isVisibleSelf()) {
            const auto [py, ph] = fit(y, f.height, widgetVerticalAt(*row.label, f.label_w, s), false);
            row.label->setGeometry({c.x, py, f.label_w, ph});
        }
        if (row.field && row.field->isVisibleSelf()) {
            const auto [py, ph] = fit(y, f.height, widgetVerticalAt(*row.field, f.field_w, s), false);
            row.field->setGeometry({f.field_x, py, f.field_w, ph});
        }
        y += f.height;
    }
}

// -- GridLayout -----------------------------------------------------------------------------------------------

GridLayout::GridLayout(Widget* host) : Layout(host) {}

void GridLayout::addWidget(Widget* w, int row, int col, int row_span, int col_span) {
    if (!w || row < 0 || col < 0 || row_span < 1 || col_span < 1) return;
    cells_.push_back(Cell{Item::ofWidget(w), row, col, row_span, col_span});
    rows_ = std::max(rows_, row + row_span);
    cols_ = std::max(cols_, col + col_span);
    changed();
}

std::vector<AxisItem> GridLayout::lines(Orientation o, std::vector<bool>& occupied) const {
    const double s = scale();
    const bool h = o == Orientation::Horizontal;
    const int n = h ? cols_ : rows_;
    std::vector<AxisItem> ln(static_cast<std::size_t>(n));
    std::vector<int> max_seen(static_cast<std::size_t>(n), -1);
    occupied.assign(static_cast<std::size_t>(n), false);
    for (const Cell& c : cells_) {
        if (!c.item.visible()) continue;
        const int start = h ? c.col : c.row, span = h ? c.col_span : c.row_span;
        for (int i = start; i < start + span; ++i) occupied[static_cast<std::size_t>(i)] = true;
        if (span != 1) continue;
        const AxisItem a = c.item.axis(o, o, s);
        auto& l = ln[static_cast<std::size_t>(start)];
        l.min = std::max(l.min, a.min);
        l.hint = std::max(l.hint, a.hint);
        max_seen[static_cast<std::size_t>(start)] = std::max(max_seen[static_cast<std::size_t>(start)], a.max);
        l.expanding = l.expanding || a.expanding;
    }
    for (int i = 0; i < n; ++i) {
        auto& l = ln[static_cast<std::size_t>(i)];
        const int m = max_seen[static_cast<std::size_t>(i)];
        l.max = m < 0 ? (occupied[static_cast<std::size_t>(i)] ? kMaxPx : 0) : m;
    }
    // A spanning cell that needs more than its lines give: the deficit spread evenly (the remainder to the first).
    const int sp = spacingPx();
    for (const Cell& c : cells_) {
        if (!c.item.visible()) continue;
        const int start = h ? c.col : c.row, span = h ? c.col_span : c.row_span;
        if (span == 1) continue;
        const AxisItem a = c.item.axis(o, o, s);
        for (int pass = 0; pass < 2; ++pass) {  // 0: minimum, 1: hint
            int have = sp * (span - 1);
            for (int i = start; i < start + span; ++i) have += pass ? ln[static_cast<std::size_t>(i)].hint : ln[static_cast<std::size_t>(i)].min;
            int deficit = (pass ? a.hint : a.min) - have;
            for (int k = 0; deficit > 0; k = (k + 1) % span, --deficit) {
                auto& l = ln[static_cast<std::size_t>(start + k)];
                (pass ? l.hint : l.min) += 1;
            }
        }
        if (a.expanding)
            for (int i = start; i < start + span; ++i) ln[static_cast<std::size_t>(i)].expanding = true;
    }
    for (auto& l : ln) {
        l.hint = std::max(l.hint, l.min);
        l.max = std::max(l.max, l.hint);
    }
    return ln;
}

int GridLayout::extent(const std::vector<AxisItem>& ln, const std::vector<bool>& occupied, bool minimum) const {
    std::int64_t t = 0;
    int k = 0;
    for (std::size_t i = 0; i < ln.size(); ++i)
        if (occupied[i]) t += minimum ? ln[i].min : ln[i].hint, ++k;
    return sat(t + static_cast<std::int64_t>(spacingPx()) * std::max(0, k - 1));
}

SizeI GridLayout::minimumSizePx() const {
    std::vector<bool> oc, orr;
    const auto c = lines(Orientation::Horizontal, oc), r = lines(Orientation::Vertical, orr);
    return {extent(c, oc, true) + marginsH(), extent(r, orr, true) + marginsV()};
}

SizeI GridLayout::sizeHintPx() const {
    std::vector<bool> oc, orr;
    const auto c = lines(Orientation::Horizontal, oc), r = lines(Orientation::Vertical, orr);
    return {extent(c, oc, false) + marginsH(), extent(r, orr, false) + marginsV()};
}

bool GridLayout::expanding(Orientation o) const {
    std::vector<bool> oc;
    for (const auto& l : lines(o, oc))
        if (l.expanding) return true;
    return false;
}

bool GridLayout::isEmpty() const {
    for (const Cell& c : cells_)
        if (c.item.visible()) return false;
    return true;
}

void GridLayout::setGeometryPx(const RectI& r) {
    const double s = scale();
    const RectI c = contentRect(r);
    const int sp = spacingPx();
    auto positions = [&](Orientation o, int start, int len, std::vector<int>& pos, std::vector<int>& size) {
        std::vector<bool> oc;
        const auto ln = lines(o, oc);
        std::vector<AxisItem> used;
        for (std::size_t i = 0; i < ln.size(); ++i)
            if (oc[i]) used.push_back(ln[i]);
        const auto sz = distribute(used, std::max(0, len - sp * std::max(0, static_cast<int>(used.size()) - 1)));
        pos.assign(ln.size(), start);
        size.assign(ln.size(), 0);
        int p = start;
        std::size_t k = 0;
        for (std::size_t i = 0; i < ln.size(); ++i) {
            if (!oc[i]) {
                pos[i] = p;
                continue;
            }
            if (k) p += sp;
            pos[i] = p;
            size[i] = sz[k++];
            p += size[i];
        }
    };
    std::vector<int> cx, cw, ry, rh;
    positions(Orientation::Horizontal, c.x, c.width, cx, cw);
    positions(Orientation::Vertical, c.y, c.height, ry, rh);
    for (const Cell& cell : cells_) {
        if (!cell.item.visible()) continue;
        const std::size_t c0 = static_cast<std::size_t>(cell.col), c1 = static_cast<std::size_t>(cell.col + cell.col_span - 1);
        const std::size_t r0 = static_cast<std::size_t>(cell.row), r1 = static_cast<std::size_t>(cell.row + cell.row_span - 1);
        cell.item.place({cx[c0], ry[r0], cx[c1] + cw[c1] - cx[c0], ry[r1] + rh[r1] - ry[r0]}, s);
    }
}

// -- StackLayout ----------------------------------------------------------------------------------------------

StackLayout::StackLayout(Widget* host) : Layout(host) {}

int StackLayout::addWidget(Widget* w) {
    if (!w) return -1;
    pages_.push_back(w);
    if (current_ < 0) current_ = 0;
    w->setVisible(static_cast<int>(pages_.size()) - 1 == current_);
    changed();
    return static_cast<int>(pages_.size()) - 1;
}

void StackLayout::setCurrentIndex(int i) {
    if (i < 0 || i >= count() || i == current_) return;
    current_ = i;
    for (int k = 0; k < count(); ++k) pages_[static_cast<std::size_t>(k)]->setVisible(k == i);
}

Widget* StackLayout::currentWidget() const { return current_ >= 0 ? pages_[static_cast<std::size_t>(current_)] : nullptr; }

SizeI StackLayout::minimumSizePx() const {
    const double s = scale();
    SizeI m{0, 0};
    for (Widget* w : pages_)  // every page, shown or not (Qt's QStackedLayout), so switching does not resize
        m = {std::max(m.width, widgetAxis(*w, Orientation::Horizontal, s).min), std::max(m.height, widgetAxis(*w, Orientation::Vertical, s).min)};
    return {m.width + marginsH(), m.height + marginsV()};
}

SizeI StackLayout::sizeHintPx() const {
    const double s = scale();
    SizeI m{0, 0};
    for (Widget* w : pages_)
        m = {std::max(m.width, widgetAxis(*w, Orientation::Horizontal, s).hint), std::max(m.height, widgetAxis(*w, Orientation::Vertical, s).hint)};
    return {m.width + marginsH(), m.height + marginsV()};
}

bool StackLayout::expanding(Orientation o) const {
    const double s = scale();
    for (Widget* w : pages_)
        if (widgetAxis(*w, o, s).expanding) return true;
    return false;
}

bool StackLayout::isEmpty() const { return pages_.empty(); }

bool StackLayout::hasHeightForWidth() const {
    for (Widget* w : pages_)
        if (w->hasHeightForWidth()) return true;
    return false;
}

int StackLayout::heightForWidthPx(int width) const {
    const double s = scale();
    const int cw = std::max(0, width - marginsH());
    int h = 0;
    for (Widget* w : pages_) h = std::max(h, widgetVerticalAt(*w, std::min(cw, widgetAxis(*w, Orientation::Horizontal, s).max), s).hint);
    return h + marginsV();
}

void StackLayout::setGeometryPx(const RectI& r) {
    const RectI c = contentRect(r);
    for (Widget* w : pages_) w->setGeometry(c);
}

}  // namespace tcad::ui

#include "ui/widgets/list_view.hpp"

#include "ui/core/keys.hpp"

#include <algorithm>

namespace tcad::ui {

using tcad::desktop::theme::T;

ListView::ListView() { name = "list"; }

const ListItem& ListView::item(int row) const {
    static const ListItem kNone;
    return valid(row) ? items_[static_cast<std::size_t>(row)] : kNone;
}

int ListView::addItem(std::string text) {
    insertItem(count(), std::move(text));
    return count() - 1;
}

void ListView::insertItem(int row, std::string text) {
    row = std::clamp(row, 0, count());
    ListItem it;
    it.text = std::move(text);
    items_.insert(items_.begin() + row, std::move(it));
    rowsInserted(row, 1);
}

void ListView::removeItem(int row) {
    if (!valid(row)) return;
    items_.erase(items_.begin() + row);
    rowsRemoved(row, 1);
}

void ListView::clear() {
    items_.clear();
    modelReset();
}

bool ListView::rowEnabled(int row) const { return valid(row) && item(row).enabled && item(row).selectable; }

#define LIST_SET(row, member, value, notify)                              \
    do {                                                                  \
        if (!valid(row) || items_[static_cast<std::size_t>(row)].member == (value)) return; \
        items_[static_cast<std::size_t>(row)].member = (value);           \
        rowChanged(row);                                                  \
        if (notify && !signalsBlocked() && on_item_changed) on_item_changed(row); \
    } while (0)

void ListView::setItemText(int row, std::string text) { LIST_SET(row, text, text, true); }
void ListView::setItemToolTip(int row, std::string tip) { LIST_SET(row, tooltip, tip, false); }
void ListView::setItemEnabled(int row, bool on) { LIST_SET(row, enabled, on, false); }
void ListView::setItemSelectable(int row, bool on) { LIST_SET(row, selectable, on, false); }
void ListView::setItemCheckable(int row, bool on) { LIST_SET(row, checkable, on, false); }
void ListView::setItemChecked(int row, bool on) { LIST_SET(row, checked, on, true); }
void ListView::setItemEditable(int row, bool on) { LIST_SET(row, editable, on, false); }
void ListView::setItemItalic(int row, bool on) { LIST_SET(row, italic, on, false); }
void ListView::setItemBold(int row, bool on) { LIST_SET(row, bold, on, false); }
void ListView::setItemColor(int row, std::optional<T> c) { LIST_SET(row, color, c, false); }

void ListView::setItemData(int row, int role, ComboData v) {
    if (!valid(row)) return;
    items_[static_cast<std::size_t>(row)].data[role] = std::move(v);
}

ComboData ListView::itemData(int row, int role) const {
    const auto& d = item(row).data;
    const auto it = d.find(role);
    return it == d.end() ? ComboData{} : it->second;
}

int ListView::findData(const ComboData& v, int role) const {
    for (int i = 0; i < count(); ++i)
        if (itemData(i, role) == v) return i;
    return -1;
}

TextStyle ListView::styleOf(int row) const {
    const ListItem& it = item(row);
    TextStyle s;
    s.bold = it.bold;
    s.italic = it.italic;
    s.valign = VAlign::Center;
    return s;
}

void ListView::paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) {
    const ListItem& it = item(row);
    if (it.checkable) paintCheckMark(p, {kPad, std::round((size.height - kCheck) / 2), kCheck, kCheck}, it.checked ? 1 : 0, it.enabled);
    TextStyle s = styleOf(row);
    // a disabled item is faint; an item with its own colour keeps it unless the (high-contrast) selection recolours the text
    s.color = !it.enabled ? token(T::TextFaint) : (it.color && !(st.selected && st.focused && highContrast().on)) ? token(*it.color) : st.text;
    const float x = textX(row);
    p.drawText({x, 0, std::max(0.0f, size.width - x - 2), size.height}, it.text, s);
}

bool ListView::rowPressed(int row, const UiMouseEvent& e, float x) {
    (void)e;
    const ListItem& it = item(row);
    if (!it.checkable || !it.enabled) return false;
    const RectF r = rowRect(row);
    const float local = x - r.x;
    if (local >= kPad - 2 && local < kPad + kCheck + 2) rowToggle(row);  // the box: toggles, and the row still becomes current
    return false;
}

void ListView::rowToggle(int row) {
    if (valid(row) && item(row).checkable && item(row).enabled) setItemChecked(row, !item(row).checked);
}

bool ListView::rowKey(int row, const platform::KeyEvent& e) {
    if (e.vk == keys::Space && e.mods == platform::Mod::None && item(row).checkable) {
        rowToggle(row);
        return true;
    }
    return false;
}

bool ListView::canEdit(int row) const { return valid(row) && item(row).editable && item(row).enabled && host() && host()->canCreateInlineEditor(); }

void ListView::startEditing(int row) {
    if (!canEdit(row)) return;
    const RectF r = rowRect(row);
    const float x = textX(row);
    beginEdit(row, 0, {x - 3, 1, std::max(20.0f, r.width - x), r.height - 2}, item(row).text);
}

void ListView::rowDoubleClicked(int row, float x) {
    if (canEdit(row)) startEditing(row);
    else ItemView::rowDoubleClicked(row, x);
}

void ListView::commitEdit(int row, int, const std::string& text) { setItemText(row, text); }

}  // namespace tcad::ui

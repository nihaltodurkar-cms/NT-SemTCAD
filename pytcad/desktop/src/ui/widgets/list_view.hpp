// QListWidget (N3e, NATIVE-DESKTOP-PLAN.md 27.8.6): 11 in the panels -- the contact, gate, region and process-step
// lists, the catalog's model list (checkable), the field list (a header row that cannot be selected, rows that are
// disabled with a tool tip saying why), the plot's overlay list (items edited in place, italic, greyed when not drawn)
// and the validation list. What they call: addItem, clear, currentRow/setCurrentRow, item(i), item data under user roles
// (104 `data(` calls: the ids), setToolTip, setFont (italic), setForeground, setFlags (enabled, selectable, checkable,
// editable), setCheckState, currentItemChanged, itemChanged. Not used, so not built: icons, drag and drop, sorting,
// item widgets, a horizontal bar (long text is cut by the row).
//
// The engine is ItemView (item_view.hpp): realized rows, scrolling, keys, selection, typeahead, copy.
// Items (the QListWidgetItem's, by row here -- the view owns them):
//   * text, tool tip, user data under integer roles (the ComboData of the combo box: a bool, an int, a string);
//   * flags: enabled (greyed and not selectable when off), selectable (a header row: shown normally, never current),
//     checkable (a check box before the text: click it or press Space), editable (F2 or a double click edits the text
//     in place; Enter commits, Escape cancels); italic, bold, a colour token (the overlay list's greyed ones).
//   * on_item_changed(row): the item's check state or text changed -- by the user or by the program, as itemChanged;
//     setSignalsBlocked(true) is the QSignalBlocker the panels use while loading.
//   * the first item added to an empty list is NOT current (Qt's); removing the current row makes its neighbour current;
//     clear() leaves no current row.
// UI Automation: List with ListItem rows (name, SelectionItem, Toggle for checkable rows), Selection, Scroll.
#pragma once

#include "theme/tokens.hpp"
#include "ui/widgets/combo_box.hpp"
#include "ui/widgets/item_view.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tcad::ui {

struct ListItem {
    std::string text, tooltip;
    std::map<int, ComboData> data;
    bool enabled = true;
    bool selectable = true;
    bool checkable = false;
    bool checked = false;
    bool editable = false;
    bool italic = false;
    bool bold = false;
    std::optional<tcad::desktop::theme::T> color;
};

class ListView : public ItemView {
public:
    std::function<void(int)> on_item_changed;  // a row's check state or text changed

    ListView();

    int count() const { return static_cast<int>(items_.size()); }
    int addItem(std::string text);
    void insertItem(int row, std::string text);
    void removeItem(int row);
    void clear();
    const ListItem& item(int row) const;  // a default item out of range
    std::string itemText(int row) const { return item(row).text; }
    std::string currentText() const { return currentRow() >= 0 ? item(currentRow()).text : std::string(); }
    ComboData currentData(int role = 0) const { return currentRow() >= 0 ? itemData(currentRow(), role) : ComboData{}; }

    void setItemText(int row, std::string text);
    void setItemToolTip(int row, std::string tip);
    void setItemData(int row, int role, ComboData v);
    ComboData itemData(int row, int role = 0) const;
    int findData(const ComboData& v, int role = 0) const;  // -1 none
    void setItemEnabled(int row, bool on);
    void setItemSelectable(int row, bool on);
    void setItemCheckable(int row, bool on);
    void setItemChecked(int row, bool on);
    bool isItemChecked(int row) const { return item(row).checked; }
    void setItemEditable(int row, bool on);
    void setItemItalic(int row, bool on);
    void setItemBold(int row, bool on);
    void setItemColor(int row, std::optional<tcad::desktop::theme::T> c);
    void editItem(int row) { startEditing(row); }

    static constexpr float kPad = 6.0f;
    static constexpr float kCheck = 14.0f;

protected:
    int rowCount() const override { return count(); }
    std::string rowText(int row) const override { return item(row).text; }
    bool rowEnabled(int row) const override;
    Role rowRole(int) const override { return Role::ListItem; }
    std::string rowToolTip(int row) const override { return item(row).tooltip; }
    void paintRowContent(Painter& p, int row, const SizeF& size, const RowState& st) override;
    bool rowPressed(int row, const UiMouseEvent& e, float x) override;
    void rowDoubleClicked(int row, float x) override;
    bool rowKey(int row, const platform::KeyEvent& e) override;
    int rowToggleState(int row) const override { return item(row).checkable ? (item(row).checked ? 1 : 0) : -1; }
    void rowToggle(int row) override;
    bool canEdit(int row) const override;
    void startEditing(int row) override;
    void commitEdit(int row, int col, const std::string& text) override;

private:
    bool valid(int row) const { return row >= 0 && row < count(); }
    float textX(int row) const { return kPad + (item(row).checkable ? kCheck + kPad : 0.0f); }
    TextStyle styleOf(int row) const;
    std::vector<ListItem> items_;
};

}  // namespace tcad::ui

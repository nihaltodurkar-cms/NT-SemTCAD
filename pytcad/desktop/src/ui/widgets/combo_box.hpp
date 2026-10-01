// QComboBox, non-editable (N3c, NATIVE-DESKTOP-PLAN.md 27.8.4): 24 in the panels. What they call: addItem 19 (text and
// user data), addItems 10, clear 8, currentText 15, currentData 11, setCurrentIndex 14, setCurrentText 7, findData 7,
// findText 4, itemData 5, count 3; currentIndexChanged 8, currentTextChanged 7, activated 5; AdjustToContents 1.
// NOT used, so not built: editable combos, icons, insertion, max-visible-items (the list shows ten rows and scrolls),
// per-item enabled state, completers, the model/view API.
//
// Behaviour (Qt's unless said):
//   * the first item added to an empty combo becomes current; clear() leaves index -1 and text "";
//   * currentIndexChanged/currentTextChanged fire for every change of the current item, including the program's
//     (setCurrentIndexSilent is QSignalBlocker); `activated` fires only when the USER picks, even the current item again;
//   * a press opens the drop-down in a popup WINDOW below the combo (decision 3; ui/core/popup.hpp); the combo keeps the
//     keyboard focus and drives the list: Up/Down/Home/End/PageUp/PageDown move the highlight, Enter or Space picks it,
//     Tab picks it and closes, Escape (and F4, Alt+Up/Down) closes without a change. A press anywhere else in the
//     window closes it (a press on the combo itself only closes it); so do deactivating, moving or resizing the window.
//     A click on a row picks it.
//   * closed: Up/Left and Down/Right change the item (PageUp/PageDown by ten), Home/End go to the ends, F4, Alt+Down
//     and Space open the drop-down; the wheel steps one item per notch -- only while the combo has the focus;
//   * TYPEAHEAD: typing picks the first item starting with what was typed (case-folded for ASCII); the same letter
//     again cycles through the items starting with it; the typed text is forgotten after one second;
//   * the hint is the widest item plus padding and the arrow (Qt's AdjustToContents, which the one panel asks for and
//     the rest would have got on first show).
// UI Automation: ComboBox with Value (the current text; setting it picks the item with that text) and ExpandCollapse;
// the highlighted row of an open drop-down is announced to screen readers (UiHost::announce).
#pragma once

#include "ui/core/popup.hpp"
#include "ui/core/widget.hpp"

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace tcad::ui {

// QVariant, for what the panels store in a combo's items: a bool, an int, a string, or nothing.
using ComboData = std::variant<std::monostate, bool, int, std::string>;

// The rows of an open drop-down. It draws, scrolls and reports the row under a click; the combo moves its highlight
// from the keyboard.
class ComboPopupList : public Widget {
public:
    static constexpr int kMaxVisibleRows = 10;
    static constexpr float kPadding = 8.0f;

    std::function<void(int)> on_chosen;       // a row was clicked
    std::function<void(int)> on_highlighted;  // the highlight moved (keys, the pointer)

    ComboPopupList(std::vector<std::string> items, int current);

    int highlighted() const { return highlight_; }
    void setHighlighted(int row);                 // scrolls it into view
    bool moveHighlight(int step);                 // clamped; false at the end
    const std::vector<std::string>& items() const { return items_; }
    int firstVisible() const { return first_; }
    int visibleRows() const;                      // that fit the current height
    float rowHeight() const;
    int rowAt(float y) const;                     // -1 outside the rows

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    void hoverChanged(bool entered) override;
    Role accessibleRole() const override { return Role::List; }

protected:
    void resized() override { clampScroll(); }

private:
    void clampScroll();
    std::vector<std::string> items_;
    int highlight_ = -1;
    int first_ = 0;
    int pressed_ = -1;
};

class ComboBox : public Widget {
public:
    std::function<void(int)> on_current_index_changed;
    std::function<void(const std::string&)> on_current_text_changed;
    std::function<void(int)> on_activated;

    ComboBox();
    ~ComboBox() override;

    void addItem(std::string text, ComboData data = {});
    void addItems(const std::vector<std::string>& texts);
    void clear();
    int count() const { return static_cast<int>(items_.size()); }
    const std::string& itemText(int i) const;  // "" out of range
    const ComboData& itemData(int i) const;    // empty out of range
    int currentIndex() const { return current_; }
    std::string currentText() const { return current_ >= 0 ? items_[static_cast<std::size_t>(current_)].text : std::string(); }
    ComboData currentData() const { return current_ >= 0 ? items_[static_cast<std::size_t>(current_)].data : ComboData{}; }
    void setCurrentIndex(int i);  // -1 none; out of range is ignored. Reports the change.
    void setCurrentIndexSilent(int i);
    void setCurrentText(const std::string& text);  // the item with that text; none: nothing changes
    int findText(const std::string& text) const;   // -1 none
    int findData(const ComboData& data) const;

    bool isPopupOpen() const { return popup_ != nullptr; }
    void showPopup();
    void hidePopup();
    ComboPopupList* popupList() const { return list_; }  // null when closed

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool charEvent(char32_t c) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    bool wantsTab() const override { return popup_ != nullptr; }
    void focusChanged(bool, FocusReason) override { update(); }
    void hoverChanged(bool) override { update(); }

    Role accessibleRole() const override { return Role::ComboBox; }
    bool accessibleHasValue() const override { return true; }
    std::string accessibleValue() const override { return currentText(); }
    bool accessibleSetValue(std::string_view v) override;
    bool accessibleReadOnly() const override { return false; }
    int accessibleExpandState() const override { return popup_ ? 1 : 0; }
    void accessibleExpand(bool open) override {
        if (open) showPopup();
        else hidePopup();
    }

    static constexpr float kArrowWidth = 20.0f;
    static constexpr float kPadding = 8.0f;
    static constexpr int kTypeaheadMs = 1000;

private:
    struct Item {
        std::string text;
        ComboData data;
    };
    void setCurrent(int i, bool notify);
    void choose(int row);          // the user picked: set, close, activated
    void userStep(int to);         // a key or the wheel changed the item
    void typeahead(const std::string& typed_utf8);
    int searchFrom(const std::string& prefix, int start, bool single_letter) const;
    void highlightChanged();       // announce the row

    std::vector<Item> items_;
    int current_ = -1;
    PopupHandle* popup_ = nullptr;
    ComboPopupList* list_ = nullptr;
    std::string typed_;
    TimerId typed_timer_ = 0;
};

}  // namespace tcad::ui

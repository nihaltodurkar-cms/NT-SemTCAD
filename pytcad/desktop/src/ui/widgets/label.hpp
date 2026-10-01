// Label (N3a, NATIVE-DESKTOP-PLAN.md 27.8.1): QLabel's measured subset -- plain text, one line or word-wrapped
// with height-for-width, a font size/weight and a text colour token, and a buddy. As in Qt, '&' is shown literally
// unless the label has a buddy; then "&Voltage" makes Alt+V focus the buddy. Not built (0 uses): rich text,
// alignment, pixmaps, links.
//
// SELECTABLE (N3d; Qt::TextSelectableByMouse, 1 use: the telemetry panel): setSelectable(true) makes the text selectable --
// a press puts the caret, a drag selects, a double click selects a word, Shift+click extends; Ctrl+A selects all and
// Ctrl+C copies through the host's clipboard. It takes the focus on a click (never on Tab: Qt's flag is the mouse's) and
// shows an I-beam. Not built (0 uses): keyboard selection, triple click, links. The selection is of the SHOWN text.
//
// Sizes: one line -> its measured text. Wrapped -> hint width min(text, Style::wrap_hint_ems em), minimum width the
// longest word, and the height from heightForWidth (layouts that know the width use it; layout.hpp).
#pragma once

#include "ui/core/style.hpp"
#include "ui/core/widget.hpp"
#include "ui/widgets/mnemonic.hpp"

#include "ui/core/edit_model.hpp"

#include "ui/widgets/edit_context_menu.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tcad::ui {

class Label : public Widget {
public:
    explicit Label(std::string text = {});

    void setText(std::string text);
    const std::string& text() const { return raw_; }       // as set
    const std::string& shownText() const { return shown_.text; }
    void setWordWrap(bool on);
    bool wordWrap() const { return wrap_; }
    // Alt+<the '&' letter> focuses `buddy`. The buddy is looked up in the tree at activation, so a buddy that was
    // destroyed or removed is never dereferenced.
    void setBuddy(Widget* buddy);
    Widget* buddy() const { return buddy_; }
    void setFont(float size, bool bold = false);
    void setColor(tcad::desktop::theme::T token);

    void setSelectable(bool on);
    bool selectable() const { return model_ != nullptr; }
    std::string selectedText() const { return model_ ? model_->selectedText() : std::string(); }
    void selectAll();
    void setSelection(std::size_t from, std::size_t to);  // byte offsets of shownText()
    std::pair<std::size_t, std::size_t> selection() const { return model_ ? model_->selection() : std::pair<std::size_t, std::size_t>{0, 0}; }
    bool copy();  // the selection to the clipboard; false when there is none

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return wrap_; }
    float heightForWidth(float width) const override;
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::Text; }
    void activateMnemonic() override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    void focusChanged(bool, FocusReason) override { update(); }

private:
    void refresh();
    TextStyle style() const;
    float textTop() const;                       // DIPs from the top: where the centred text block starts
    std::size_t offsetAt(PointF local) const;
    std::vector<RectF> selectionRects() const;   // in widget DIPs

    std::string raw_;
    MnemonicText shown_;
    bool wrap_ = false;
    Widget* buddy_ = nullptr;
    std::unique_ptr<EditModel> model_;  // read-only; present when selectable
    std::unique_ptr<EditContextMenu> context_menu_;  // N3f: Copy and Select All, made on the first right-click
    float size_ = Style::standard().font_size;
    bool bold_ = false;
    tcad::desktop::theme::T color_ = tcad::desktop::theme::T::Text;
};

}  // namespace tcad::ui

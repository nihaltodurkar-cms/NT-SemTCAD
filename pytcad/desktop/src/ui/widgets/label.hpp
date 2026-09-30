// Label (N3a, NATIVE-DESKTOP-PLAN.md 27.8.1): QLabel's measured subset -- plain text, one line or word-wrapped
// with height-for-width, a font size/weight and a text colour token, and a buddy. As in Qt, '&' is shown literally
// unless the label has a buddy; then "&Voltage" makes Alt+V focus the buddy. Not built (0 uses): rich text,
// alignment, pixmaps, links. A selectable label (1 use) comes with the N3d edits.
//
// Sizes: one line -> its measured text. Wrapped -> hint width min(text, Style::wrap_hint_ems em), minimum width the
// longest word, and the height from heightForWidth (layouts that know the width use it; layout.hpp).
#pragma once

#include "ui/core/style.hpp"
#include "ui/core/widget.hpp"
#include "ui/widgets/mnemonic.hpp"

#include <string>

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

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return wrap_; }
    float heightForWidth(float width) const override;
    void paint(Painter& p) override;
    Role accessibleRole() const override { return Role::Text; }
    void activateMnemonic() override;

private:
    void refresh();
    TextStyle style() const;

    std::string raw_;
    MnemonicText shown_;
    bool wrap_ = false;
    Widget* buddy_ = nullptr;
    float size_ = Style::standard().font_size;
    bool bold_ = false;
    tcad::desktop::theme::T color_ = tcad::desktop::theme::T::Text;
};

}  // namespace tcad::ui

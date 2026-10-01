// QMessageBox (N3f, NATIVE-DESKTOP-PLAN.md 27.8.7): 12 in the panels, in two shapes -- the MODAL question that blocks the close of
// a project with unsaved changes (Save / Discard / Cancel, Save the default, Warning-less "Question" icon) and the NON-MODAL
// warning that reports an error and goes away (Ok). What they call: the icon, a title, the text, standardButtons, the default
// button, exec() for the modal one and show() for the other, the object name for the end-to-end tests. Not used, so not built:
// custom (non-standard) buttons, informative or detailed text, a check box, a pixmap icon, a text selectable by mouse.
//
// THE CONTENT (this file, portable): a Widget with the icon, the text and a row of PushButtons, ready to sit in a dialog window
// (ui/win32/message_box_window.hpp). Behaviour (Windows' message box, which Qt matches):
//   * the buttons are right-aligned, in the order given, 6 DIPs apart, at least 80 wide; the default one has the accent ring and
//     takes the focus when the box opens; Enter presses the focused button, or the default one when the focus is elsewhere;
//   * Escape presses the ESCAPE button: the one named, else Cancel, else the only button, else No, else Close; with none of
//     those Escape does nothing (a box whose only choices are Yes and No cannot be dismissed by accident);
//   * Alt+<letter> presses the button with that mnemonic; Left/Right and Tab move between the buttons; Ctrl+C copies the
//     text (the title, a rule, the text, a rule, the button labels: what Windows' own message box copies);
//   * on_finished(button) fires once, with the button that was pressed.
// The icon is drawn, not a bitmap: a filled disc with a glyph -- "i" (Information), "!" (Warning), "x" (Critical), "?" (Question).
#pragma once

#include "ui/core/widget.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tcad::ui {

enum class StandardButton { None, Ok, Cancel, Yes, No, Save, Discard, Close };
enum class MessageIcon { None, Information, Warning, Critical, Question };

struct MessageBoxSpec {
    MessageIcon icon = MessageIcon::None;
    std::string title, text;
    std::vector<StandardButton> buttons{StandardButton::Ok};
    StandardButton default_button = StandardButton::None;  // None: the first
    StandardButton escape_button = StandardButton::None;   // None: the rule above
};

const char* standardButtonText(StandardButton b);  // with its '&'

class MessageBoxContent : public Widget {
public:
    std::function<void(StandardButton)> on_finished;

    explicit MessageBoxContent(MessageBoxSpec spec);

    const MessageBoxSpec& spec() const { return spec_; }
    PushButton* button(StandardButton b) const;
    PushButton* defaultButton() const { return button(default_); }
    StandardButton defaultButtonId() const { return default_; }
    StandardButton escapeButtonId() const { return escape_; }
    Label* textLabel() const { return label_; }
    bool finished() const { return finished_; }
    void press(StandardButton b);  // as a click on it (once)

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override { return sizeHint(); }
    bool hasHeightForWidth() const override { return false; }
    void paint(Painter& p) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    Role accessibleRole() const override { return Role::Dialog; }

    std::string copyText() const;

    static constexpr float kMargin = 18.0f;
    static constexpr float kIconSize = 34.0f;
    static constexpr float kGap = 14.0f;
    static constexpr float kTextWidth = 360.0f;
    static constexpr float kButtonGap = 6.0f;
    static constexpr float kButtonHeight = 26.0f;
    static constexpr float kMinButton = 80.0f;

protected:
    void resized() override { layoutParts(); }

private:
    void layoutParts();
    float textHeight() const;
    MessageBoxSpec spec_;
    StandardButton default_ = StandardButton::None;
    StandardButton escape_ = StandardButton::None;
    Label* label_ = nullptr;
    std::vector<std::pair<StandardButton, PushButton*>> buttons_;
    bool finished_ = false;
};

}  // namespace tcad::ui

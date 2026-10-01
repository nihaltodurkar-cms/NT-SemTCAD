// What LineEdit and PlainTextEdit share (N3d, NATIVE-DESKTOP-PLAN.md 27.8.5): the EditModel, drawn and driven by a
// widget -- TSF (IME, emoji panel, dictation), the clipboard, the shared keys, the mouse selection, the caret blink and
// UI Automation's value. The subclass owns the geometry: how text is laid out, scrolled and painted.
//
//   keys:  Left/Right (+Ctrl by word, +Shift to select), Home/End, Backspace/Delete (+Ctrl by word), Ctrl+A/C/X/V/Z/Y
//          (and Ctrl+Shift+Z, Ctrl+Insert, Shift+Insert, Shift+Delete); these claim the key ahead of window shortcuts.
//   mouse: a press puts the caret at the hit cluster (Shift extends), a double click selects the word, a drag extends
//          the selection; the right button is the context menu's (N3f).
//   IME:   a TSF document with this edit's TextStore gets the TSF focus while the edit has the focus; losing it moves
//          the TSF focus to an empty document, so an IME never types into an edit that is not focused.
// The caret blinks at the system rate (GetCaretBlinkTime). The selection stays visible while the edit does not have
// the focus (ui/core/selection.hpp).
#pragma once

#include "ui/core/edit_model.hpp"
#include "ui/core/widget.hpp"
#include "ui/render/render_device.hpp"
#include "ui/widgets/edit_context_menu.hpp"
#include "ui/win32/tsf_text_store.hpp"

#include <msctf.h>
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tcad::ui {

class TextInput : public Widget {
public:
    ~TextInput() override;

    EditModel& model() { return model_; }
    const EditModel& model() const { return model_; }
    const std::string& text() const { return model_.text(); }
    void setText(std::string_view utf8);  // QLineEdit::setText: a program's change, no undo, caret at the end
    void setReadOnly(bool r);
    bool isReadOnly() const { return model_.readOnly(); }
    void setPlaceholderText(std::string s);  // shown, faint, while the text is empty (QLineEdit/QPlainTextEdit)
    const std::string& placeholderText() const { return placeholder_; }

    std::function<void()> on_text_changed;
    std::function<void(bool, FocusReason)> on_focus_changed;  // after the edit's own reaction (N3b: a spin box's)
    std::function<void()> on_selection_changed;

    TextStore* textStore() const { return store_; }
    bool tsfReady() const { return doc_ != nullptr; }
    ITfThreadMgr* tsfThreadManager() const { return tsf_.Get(); }
    ITfDocumentMgr* tsfDocument() const { return doc_.Get(); }
    void setCaretBlinking(bool on);  // tests: false = the caret stays visible (deterministic pixels)
    bool caretVisible() const { return caret_on_; }

    // Screen geometry of the text (TSF and UI Automation): a range's rectangle (the union of its lines), the pieces
    // of it, the edit's, and a hit test.
    RECT rangeScreenRect(std::size_t u8_start, std::size_t u8_end) const;
    std::vector<RECT> rangeScreenRects(std::size_t u8_start, std::size_t u8_end) const;
    RECT screenRect() const;
    std::size_t offsetAtScreen(POINT screen) const;
    // The lines a range covers, for UI Automation's Line and Paragraph units: the hard line containing `p`.
    virtual std::pair<std::size_t, std::size_t> lineRangeAt(std::size_t p) const { return {model_.lineStart(p), model_.lineEnd(p)}; }

    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool charEvent(char32_t c) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    void focusChanged(bool in, FocusReason why) override;

    // Accessibility (N2f): an edit whose value is its text.
    Role accessibleRole() const override { return Role::Edit; }
    bool accessibleHasValue() const override { return true; }
    std::string accessibleValue() const override { return model_.text(); }
    bool accessibleSetValue(std::string_view v) override;
    bool accessibleReadOnly() const override { return model_.readOnly(); }
    std::function<void()> on_accessible_value_changed;  // set by the window's UIA host

    static constexpr float kPadding = 4.0f;

protected:
    TextInput(HWND window, bool multi_line);

    HWND hwnd() const { return hwnd_; }
    virtual TextStyle style() const;
    // The rectangles (widget DIPs, scroll applied) a range occupies; a collapsed range gives its caret's rectangle.
    virtual std::vector<RectF> rangeRects(std::size_t a, std::size_t b) const = 0;
    virtual std::size_t offsetAt(PointF local) const = 0;  // a widget-local point -> the nearest caret stop
    virtual void ensureCaretVisible() = 0;
    virtual void contentChanged() {}                       // the text changed (a relayout)
    virtual void textSet() {}                              // the program replaced the text (scroll back to the start)
    virtual bool enterPressed() = 0;                       // true: the key was used
    virtual bool contextMenuRequested(PointF local);  // N3f: the edit menu (Undo, Cut, Copy, Paste, Delete, Select All) at `local`
    virtual bool dragOutside(const UiMouseEvent&) { return false; }
    // Called after any user or program change of text or selection.
    void changed(std::size_t old_u16_len);   // after any edit this widget made
    void selectionMoved();                   // after a move or selection change
    // After the PROGRAM changed the text (a log's append): everything `changed` does except bringing the caret into view
    // and restarting the blink -- the view stays where the reader put it.
    void changedByProgram(std::size_t old_u16_len);
    std::size_t u16Length() const { return store_->u16Length(); }
    void restartBlink();
    float lineHeight() const;
    virtual void focusLost() {}                            // after the edit's own reaction, before on_focus_changed
    virtual bool beforeKey(const platform::KeyEvent&) { return false; }  // a subclass's keys, tried first (true: used)
    bool selectionActive() const { return hasFocus(); }  // the selection draws active only while focused

    EditModel model_;
    std::string placeholder_;

private:
    void setupTsf();
    void runMenuCommand(EditContextMenu::Command c);
    std::unique_ptr<EditContextMenu> context_menu_;

    HWND hwnd_;
    TextStore* store_ = nullptr;
    ComPtr<ITfThreadMgr> tsf_;
    TfClientId client_ = TF_CLIENTID_NULL;
    ComPtr<ITfDocumentMgr> doc_, blank_doc_;
    ComPtr<ITfContext> ctx_;
    TfEditCookie cookie_ = 0;
    bool caret_on_ = true;
    bool blinking_ = true;
    TimerId blink_ = 0;
};

}  // namespace tcad::ui

// The bare single-line edit of N2e (NATIVE-DESKTOP-PLAN.md 27.7): the EditModel, drawn and driven -- enough to
// exercise text input end to end (keys, characters, the mouse, the clipboard, TSF) before N3's LineEdit builds on it.
//
//   keys:  Left/Right (+Ctrl by word, +Shift to select), Home/End, Backspace/Delete (+Ctrl by word), Ctrl+A/C/X/V/Z/Y
//          (and Ctrl+Shift+Z); these claim the key ahead of window shortcuts. Enter and losing focus report
//          on_editing_finished (QLineEdit::editingFinished, the signal the panels use: 21 connections).
//   mouse: a press puts the caret at the hit cluster (Shift extends), a drag extends the selection.
//   IME:   a TSF document with this edit's TextStore gets the TSF focus while the edit has the focus; losing it moves
//          the TSF focus to an empty document, so an IME never types into an edit that is not focused.
//
// Horizontal scrolling keeps the caret visible. The caret blinks at the system rate (GetCaretBlinkTime).
#pragma once

#include "ui/core/edit_model.hpp"
#include "ui/core/widget.hpp"
#include "ui/render/render_device.hpp"
#include "ui/win32/tsf_text_store.hpp"

#include <msctf.h>
#include <windows.h>

#include <functional>
#include <string>

namespace tcad::ui {

class LineEdit : public Widget {
public:
    explicit LineEdit(HWND window);  // the window it lives in: screen geometry for TSF, the clipboard owner
    ~LineEdit() override;

    EditModel& model() { return model_; }
    const std::string& text() const { return model_.text(); }
    void setText(std::string_view utf8);
    std::function<void()> on_editing_finished;
    std::function<void()> on_text_changed;

    TextStore* textStore() const { return store_; }
    bool tsfReady() const { return doc_ != nullptr; }
    ITfThreadMgr* tsfThreadManager() const { return tsf_.Get(); }
    ITfDocumentMgr* tsfDocument() const { return doc_.Get(); }
    void setCaretBlinking(bool on);  // tests: false = the caret stays visible (deterministic pixels)

    SizeF sizeHint() const override;
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;
    bool keyEvent(const platform::KeyEvent& e) override;
    bool charEvent(char32_t c) override;
    bool overridesShortcut(const platform::KeyEvent& e) const override;
    void focusChanged(bool in, FocusReason why) override;

    static constexpr float kPadding = 4.0f;

    // Screen geometry of the text (TSF and UI Automation): a range's rectangle, the edit's, and a hit test.
    RECT rangeScreenRect(std::size_t u8_start, std::size_t u8_end) const;
    RECT screenRect() const;
    std::size_t offsetAtScreen(POINT screen) const;

    // Accessibility (N2f): an edit whose value is its text.
    Role accessibleRole() const override { return Role::Edit; }
    bool accessibleHasValue() const override { return true; }
    std::string accessibleValue() const override { return model_.text(); }
    bool accessibleSetValue(std::string_view v) override;
    bool accessibleReadOnly() const override { return model_.readOnly(); }
    std::function<void()> on_accessible_value_changed;  // set by the window's UIA host

protected:
    // A text set before the first layout scrolled against a zero width: every resize re-checks the scroll
    // (found by the N2f high-contrast golden, which showed only the last character of the text).
    void resized() override { ensureCaretVisible(); }

private:
    TextStyle style() const;
    float lineHeight() const;
    float textTop() const;  // DIPs from the widget top to the text's first line
    std::size_t offsetAt(PointF local) const;
    float caretX(std::size_t offset) const;  // in text coordinates (before scrolling)
    void changed(std::size_t old_u16_len);   // after any edit this widget made
    void ensureCaretVisible();
    void restartBlink();
    void setupTsf();

    HWND hwnd_;
    EditModel model_;
    TextStore* store_ = nullptr;
    ComPtr<ITfThreadMgr> tsf_;
    TfClientId client_ = TF_CLIENTID_NULL;
    ComPtr<ITfDocumentMgr> doc_, blank_doc_;
    ComPtr<ITfContext> ctx_;
    TfEditCookie cookie_ = 0;
    float scroll_ = 0;
    bool caret_on_ = true;
    bool blinking_ = true;
    TimerId blink_ = 0;
};

}  // namespace tcad::ui

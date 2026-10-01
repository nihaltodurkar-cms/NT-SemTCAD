// QLineEdit (N2e, completed in N3d; NATIVE-DESKTOP-PLAN.md 27.7 and 27.8.5): the single-line edit on TextInput's keys,
// mouse, clipboard and TSF, with the QLineEdit parts the panels use (39 of them, measured 2026-10-01):
//   * editingFinished (21 connections) -- on_editing_finished -- on Enter or when the focus leaves, as Qt, but not while
//     a validator calls the text Intermediate or Invalid; returnPressed (1) -- on_return_pressed -- on Enter, same gate;
//   * validators (5 uses, all one scientific-notation QDoubleValidator): setValidator. An edit that would make the text
//     Invalid is refused (the key does nothing; a paste that would is refused whole); setText bypasses it, as in Qt;
//   * setReadOnly (5), setPlaceholderText (2), setText/text.
// DECISION 4 of 27.8: on_commit is the signal a port should prefer to on_editing_finished -- it fires on Enter or focus
// loss only when the text now differs from what it was at the last setText or commit, and is acceptable. A panel that
// relied on Qt's "also fires with no change" is checked while it is ported (N5). on_editing_finished stays Qt's (the
// spin boxes use it, and filter by value themselves).
// Not built (0 uses): echo modes, input masks, completers, a maximum length, a clear button.
// Horizontal scrolling keeps the caret visible. The double click selects a word. The inactive selection stays visible.
#pragma once

#include "ui/widgets/validator.hpp"
#include "ui/win32/text_input.hpp"

#include <functional>
#include <memory>
#include <string>

namespace tcad::ui {

class LineEdit : public TextInput {
public:
    explicit LineEdit(HWND window);  // the window it lives in: screen geometry for TSF, the clipboard owner
    ~LineEdit() override;

    std::function<void()> on_editing_finished;
    std::function<void()> on_return_pressed;
    std::function<void()> on_commit;

    // The text is not acceptable: a two-DIP error-coloured frame (N3b; Qt's spin boxes show no such state -- they
    // silently fix the text up, which decision 1 of 27.8 forbids).
    void setInvalid(bool on);
    bool isInvalid() const { return invalid_; }

    void setValidator(std::shared_ptr<const Validator> v);
    const Validator* validator() const { return validator_.get(); }
    bool hasAcceptableInput() const;  // no validator: true

    // Tab and Shift+Tab are keys for this edit (they reach the parent as key events) instead of moving the focus: an
    // in-place editor reports them to its view (N3e).
    void setCaptureTab(bool on) { capture_tab_ = on; }
    bool wantsTabKey(bool) const override { return capture_tab_; }

    SizeF sizeHint() const override;
    void paint(Painter& p) override;

protected:
    // A text set before the first layout scrolled against a zero width: every resize re-checks the scroll
    // (found by the N2f high-contrast golden, which showed only the last character of the text).
    void resized() override { ensureCaretVisible(); }
    std::vector<RectF> rangeRects(std::size_t a, std::size_t b) const override;
    std::size_t offsetAt(PointF local) const override;
    void ensureCaretVisible() override;
    void textSet() override;
    bool enterPressed() override;
    void focusLost() override;

private:
    float textTop() const;                   // DIPs from the widget top to the text's first line
    float caretX(std::size_t offset) const;  // in text coordinates (before scrolling)
    void finish(bool enter);

    std::shared_ptr<const Validator> validator_;
    std::string committed_;  // the text at the last setText or commit
    float scroll_ = 0;
    bool invalid_ = false;
    bool capture_tab_ = false;
};

}  // namespace tcad::ui

// QPlainTextEdit (N3d, NATIVE-DESKTOP-PLAN.md 27.8.5): the multi-line edit, on TextInput's keys, mouse, clipboard and
// TSF, with the parts the panels use (15 of them, measured 2026-10-01): three read-only views and ONE capped log.
//   * setPlainText (3), setReadOnly, setPlaceholderText (1: the netlist view), setLineWrapMode NoWrap (1: the console),
//     a monospace font (1: the console), setMaximumBlockCount (3: 200 lines, 200, the console's), the vertical scroll
//     bar's value and maximum (1: "was the view at the end?"), and per-line colour/weight/italic (the console's styled
//     blocks). Not used, so not built: rich text, find, document margins, tab stops, cursor-width, a text cursor object.
//   * LINES. The text is hard lines (line feeds), each laid out by the text engine on its own -- so each can have its own
//     LineFormat, and only the visible ones are measured and drawn. A wrapped line is several visual lines.
//   * appendLine(text, format): adds a line (the first one fills an empty view); trims the OLDEST lines past the maximum
//     (droppedLines() counts them); with setFollowEnd(true) (the console) the view stays at the end if it was at the end
//     -- within 2 DIPs, the Qt console's own test -- and stays where the reader put it otherwise. The program's changes
//     never move the view, the caret or the selection (the selection moves with the text it was on).
//   * setPlainText resets: caret and view to the start, formats cleared, dropped count zero (Qt).
//   * A maximum line count is applied by appendLine and setPlainText (a program's changes), not to typing: the capped
//     views are read-only. A user edit that changes the NUMBER of lines resets the per-line formats to the default
//     (they are for logs; an editable styled document is not built).
//   * Keys: the shared edit keys, plus Up/Down by visual line (the caret keeps its column), PageUp/PageDown by a view,
//     Home/End by visual line, Ctrl+Home/End, Enter (a line feed; the key goes on when read-only). Tab MOVES FOCUS
//     (tabChangesFocus; there is no editable code view in the panels). The wheel scrolls three lines a notch.
//   * Scroll bars (ui/widgets/scroll_bar.hpp) appear when the content does not fit; the horizontal one only without
//     wrapping. A drag-selection beyond the view scrolls it.
// UI Automation: Edit with Value (the whole text) and Text (document, selection, ranges by character, word, line and
// paragraph -- a hard line; with wrapping the "line" unit is the whole paragraph, a stated limit), and the two bars
// as children (RangeValue).
#pragma once

#include "theme/tokens.hpp"
#include "ui/widgets/scroll_bar.hpp"
#include "ui/win32/text_input.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tcad::ui {

struct LineFormat {
    tcad::desktop::theme::T color = tcad::desktop::theme::T::Text;
    bool bold = false;
    bool italic = false;
    bool operator==(const LineFormat&) const = default;
};

class PlainTextEdit : public TextInput {
public:
    explicit PlainTextEdit(HWND window);
    ~PlainTextEdit() override;

    void setPlainText(std::string_view utf8) { setText(utf8); }  // QPlainTextEdit::setPlainText: the view starts over
    std::string toPlainText() const { return text(); }
    void appendLine(std::string_view line, LineFormat format = {});
    void clear();
    int lineCount() const { return line_count_; }  // QPlainTextEdit::blockCount: 1 when empty
    std::string lineText(int i) const;             // "" out of range
    const LineFormat& lineFormat(int i) const;

    void setMaximumLineCount(int n);  // 0: unlimited
    int maximumLineCount() const { return max_lines_; }
    std::int64_t droppedLines() const { return dropped_; }
    void setWordWrap(bool on);
    bool wordWrap() const { return wrap_; }
    void setMonospace(bool on);
    bool monospace() const { return mono_; }
    void setFollowEnd(bool on) { follow_ = on; }
    bool followEnd() const { return follow_; }

    ScrollBar* verticalScrollBar() const { return vbar_; }
    ScrollBar* horizontalScrollBar() const { return hbar_; }
    bool atEnd() const;  // the view shows the last line (within 2 DIPs)
    void scrollToEnd();
    void scrollToStart();
    int scrollY() const { return vbar_->value(); }
    int scrollX() const { return hbar_->value(); }
    RectF viewportRect() const;  // widget DIPs: inside the frame and the bars
    float contentHeight() const;
    int firstVisibleLine() const;  // hard line at the top of the view
    RectF caretRectInWidget() const;

    SizeF sizeHint() const override;
    SizeF minimumSizeHint() const override;
    void paint(Painter& p) override;
    bool mouseEvent(const UiMouseEvent& e) override;

    // UI Automation's Scroll pattern over the two bars
    AccessibleScroll accessibleScroll() const override;
    void accessibleScrollBy(int h_amount, int v_amount) override;
    void accessibleSetScrollPercent(double h, double v) override;

    static constexpr float kPadV = 3.0f;
    static constexpr int kDragScrollMs = 60;

protected:
    TextStyle style() const override;
    std::vector<RectF> rangeRects(std::size_t a, std::size_t b) const override;
    std::size_t offsetAt(PointF local) const override;
    void ensureCaretVisible() override;
    void contentChanged() override;
    void textSet() override;
    bool enterPressed() override;
    bool beforeKey(const platform::KeyEvent& e) override;
    bool dragOutside(const UiMouseEvent& e) override;
    void resized() override;

private:
    struct Line {
        std::size_t start = 0, end = 0;  // the line's bytes; `end` is at the line feed (or the text's end)
        float top = 0, height = 0, width = 0;
    };
    TextStyle lineStyle(int i) const;
    float wrapWidth() const;                // DIPs the text wraps at (0 when not wrapping)
    void layout() const;                    // lazily: lines_, content size
    int lineOf(std::size_t offset) const;   // the hard line containing the byte offset
    std::string_view lineView(int i) const;
    std::vector<RectF> selectionRectsDoc(std::size_t a, std::size_t b) const;  // document coordinates
    RectF caretRectDoc(std::size_t offset) const;
    std::size_t offsetAtDoc(PointF doc) const;
    void updateBars();                      // visibility, geometry and ranges of the scroll bars
    void moveVertical(bool down, bool page, bool extend);
    void visualHomeEnd(bool end, bool extend);
    void setScroll(float x, float y);
    void trimToMaximum();
    void stopDragScroll();

    ScrollBar* vbar_ = nullptr;
    ScrollBar* hbar_ = nullptr;
    std::vector<LineFormat> formats_{LineFormat{}};
    bool wrap_ = true;
    bool mono_ = false;
    bool follow_ = false;
    bool pristine_ = true;  // empty and nothing appended yet: the next appendLine fills the first line
    int max_lines_ = 0;
    std::int64_t dropped_ = 0;
    int line_count_ = 1;
    float view_w_ = 0, view_h_ = 0;  // the viewport, DIPs
    bool in_update_bars_ = false;
    mutable std::vector<Line> lines_;
    mutable bool dirty_ = true;
    mutable float laid_out_at_ = -1;
    mutable float content_h_ = 0, content_w_ = 0;
    float goal_x_ = 0;
    bool goal_valid_ = false;
    PointF drag_pos_;
    TimerId drag_timer_ = 0;
};

}  // namespace tcad::ui

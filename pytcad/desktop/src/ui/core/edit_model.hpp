// The text-editing model under every edit widget (N2e, NATIVE-DESKTOP-PLAN.md 27.7). Portable: no Win32.
//
// SCOPE, measured in desktop/src on 2026-09-30: QLineEdit 39 (editingFinished 21, validators 5, setReadOnly 3,
// setSelection 2), QPlainTextEdit 14 (setPlainText 2, setMaximumBlockCount 2 -- consoles). NOT used, so not built:
// echo modes, input masks, completers, max length. (The 9 undo()/7 redo() calls are the document UndoStack.)
//
// Text is UTF-8; every position is a byte offset on a CARET STOP -- a cluster boundary from the text engine
// (TextEngine::caretStops), so a caret never splits a combining sequence, a conjunct, a surrogate pair or an emoji
// ZWJ sequence. The selection is [min(anchor, caret), max(...)).
//
// Editing: typing replaces the selection; Backspace/Delete remove the selection or one cluster (a word with Ctrl);
// words are runs of letters, digits and '_' (every non-ASCII code point counts as a letter), and Ctrl+Left/Right
// move to word starts / word ends as Windows edit controls do. A single-line model turns line breaks into spaces.
// Undo/redo record every change; consecutive typed characters at the caret merge into one step, and any move,
// selection change or different kind of edit starts a new one. Read-only models move and select but never change.
//
// IME: the composition range (TSF, N2e) marks the text being composed; replaceRange() is the one primitive a text
// store needs, and it records undo like any other edit.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tcad::ui {

class EditModel {
public:
    using Stops = std::function<std::vector<std::size_t>(std::string_view)>;
    // `stops`: the caret stops of a text (the text engine's clusters); default: every code point.
    explicit EditModel(Stops stops = {}, bool multi_line = false);

    const std::string& text() const { return text_; }
    std::size_t caret() const { return caret_; }
    std::size_t anchor() const { return anchor_; }
    bool hasSelection() const { return caret_ != anchor_; }
    std::pair<std::size_t, std::size_t> selection() const;  // ordered
    std::string selectedText() const;
    bool multiLine() const { return multi_line_; }
    void setReadOnly(bool r) { read_only_ = r; }
    bool readOnly() const { return read_only_; }
    int revision() const { return revision_; }  // increments on every change of the text

    // Programmatic: replaces everything, caret at the end, undo history cleared (QLineEdit::setText).
    void setText(std::string_view utf8);
    void setSelection(std::size_t anchor, std::size_t caret);  // snapped to caret stops
    void selectAll();

    // Moves; `extend` keeps the anchor (Shift).
    void moveLeft(bool word, bool extend);
    void moveRight(bool word, bool extend);
    void home(bool extend);
    void end(bool extend);

    // Edits (false when nothing changed: read-only, or nothing to delete).
    // Replaces the selection. `typing`: a character the user typed (merges with the typing before it); a paste or a
    // programmatic insert is its own undo step.
    bool insert(std::string_view utf8, bool typing = false);
    bool backspace(bool word);
    bool deleteForward(bool word);
    bool cut(std::string* out);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // The primitive under all edits (and the TSF text store's SetText): replace [start, end) by `utf8`, caret after it.
    bool replaceRange(std::size_t start, std::size_t end, std::string_view utf8, bool typing = false);

    // A veto on edits (N3d: a validator). Called with the text an edit WOULD produce; false refuses the edit (nothing
    // changes, replaceRange returns false). Not consulted by setText, forceReplace, undo or redo: they put back text that
    // was accepted, or that the program chose.
    void setFilter(std::function<bool(const std::string& candidate)> f) { filter_ = std::move(f); }

    // The program's edit, for logs (N3d): replaces [start, end) even in a read-only model, is NOT an undo step, and
    // clears the undo history (the recorded steps would point at text that moved). The caret and anchor keep their
    // place in the text: after the range they shift by the length change; inside it they go to its start.
    void forceReplace(std::size_t start, std::size_t end, std::string_view utf8);

    // The line (the text between line feeds) containing p: [start, end), end before the line feed.
    std::size_t lineStart(std::size_t p) const;
    std::size_t lineEnd(std::size_t p) const;

    // IME composition: the range being composed (empty when none).
    void setComposition(std::size_t start, std::size_t end) { comp_ = {start, end}; }
    void clearComposition() { comp_ = {0, 0}; }
    std::pair<std::size_t, std::size_t> composition() const { return comp_; }
    bool composing() const { return comp_.first != comp_.second; }

    std::vector<std::size_t> stops() const { return stops_(text_); }
    std::size_t snap(std::size_t offset) const;  // the caret stop at or before `offset`
    std::size_t prevStop(std::size_t p) const;    // the caret stop before p (0 at the start)
    std::size_t nextStop(std::size_t p) const;    // the caret stop after p (size() at the end)
    // The word containing p ([start, end)); where p is not in a word, the one cluster at p.
    std::pair<std::size_t, std::size_t> wordAt(std::size_t p) const;

private:
    struct Step {
        std::size_t start;
        std::string removed, inserted;
        std::size_t caret_before, anchor_before;
        bool typing;
    };
    std::size_t prevWord(std::size_t p) const;
    std::size_t nextWord(std::size_t p) const;
    void moveTo(std::size_t p, bool extend);

    Stops stops_;
    std::function<bool(const std::string&)> filter_;
    bool multi_line_;
    bool read_only_ = false;
    std::string text_;
    std::size_t caret_ = 0, anchor_ = 0;
    std::pair<std::size_t, std::size_t> comp_{0, 0};
    std::vector<Step> undo_, redo_;
    bool coalesce_ = false;  // the last step was typing and nothing moved since
    int revision_ = 0;
};

}  // namespace tcad::ui

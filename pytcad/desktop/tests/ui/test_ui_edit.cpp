// Portable tests of N2e's EditModel (edit_model.hpp): caret stops by cluster, selection, word moves and deletes,
// undo coalescing, read-only, single vs multi-line, programmatic setText/setSelection, composition. Part of
// tcad_ui_core_tests: no Win32. The Win32 side (clipboard, TSF text store, the bare edit) is test_ui_edit_win32.cpp.
#include "mini_test.hpp"

#include "ui/core/edit_model.hpp"

#include <string>
#include <vector>

using namespace tcad::ui;

namespace {

// Caret stops like a text engine's clusters: a combining mark (U+0300..U+036F) belongs to the character before it.
std::vector<std::size_t> clusterStops(std::string_view s) {
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) == 0x80) continue;
        const bool combining = c == 0xCC || (c == 0xCD && static_cast<unsigned char>(s[i + 1]) < 0xB0);
        if (!combining) v.push_back(i);
    }
    v.push_back(s.size());
    return v;
}

const std::string kE = "e\xCC\x81";  // e + U+0301: one cluster, 3 bytes

void type(EditModel& m, std::string_view s) {
    for (char c : s) m.insert(std::string(1, c), true);
}

}  // namespace

TEST(typing_and_backspace_move_by_whole_clusters) {
    EditModel m(clusterStops);
    m.insert("a" + kE + "b");
    CHECK(m.text() == "a" + kE + "b" && m.caret() == 5);
    m.moveLeft(false, false);
    CHECK_EQ(m.caret(), std::size_t{4});
    m.moveLeft(false, false);  // over the whole e+acute
    CHECK_EQ(m.caret(), std::size_t{1});
    m.moveRight(false, false);
    CHECK(m.backspace(false));  // removes e AND its accent
    CHECK(m.text() == "ab" && m.caret() == 1);
    m.setSelection(2, 2);
    m.setSelection(0, 2);
    CHECK(m.selectedText() == "ab");
}

TEST(positions_snap_to_caret_stops) {
    EditModel m(clusterStops);
    m.setText("x" + kE);
    m.setSelection(2, 3);  // inside the cluster: both snap back to its start
    CHECK(m.anchor() == 1 && m.caret() == 1);
    CHECK_EQ(m.snap(100), std::size_t{4});
}

TEST(a_selection_is_replaced_by_typing_and_collapsed_by_arrows) {
    EditModel m;
    m.setText("hello world");
    m.setSelection(0, 5);
    m.moveRight(false, false);  // collapses to the right edge
    CHECK(!m.hasSelection() && m.caret() == 5);
    m.setSelection(7, 2);
    m.moveLeft(false, false);  // and Left to the left edge (not one cluster left of the caret)
    CHECK(!m.hasSelection() && m.caret() == 2);
    m.setSelection(6, 11);
    m.insert("there", true);
    CHECK_EQ(m.text(), std::string("hello there"));
    m.home(true);
    CHECK((m.selection() == std::pair<std::size_t, std::size_t>{0, 11}));
}

TEST(word_moves_and_deletes_follow_windows_edit_controls) {
    EditModel m;
    m.setText("alpha  beta_gamma, delta");
    m.home(false);
    m.moveRight(true, false);
    CHECK_EQ(m.caret(), std::size_t{5});  // end of "alpha"
    m.moveRight(true, false);
    CHECK_EQ(m.caret(), std::size_t{17});  // end of "beta_gamma" ('_' is a word character)
    m.end(false);
    m.moveLeft(true, false);
    CHECK_EQ(m.caret(), std::size_t{19});  // start of "delta"
    CHECK(m.backspace(true));              // Ctrl+Backspace: back to the start of "beta_gamma"
    CHECK_EQ(m.text(), std::string("alpha  delta"));
    m.home(false);
    CHECK(m.deleteForward(true));
    CHECK_EQ(m.text(), std::string("  delta"));
    m.setText("naïve café");  // non-ASCII letters are word characters
    m.home(false);
    m.moveRight(true, false);
    CHECK_EQ(m.caret(), std::size_t{6});
}

TEST(typing_coalesces_into_one_undo_step_until_something_else_happens) {
    EditModel m;
    type(m, "abc");
    CHECK(m.undo());
    CHECK(m.text().empty() && !m.canUndo());
    CHECK(m.redo());
    CHECK_EQ(m.text(), std::string("abc"));
    type(m, "d");
    m.moveLeft(false, false);  // a move breaks the run
    m.moveRight(false, false);
    type(m, "e");
    CHECK(m.undo());
    CHECK_EQ(m.text(), std::string("abcd"));
    CHECK(m.undo());
    CHECK_EQ(m.text(), std::string("abc"));
    m.insert("XY");  // a paste: its own step even right after typing
    type(m, "z");
    CHECK(m.undo());
    CHECK(m.undo());
    CHECK_EQ(m.text(), std::string("abc"));
    CHECK(m.backspace(false));
    CHECK(m.undo());
    CHECK(m.text() == "abc" && m.caret() == 3);  // undo restores the caret too
    type(m, "x");
    CHECK(!m.canRedo());  // a new edit drops the redo history
}

TEST(read_only_models_select_but_never_change) {
    EditModel m;
    m.setText("fixed");
    m.setReadOnly(true);
    CHECK(!m.insert("x", true) && !m.backspace(false) && !m.deleteForward(false) && !m.undo());
    std::string cut;
    m.selectAll();
    CHECK(!m.cut(&cut) && m.selectedText() == "fixed");
    CHECK_EQ(m.text(), std::string("fixed"));
}

TEST(single_line_models_flatten_line_breaks_and_multi_line_ones_keep_lines) {
    EditModel one;
    one.insert("a\nb\r\nc");
    CHECK_EQ(one.text(), std::string("a b  c"));
    EditModel many({}, true);
    many.setText("first\nsecond");
    many.setSelection(8, 8);  // in "second"
    many.home(false);
    CHECK_EQ(many.caret(), std::size_t{6});
    many.end(false);
    CHECK_EQ(many.caret(), std::size_t{12});
    many.setSelection(2, 2);
    many.end(false);
    CHECK_EQ(many.caret(), std::size_t{5});
}

TEST(set_text_clears_history_and_cut_returns_the_selection) {
    EditModel m;
    type(m, "abc");
    m.setText("new");
    CHECK(!m.canUndo() && m.caret() == 3);
    const int rev = m.revision();
    m.setSelection(0, 2);
    std::string out;
    CHECK(m.cut(&out) && out == "ne" && m.text() == "w");
    CHECK(m.revision() == rev + 1);
}

TEST(composition_marks_a_range_and_replace_range_is_the_primitive) {
    EditModel m;
    m.setText("ab");
    m.setComposition(1, 2);
    CHECK(m.composing());
    CHECK(m.replaceRange(1, 2, "XYZ"));
    CHECK(m.text() == "aXYZ" && m.caret() == 4);
    m.clearComposition();
    CHECK(!m.composing());
    CHECK(m.undo() && m.text() == "ab");
}

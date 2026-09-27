// C++ port of gui/services/undo_stack.py's Command/UndoStack (P4 S8,
// NATIVE-DESKTOP-PLAN.md section 20.4): same shape -- closures,
// redo-clear-on-push, a clean-index dirty flag. Qt-free (header-only,
// std::function), so it applies uniformly to every document S1-S7
// built (StructureDocument/MeshDocument/ProcessFlowDocument), none of
// which are Qt types.
//
// Snapshot commands (make_command below), not delta closures over a
// mutator call, are the concrete shape used here: every document this
// applies to already has a lossless parse(text)/dump() round trip (S1),
// and none of them carries a huge array (no solved fields -- see
// structure_document.hpp's own header comment), so a whole-document
// JSON snapshot is exactly the "small diff, never a huge array" shape
// undo_stack.py's own docstring asks for, just captured as "the whole
// (small) document" rather than a hand-written per-field diff. This
// also makes `apply()` idempotent (redo() re-applies the same "after"
// snapshot rather than re-running a mutator that might not be safe to
// call twice), which a generic helper needs to be usable for every
// mutator (add/remove/move/set) without a bespoke Command per call site.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tcad::desktop {

class Command {
public:
    Command(std::function<void()> apply_fn, std::function<void()> undo_fn, std::string description)
        : apply_fn_(std::move(apply_fn)), undo_fn_(std::move(undo_fn)),
          description_(std::move(description)) {}

    // Named apply(), not do(): `do` is a C++ keyword. Equivalent to
    // Python Command.do().
    void apply() const { apply_fn_(); }
    void undo() const { undo_fn_(); }
    const std::string& description() const { return description_; }

private:
    std::function<void()> apply_fn_;
    std::function<void()> undo_fn_;
    std::string description_;
};

class UndoStack {
public:
    void push(Command command) {
        command.apply();
        undo_.push_back(std::move(command));
        redo_.clear();
    }

    void undo() {
        if (undo_.empty()) return;
        Command c = std::move(undo_.back());
        undo_.pop_back();
        c.undo();
        redo_.push_back(std::move(c));
    }

    void redo() {
        if (redo_.empty()) return;
        Command c = std::move(redo_.back());
        redo_.pop_back();
        c.apply();
        undo_.push_back(std::move(c));
    }

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    std::size_t undo_count() const { return undo_.size(); }
    std::size_t redo_count() const { return redo_.size(); }

    void mark_clean() { clean_index_ = undo_.size(); }
    bool is_dirty() const { return undo_.size() != clean_index_; }

private:
    std::vector<Command> undo_;
    std::vector<Command> redo_;
    std::size_t clean_index_ = 0;
};

// Builds a Command that performs `mutate(doc)` once now and captures
// the document's `dump()` before and after, so apply()/undo() are pure
// snapshot restores via `Doc::parse` -- generic over any of S1's three
// document types (or any future one with the same parse/dump/assignment
// shape). Pushing the returned Command (UndoStack::push calls apply()
// immediately) is a harmless no-op re-application of the "after"
// snapshot the mutation already produced.
template <typename Doc, typename Mutate>
Command make_command(Doc& doc, std::string description, Mutate&& mutate) {
    std::string before = doc.dump();
    mutate(doc);
    std::string after = doc.dump();
    return Command(
        [&doc, after]() { doc = Doc::parse(after); },
        [&doc, before]() { doc = Doc::parse(before); },
        std::move(description));
}

}  // namespace tcad::desktop

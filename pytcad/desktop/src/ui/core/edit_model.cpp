#include "ui/core/edit_model.hpp"

#include <algorithm>

namespace tcad::ui {

namespace {

std::vector<std::size_t> codePointStops(std::string_view s) {
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) v.push_back(i);
    v.push_back(s.size());
    return v;
}

// A word character: ASCII letters, digits, '_', and every byte of a non-ASCII code point.
bool wordByte(unsigned char c) { return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'); }

}  // namespace

EditModel::EditModel(Stops stops, bool multi_line) : stops_(stops ? std::move(stops) : Stops(codePointStops)), multi_line_(multi_line) {}

std::pair<std::size_t, std::size_t> EditModel::selection() const { return {std::min(caret_, anchor_), std::max(caret_, anchor_)}; }

std::string EditModel::selectedText() const {
    const auto [a, b] = selection();
    return text_.substr(a, b - a);
}

std::size_t EditModel::snap(std::size_t off) const {
    const auto st = stops();
    off = std::min(off, text_.size());
    auto it = std::upper_bound(st.begin(), st.end(), off);
    return it == st.begin() ? 0 : *(it - 1);
}

std::size_t EditModel::prevStop(std::size_t p) const {
    const auto st = stops();
    auto it = std::lower_bound(st.begin(), st.end(), p);
    return it == st.begin() ? 0 : *(it - 1);
}

std::size_t EditModel::nextStop(std::size_t p) const {
    const auto st = stops();
    auto it = std::upper_bound(st.begin(), st.end(), p);
    return it == st.end() ? text_.size() : *it;
}

std::pair<std::size_t, std::size_t> EditModel::wordAt(std::size_t p) const {
    p = snap(p);
    auto at = [&](std::size_t i) { return static_cast<unsigned char>(text_[i]); };
    std::size_t s = p, e = p;
    while (s > 0 && wordByte(at(s - 1))) s = prevStop(s);
    while (e < text_.size() && wordByte(at(e))) e = nextStop(e);
    if (s == e) e = nextStop(p);
    return {s, e};
}

std::size_t EditModel::prevWord(std::size_t p) const {  // the start of the word before p (skipping separators)
    auto at = [&](std::size_t i) { return static_cast<unsigned char>(text_[i]); };
    while (p > 0 && !wordByte(at(p - 1))) p = prevStop(p);
    while (p > 0 && wordByte(at(p - 1))) p = prevStop(p);
    return p;
}

std::size_t EditModel::nextWord(std::size_t p) const {  // the end of the word after p (skipping separators)
    auto at = [&](std::size_t i) { return static_cast<unsigned char>(text_[i]); };
    while (p < text_.size() && !wordByte(at(p))) p = nextStop(p);
    while (p < text_.size() && wordByte(at(p))) p = nextStop(p);
    return p;
}

void EditModel::moveTo(std::size_t p, bool extend) {
    caret_ = p;
    if (!extend) anchor_ = p;
    coalesce_ = false;
}

void EditModel::setText(std::string_view utf8) {
    std::string t(utf8);
    if (!multi_line_) std::replace_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    text_ = std::move(t);
    caret_ = anchor_ = text_.size();
    undo_.clear();
    redo_.clear();
    coalesce_ = false;
    clearComposition();
    ++revision_;
}

void EditModel::setSelection(std::size_t anchor, std::size_t caret) {
    anchor_ = snap(anchor);
    caret_ = snap(caret);
    coalesce_ = false;
}

void EditModel::selectAll() { setSelection(0, text_.size()); }

void EditModel::moveLeft(bool word, bool extend) {
    if (hasSelection() && !extend && !word) return moveTo(selection().first, false);  // collapse to the left edge
    moveTo(word ? prevWord(caret_) : prevStop(caret_), extend);
}

void EditModel::moveRight(bool word, bool extend) {
    if (hasSelection() && !extend && !word) return moveTo(selection().second, false);
    moveTo(word ? nextWord(caret_) : nextStop(caret_), extend);
}

void EditModel::home(bool extend) {
    std::size_t p = caret_;
    if (multi_line_)
        while (p > 0 && text_[p - 1] != '\n') --p;
    else p = 0;
    moveTo(p, extend);
}

void EditModel::end(bool extend) {
    std::size_t p = caret_;
    if (multi_line_)
        while (p < text_.size() && text_[p] != '\n') ++p;
    else p = text_.size();
    moveTo(p, extend);
}

bool EditModel::replaceRange(std::size_t start, std::size_t end, std::string_view utf8, bool typing) {
    if (read_only_) return false;
    start = std::min(start, text_.size());
    end = std::clamp(end, start, text_.size());
    std::string ins(utf8);
    if (!multi_line_) std::replace_if(ins.begin(), ins.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    if (start == end && ins.empty()) return false;
    if (filter_) {
        std::string candidate = text_;
        candidate.replace(start, end - start, ins);
        if (!filter_(candidate)) return false;
    }
    Step step{start, text_.substr(start, end - start), ins, caret_, anchor_, typing};
    text_.replace(start, end - start, ins);
    caret_ = anchor_ = start + ins.size();
    redo_.clear();
    // typing merges into the previous typing step when it continues right where that one ended
    if (typing && coalesce_ && !undo_.empty() && undo_.back().typing && step.removed.empty() &&
        undo_.back().start + undo_.back().inserted.size() == start) {
        undo_.back().inserted += ins;
    } else {
        undo_.push_back(std::move(step));
    }
    coalesce_ = typing;
    ++revision_;
    return true;
}

void EditModel::forceReplace(std::size_t start, std::size_t end, std::string_view utf8) {
    start = std::min(start, text_.size());
    end = std::clamp(end, start, text_.size());
    std::string ins(utf8);
    if (!multi_line_) std::replace_if(ins.begin(), ins.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    auto moved = [&](std::size_t p) {
        if (p >= end) return p - (end - start) + ins.size();
        return std::min(p, start);  // inside the replaced range: its start
    };
    caret_ = moved(caret_);
    anchor_ = moved(anchor_);
    text_.replace(start, end - start, ins);
    undo_.clear();
    redo_.clear();
    coalesce_ = false;
    clearComposition();
    ++revision_;
}

std::size_t EditModel::lineStart(std::size_t p) const {
    p = std::min(p, text_.size());
    const std::size_t at = text_.rfind('\n', p == 0 ? std::string::npos : p - 1);
    return p == 0 || at == std::string::npos ? 0 : at + 1;
}

std::size_t EditModel::lineEnd(std::size_t p) const {
    p = std::min(p, text_.size());
    const std::size_t at = text_.find('\n', p);
    return at == std::string::npos ? text_.size() : at;
}

bool EditModel::insert(std::string_view utf8, bool typing) {
    const auto [a, b] = selection();
    return replaceRange(a, b, utf8, typing && a == b);  // typing over a selection is a replacement: its own step
}

bool EditModel::backspace(bool word) {
    if (hasSelection()) return replaceRange(selection().first, selection().second, "");
    if (caret_ == 0) return false;
    return replaceRange(word ? prevWord(caret_) : prevStop(caret_), caret_, "");
}

bool EditModel::deleteForward(bool word) {
    if (hasSelection()) return replaceRange(selection().first, selection().second, "");
    if (caret_ >= text_.size()) return false;
    return replaceRange(caret_, word ? nextWord(caret_) : nextStop(caret_), "");
}

bool EditModel::cut(std::string* out) {
    if (!hasSelection() || read_only_) return false;
    if (out) *out = selectedText();
    return replaceRange(selection().first, selection().second, "");
}

bool EditModel::undo() {
    if (undo_.empty() || read_only_) return false;
    Step s = std::move(undo_.back());
    undo_.pop_back();
    text_.replace(s.start, s.inserted.size(), s.removed);
    caret_ = s.caret_before;
    anchor_ = s.anchor_before;
    redo_.push_back(std::move(s));
    coalesce_ = false;
    ++revision_;
    return true;
}

bool EditModel::redo() {
    if (redo_.empty() || read_only_) return false;
    Step s = std::move(redo_.back());
    redo_.pop_back();
    text_.replace(s.start, s.removed.size(), s.inserted);
    caret_ = anchor_ = s.start + s.inserted.size();
    undo_.push_back(std::move(s));
    coalesce_ = false;
    ++revision_;
    return true;
}

}  // namespace tcad::ui

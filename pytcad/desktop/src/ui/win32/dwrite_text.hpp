// DirectWrite behind the portable TextEngine (N2b; N2d: wrapping, hit-testing, caret stops, a layout cache).
//
// Fonts: Segoe UI for the UI, Consolas for FontFamily::Monospace. Anything a font lacks (Devanagari, CJK, emoji, ...)
// comes from the system's font fallback; Arabic and Hebrew runs are shaped and laid out right to left inside the
// (left-to-right) UI paragraphs by DirectWrite's bidi algorithm.
//
// Every text operation goes through ONE cached IDWriteTextLayout per (text, style, box): measuring, drawing
// (D2DPainter uses DrawTextLayout) and hit-testing agree by construction. The cache is LRU, kCacheCapacity layouts.
// Offsets are UTF-8 bytes (painter.hpp); DirectWrite's UTF-16 positions are mapped both ways per layout.
#pragma once

#include "ui/core/painter.hpp"
#include "ui/render/render_device.hpp"

#include <list>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace tcad::ui {

std::wstring toUtf16(std::string_view utf8);

class DWriteTextEngine final : public TextEngine {
public:
    static constexpr std::size_t kCacheCapacity = 256;
    static constexpr float kUnbounded = 1.0e6f;

    explicit DWriteTextEngine(IDWriteFactory2* factory);

    SizeF measure(std::string_view utf8, const TextStyle& style) override;
    SizeF measureWrapped(std::string_view utf8, const TextStyle& style, float max_width) override;
    int lineCount(std::string_view utf8, const TextStyle& style, float max_width) override;
    TextHit hitTest(std::string_view utf8, const TextStyle& style, float max_width, PointF p) override;
    RectF caretRect(std::string_view utf8, const TextStyle& style, float max_width, std::size_t offset) override;
    std::vector<std::size_t> caretStops(std::string_view utf8, const TextStyle& style) override;

    // The cached layout of `utf8` in a max_width x max_height box, with the style's alignment and wrapping.
    IDWriteTextLayout* layout(std::string_view utf8, const TextStyle& style, float max_width, float max_height = kUnbounded);
    IDWriteTextFormat* format(const TextStyle& style);  // the style's font, size and weight (no alignment)

    struct Stats {
        std::size_t hits = 0, misses = 0, entries = 0;
    };
    Stats stats() const { return {hits_, misses_, entries_.size()}; }

private:
    struct Key {
        std::string text;
        float size, width, height;
        bool bold, wrap;
        int family, halign, valign;
        auto tie() const { return std::tie(text, size, width, height, bold, wrap, family, halign, valign); }
        bool operator<(const Key& o) const { return tie() < o.tie(); }
    };
    struct Entry {
        ComPtr<IDWriteTextLayout> layout;
        std::vector<std::size_t> u8_of_u16;  // UTF-8 offset of each UTF-16 index, plus one for the end
        std::list<Key>::iterator lru;
    };
    Entry* entry(std::string_view utf8, const TextStyle& style, float max_width, float max_height);
    static std::size_t u16Index(const Entry& e, std::size_t u8_offset);

    ComPtr<IDWriteFactory2> factory_;
    std::map<std::tuple<float, bool, int>, ComPtr<IDWriteTextFormat>> formats_;
    std::map<Key, Entry> entries_;
    std::list<Key> lru_;  // most recent first
    std::size_t hits_ = 0, misses_ = 0;
};

}  // namespace tcad::ui

#include "ui/win32/dwrite_text.hpp"

#include <windows.h>

#include <algorithm>

namespace tcad::ui {

namespace {

// UTF-8 -> UTF-16, and for every UTF-16 unit the UTF-8 offset of its code point (plus a final entry = size). An
// invalid byte becomes U+FFFD and is mapped on its own, so the maps never lose their correspondence.
void convert(std::string_view s, std::wstring& w, std::vector<std::size_t>& u8_of_u16) {
    w.clear();
    u8_of_u16.clear();
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        std::size_t n = 1;
        auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && cont(1)) cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3F), n = 2;
        else if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3F), n = 3;
        else if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3))
            cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3F), n = 4;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            w.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            w.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            u8_of_u16.push_back(i);
            u8_of_u16.push_back(i);
        } else {
            w.push_back(static_cast<wchar_t>(cp));
            u8_of_u16.push_back(i);
        }
        i += n;
    }
    u8_of_u16.push_back(s.size());
}

}  // namespace

std::wstring toUtf16(std::string_view utf8) {
    std::wstring w;
    std::vector<std::size_t> map;
    convert(utf8, w, map);
    return w;
}

DWriteTextEngine::DWriteTextEngine(IDWriteFactory2* factory) : factory_(factory) {}

IDWriteTextFormat* DWriteTextEngine::format(const TextStyle& style) {
    const auto key = std::make_tuple(style.size, style.bold, static_cast<int>(style.family));
    if (auto it = formats_.find(key); it != formats_.end()) return it->second.Get();
    const wchar_t* family = style.family == FontFamily::Monospace ? L"Consolas" : L"Segoe UI";
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(factory_->CreateTextFormat(family, nullptr, style.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.size, L"en-us", &f)))
        return nullptr;
    return formats_.emplace(key, f).first->second.Get();
}

DWriteTextEngine::Entry* DWriteTextEngine::entry(std::string_view utf8, const TextStyle& style, float w, float h) {
    Key key{std::string(utf8), style.size, w, h, style.bold, style.wrap,
            static_cast<int>(style.family), static_cast<int>(style.halign), static_cast<int>(style.valign)};
    if (auto it = entries_.find(key); it != entries_.end()) {
        ++hits_;
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return &it->second;
    }
    ++misses_;
    IDWriteTextFormat* f = format(style);
    if (!f) return nullptr;
    std::wstring text;
    Entry e;
    convert(utf8, text, e.u8_of_u16);
    if (FAILED(factory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), f, w, h, &e.layout))) return nullptr;
    // WHOLE_WORD, not WRAP: a word wider than the box overflows instead of being split mid-word (Qt's WordWrap)
    e.layout->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WHOLE_WORD : DWRITE_WORD_WRAPPING_NO_WRAP);
    e.layout->SetTextAlignment(style.halign == HAlign::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                               : style.halign == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                                : DWRITE_TEXT_ALIGNMENT_TRAILING);
    e.layout->SetParagraphAlignment(style.valign == VAlign::Top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                    : style.valign == VAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                                     : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    if (entries_.size() >= kCacheCapacity) {  // evict the least recently used
        entries_.erase(lru_.back());
        lru_.pop_back();
    }
    lru_.push_front(key);
    e.lru = lru_.begin();
    return &entries_.emplace(std::move(key), std::move(e)).first->second;
}

IDWriteTextLayout* DWriteTextEngine::layout(std::string_view utf8, const TextStyle& style, float w, float h) {
    Entry* e = entry(utf8, style, w, h);
    return e ? e->layout.Get() : nullptr;
}

namespace {

TextStyle measuring(TextStyle s, bool wrap) {  // alignment does not change a size: one cache entry per text
    s.halign = HAlign::Left;
    s.valign = VAlign::Top;
    s.wrap = wrap;
    s.color = {};
    return s;
}

}  // namespace

SizeF DWriteTextEngine::measure(std::string_view utf8, const TextStyle& style) {
    IDWriteTextLayout* l = layout(utf8, measuring(style, false), kUnbounded, kUnbounded);
    if (!l) return {0, 0};
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return {m.widthIncludingTrailingWhitespace, m.height};
}

SizeF DWriteTextEngine::measureWrapped(std::string_view utf8, const TextStyle& style, float max_width) {
    IDWriteTextLayout* l = layout(utf8, measuring(style, true), std::max(0.0f, max_width), kUnbounded);
    if (!l) return {0, 0};
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return {m.width, m.height};  // trailing spaces at a break do not widen a wrapped block
}

int DWriteTextEngine::lineCount(std::string_view utf8, const TextStyle& style, float max_width) {
    IDWriteTextLayout* l = layout(utf8, measuring(style, true), std::max(0.0f, max_width), kUnbounded);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return static_cast<int>(m.lineCount);
}

std::size_t DWriteTextEngine::u16Index(const Entry& e, std::size_t off) {
    // the first UTF-16 unit that starts at (or after) the byte offset
    const auto it = std::lower_bound(e.u8_of_u16.begin(), e.u8_of_u16.end(), off);
    return static_cast<std::size_t>(it - e.u8_of_u16.begin());
}

TextHit DWriteTextEngine::hitTest(std::string_view utf8, const TextStyle& style, float max_width, PointF p) {
    TextStyle s = style;
    s.color = {};
    s.valign = VAlign::Top;  // positions are from the text's top line: vertical placement is the caller's (an
                             // unbounded box height with Center would put the text half a million DIPs down)
    Entry* e = entry(utf8, s, max_width > 0 ? max_width : kUnbounded, kUnbounded);
    if (!e) return {};
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    if (FAILED(e->layout->HitTestPoint(p.x, p.y, &trailing, &inside, &m))) return {};
    const std::size_t u16 = std::min<std::size_t>(m.textPosition + (trailing ? m.length : 0), e->u8_of_u16.size() - 1);
    return {e->u8_of_u16[u16], inside != FALSE};
}

RectF DWriteTextEngine::caretRect(std::string_view utf8, const TextStyle& style, float max_width, std::size_t offset) {
    TextStyle s = style;
    s.color = {};
    s.valign = VAlign::Top;  // as hitTest
    Entry* e = entry(utf8, s, max_width > 0 ? max_width : kUnbounded, kUnbounded);
    if (!e) return {};
    const std::size_t idx = u16Index(*e, std::min(offset, utf8.size()));
    FLOAT x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS m{};
    // at the end of the text, the TRAILING edge of the last unit; elsewhere the leading edge of the unit at idx
    const bool at_end = idx + 1 >= e->u8_of_u16.size() && idx > 0;
    if (FAILED(e->layout->HitTestTextPosition(static_cast<UINT32>(at_end ? idx - 1 : idx), at_end ? TRUE : FALSE, &x, &y, &m)))
        return {};
    return {x, y, 0.0f, m.height};
}

std::vector<std::size_t> DWriteTextEngine::caretStops(std::string_view utf8, const TextStyle& style) {
    Entry* e = entry(utf8, measuring(style, false), kUnbounded, kUnbounded);
    std::vector<std::size_t> stops{0};
    if (!e || utf8.empty()) return stops;
    UINT32 n = 0;
    e->layout->GetClusterMetrics(nullptr, 0, &n);  // E_NOT_SUFFICIENT_BUFFER, and the count
    std::vector<DWRITE_CLUSTER_METRICS> cm(n);
    if (FAILED(e->layout->GetClusterMetrics(cm.data(), n, &n))) return stops;
    std::size_t pos = 0;
    for (const auto& c : cm) {
        pos += c.length;
        stops.push_back(e->u8_of_u16[std::min<std::size_t>(pos, e->u8_of_u16.size() - 1)]);
    }
    return stops;
}

}  // namespace tcad::ui

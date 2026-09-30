#include "ui/widgets/mnemonic.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tcad::ui {

namespace {

// The code point starting at s[i] and its length in bytes (a malformed byte is taken as one byte).
std::pair<char32_t, std::size_t> decode(std::string_view s, std::size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
    if (i + n > s.size()) n = 1;
    if (n == 1) return {b, 1};
    char32_t c = b & (0x7F >> n);
    for (std::size_t k = 1; k < n; ++k) {
        const auto cb = static_cast<unsigned char>(s[i + k]);
        if ((cb & 0xC0) != 0x80) return {b, 1};
        c = (c << 6) | (cb & 0x3F);
    }
    return {c, n};
}

}  // namespace

MnemonicText parseMnemonic(std::string_view s) {
    MnemonicText m;
    m.text.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&' || i + 1 == s.size()) {
            m.text.push_back(s[i++]);
            continue;
        }
        if (s[i + 1] == '&') {
            m.text.push_back('&');
            i += 2;
            continue;
        }
        const auto [c, n] = decode(s, i + 1);
        if (!m.key && c != ' ') {
            m.key = c;
            m.offset = m.text.size();
            m.length = n;
        }
        m.text.append(s.substr(i + 1, n));
        i += 1 + n;
    }
    return m;
}

void drawMnemonicText(Painter& p, TextEngine* te, const RectF& r, const MnemonicText& m, const TextStyle& style,
                      bool underline) {
    p.drawText(r, m.text, style);
    if (!underline || !m.key || !te) return;
    const SizeF whole = te->measure(m.text, style);
    const float before = te->measure(std::string_view(m.text).substr(0, m.offset), style).width;
    const float glyph = te->measure(std::string_view(m.text).substr(m.offset, m.length), style).width;
    const float left = style.halign == HAlign::Left ? r.x
                       : style.halign == HAlign::Center ? r.x + (r.width - whole.width) / 2
                                                         : r.right() - whole.width;
    const float top = style.valign == VAlign::Top ? r.y
                      : style.valign == VAlign::Center ? r.y + (r.height - whole.height) / 2
                                                        : r.bottom() - whole.height;
    // Segoe UI: ascent 2210 of a 2724-unit line; the underline sits a little below the baseline, one device pixel
    // thick, on a whole pixel so it is sharp
    const double s = p.scale();
    const float y = static_cast<float>(std::round((top + 0.811f * whole.height + 0.09f * style.size) * s) / s);
    const float thick = static_cast<float>(std::max(1.0, std::round(s)) / s);
    p.fillRect({left + before, y, glyph, thick}, style.color);
}

}  // namespace tcad::ui

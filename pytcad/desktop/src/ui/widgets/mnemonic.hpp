// '&' mnemonics in widget text (N3a, NATIVE-DESKTOP-PLAN.md 27.8), Qt's rules: "&Run" shows "Run" with R as the
// Alt+R key and underlined; "&&" shows one '&'; only the first single '&' marks a key (later ones are dropped); a
// trailing '&' is shown as is. The underline is drawn only while the window shows keyboard cues
// (Widget::mnemonicCuesVisible).
#pragma once

#include "ui/core/painter.hpp"

#include <string>
#include <string_view>

namespace tcad::ui {

struct MnemonicText {
    std::string text;                   // as shown
    char32_t key = 0;                   // the marked character (0: none); the router compares it case-insensitively
    std::size_t offset = 0, length = 0;  // the marked character's UTF-8 bytes in `text`
};

MnemonicText parseMnemonic(std::string_view s);

// `m.text` in `r` per `style` (one line), and, when `underline` and there is a key, a line under the key's character
// at the text's baseline. The underline's position comes from measuring the text before the key, so it follows the
// alignment and the font (not the glyph's kerning inside the whole line: under a pixel at UI sizes).
void drawMnemonicText(Painter& p, TextEngine* te, const RectF& r, const MnemonicText& m, const TextStyle& style,
                      bool underline);

}  // namespace tcad::ui

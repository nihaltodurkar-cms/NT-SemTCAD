// Named Windows virtual-key codes for portable widget code (N3a). platform::KeyEvent carries Windows VK codes on
// every platform; these are the values of winuser.h, so portable code need not include it.
#pragma once

namespace tcad::ui::keys {

inline constexpr int Tab = 0x09;
inline constexpr int Return = 0x0D;
inline constexpr int Menu = 0x12;  // Alt
inline constexpr int Escape = 0x1B;
inline constexpr int Space = 0x20;
inline constexpr int PageUp = 0x21;
inline constexpr int PageDown = 0x22;
inline constexpr int End = 0x23;
inline constexpr int Home = 0x24;
inline constexpr int Left = 0x25;
inline constexpr int Up = 0x26;
inline constexpr int Right = 0x27;
inline constexpr int Down = 0x28;
inline constexpr int Insert = 0x2D;
inline constexpr int Delete = 0x2E;
inline constexpr int Back = 0x08;
inline constexpr int Multiply = 0x6A;  // numpad *
inline constexpr int Add = 0x6B;       // numpad +
inline constexpr int Subtract = 0x6D;  // numpad -
inline constexpr int F2 = 0x71;
inline constexpr int F10 = 0x79;
inline constexpr int Apps = 0x5D;      // the context-menu key

}  // namespace tcad::ui::keys

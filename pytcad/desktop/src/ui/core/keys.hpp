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

}  // namespace tcad::ui::keys

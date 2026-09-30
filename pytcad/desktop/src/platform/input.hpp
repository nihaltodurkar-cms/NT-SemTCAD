// Input events and keyboard shortcuts (N1). Pure and platform-independent: the Win32 window translates messages
// into these; the workspace and the (future, N2) widgets consume them. Coordinates are LOGICAL pixels,
// top-left origin.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace tcad::platform {

enum class Mod : unsigned { None = 0, Ctrl = 1, Shift = 2, Alt = 4 };
constexpr Mod operator|(Mod a, Mod b) { return static_cast<Mod>(static_cast<unsigned>(a) | static_cast<unsigned>(b)); }
constexpr Mod operator&(Mod a, Mod b) { return static_cast<Mod>(static_cast<unsigned>(a) & static_cast<unsigned>(b)); }
constexpr bool any(Mod m) { return static_cast<unsigned>(m) != 0; }

enum class MouseButton { None, Left, Middle, Right, X1, X2 };
enum class MouseType { Move, Down, Up, DoubleClick, Wheel, Leave };

struct MouseEvent {
    MouseType type = MouseType::Move;
    MouseButton button = MouseButton::None;  // Down / Up / DoubleClick: which one
    double x = 0, y = 0;                     // logical px
    double wheel_steps = 0;                  // Wheel: notches, positive = away from the user
    Mod mods = Mod::None;
    unsigned buttons_down = 0;               // bit per MouseButton (1 << int(button)), all currently held
};

struct KeyEvent {
    int vk = 0;  // Windows virtual-key code (letters and digits are their upper-case ASCII)
    Mod mods = Mod::None;
    bool down = true;
    bool repeat = false;
};

struct Shortcut {
    int vk = 0;
    Mod mods = Mod::None;
    bool operator==(const Shortcut&) const = default;
    bool operator<(const Shortcut& o) const {
        return vk != o.vk ? vk < o.vk : static_cast<unsigned>(mods) < static_cast<unsigned>(o.mods);
    }
};

// "Ctrl+O", "Ctrl+Shift+F5", "Alt+Left", "Esc", "Del" (case-insensitive). nullopt if any part is unknown.
std::optional<Shortcut> parseShortcut(std::string_view text);
// The virtual key VTK's KeySym names ("f", "F5", "Escape", "Left", "Delete", ...); 0 if unknown.
int vkFromVtkKeySym(std::string_view keysym);

class ShortcutMap {
public:
    // False (and nothing registered) when `text` does not parse or is already taken.
    bool add(std::string_view text, std::function<void()> action);
    // Runs the action of a key-DOWN, non-repeat event that matches; true if one ran.
    bool dispatch(const KeyEvent& e) const;
    std::size_t size() const { return map_.size(); }

private:
    std::map<Shortcut, std::function<void()>> map_;
};

}  // namespace tcad::platform

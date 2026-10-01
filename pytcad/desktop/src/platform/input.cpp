#include "platform/input.hpp"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace tcad::platform {
namespace {

std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

// Key names shared by the shortcut parser and the VTK keysym table.
int namedKey(const std::string& n) {
    static const std::map<std::string, int> keys = {
        {"esc", 0x1B},    {"escape", 0x1B}, {"enter", 0x0D},  {"return", 0x0D}, {"tab", 0x09},   {"space", 0x20},
        {"del", 0x2E},    {"delete", 0x2E}, {"backspace", 0x08}, {"back", 0x08}, {"insert", 0x2D}, {"ins", 0x2D},
        {"home", 0x24},   {"end", 0x23},    {"pgup", 0x21},   {"prior", 0x21},  {"pageup", 0x21}, {"pgdn", 0x22},
        {"next", 0x22},   {"pagedown", 0x22}, {"left", 0x25}, {"up", 0x26},    {"right", 0x27},  {"down", 0x28},
        {"plus", 0xBB},   {"minus", 0xBD}};
    if (auto it = keys.find(n); it != keys.end()) return it->second;
    if (n.size() == 1 && std::isalnum(static_cast<unsigned char>(n[0])))
        return std::toupper(static_cast<unsigned char>(n[0]));  // A-Z and 0-9 are their ASCII codes
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'f' && std::isdigit(static_cast<unsigned char>(n[1]))) {
        const int k = std::atoi(n.c_str() + 1);
        if (k >= 1 && k <= 24) return 0x6F + k;  // F1 = 0x70
    }
    return 0;
}

}  // namespace

std::optional<Shortcut> parseShortcut(std::string_view text) {
    Shortcut s;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : text) {
        if (c == '+' && !cur.empty()) {
            parts.push_back(lower(cur));
            cur.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        }
    }
    if (cur.empty()) return std::nullopt;  // "Ctrl+" or ""
    parts.push_back(lower(cur));
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i] == "ctrl" || parts[i] == "control") s.mods = s.mods | Mod::Ctrl;
        else if (parts[i] == "shift") s.mods = s.mods | Mod::Shift;
        else if (parts[i] == "alt") s.mods = s.mods | Mod::Alt;
        else return std::nullopt;
    }
    s.vk = namedKey(parts.back());
    if (!s.vk) return std::nullopt;
    return s;
}

int vkFromVtkKeySym(std::string_view keysym) { return namedKey(lower(keysym)); }

bool ShortcutMap::add(std::string_view text, std::function<void()> action) {
    const auto s = parseShortcut(text);
    if (!s || !action || map_.count(*s)) return false;
    map_.emplace(*s, std::move(action));
    return true;
}

bool ShortcutMap::remove(std::string_view text) {
    const auto s = parseShortcut(text);
    return s && map_.erase(*s) > 0;
}

bool ShortcutMap::dispatch(const KeyEvent& e) const {
    if (!e.down || e.repeat) return false;
    const auto it = map_.find(Shortcut{e.vk, e.mods});
    if (it == map_.end()) return false;
    it->second();
    return true;
}

}  // namespace tcad::platform

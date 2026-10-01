#include "ui/win32/clipboard.hpp"

#include "ui/win32/dwrite_text.hpp"

#include <cstring>

namespace tcad::ui {

namespace {

bool openClipboard(HWND owner) {
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(10);
    }
    return false;
}

// Windows text uses CR LF between lines; ours (and every portable consumer's) uses LF alone. Converted at the edge, both
// ways, so a multi-line selection pastes into Notepad, Excel and a mail as lines (N3d), and what they copy comes back as
// lines. A lone CR (old Mac text) is a line break too.
std::string withCrLf(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += "\r\n";
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else if (s[i] == '\n') {
            out += "\r\n";
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string withLf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string toUtf8(const wchar_t* w, std::size_t n) {
    if (!n) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), len, nullptr, nullptr);
    return s;
}

}  // namespace

bool setClipboardText(HWND owner, std::string_view utf8) {
    const std::wstring w = toUtf16(withCrLf(utf8));
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (!mem) return false;
    std::memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem);
    if (!openClipboard(owner)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);  // on success the clipboard owns it
    return ok;
}

std::optional<std::string> clipboardText(HWND owner) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !openClipboard(owner)) return std::nullopt;
    std::optional<std::string> out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = withLf(toUtf8(w, wcsnlen(w, GlobalSize(h) / sizeof(wchar_t))));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

}  // namespace tcad::ui

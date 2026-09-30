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

std::string toUtf8(const wchar_t* w, std::size_t n) {
    if (!n) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), len, nullptr, nullptr);
    return s;
}

}  // namespace

bool setClipboardText(HWND owner, std::string_view utf8) {
    const std::wstring w = toUtf16(utf8);
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
            out = toUtf8(w, wcsnlen(w, GlobalSize(h) / sizeof(wchar_t)));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

}  // namespace tcad::ui

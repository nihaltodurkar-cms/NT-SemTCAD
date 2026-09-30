// The Windows clipboard as UTF-8 text (N2e): CF_UNICODETEXT in and out. OpenClipboard can fail while another process
// holds it, so both retry briefly; they return false / nullopt when the clipboard stays unavailable.
#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace tcad::ui {

bool setClipboardText(HWND owner, std::string_view utf8);
std::optional<std::string> clipboardText(HWND owner);

}  // namespace tcad::ui

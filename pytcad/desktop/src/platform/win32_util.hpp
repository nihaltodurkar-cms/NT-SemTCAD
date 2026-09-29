// Small Win32 helpers of the native platform layer (N1): UTF-8 <-> UTF-16, environment, well-known folders.
#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace tcad::platform {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
// The value of an environment variable (UTF-8), or nullopt when it is not set.
std::optional<std::string> getEnv(const char* name);
// The running executable's directory, and %APPDATA%\PyTCAD (the per-user settings directory; not created).
std::filesystem::path executableDir();
std::filesystem::path appDataDir();

}  // namespace tcad::platform

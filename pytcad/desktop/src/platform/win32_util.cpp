#include "platform/win32_util.hpp"

#include <shlobj.h>

#include <vector>

namespace tcad::platform {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::optional<std::string> getEnv(const char* name) {
    const std::wstring wname = widen(name);
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (n == 0) return std::nullopt;  // not set (an empty value is also reported as 0 with no error set: treat as absent)
    std::wstring v(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(wname.c_str(), v.data(), n);
    v.resize(got);
    return narrow(v);
}

std::filesystem::path executableDir() {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path appDataDir() {
    PWSTR p = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &p))) out = std::filesystem::path(p) / L"PyTCAD";
    CoTaskMemFree(p);
    return out;
}

}  // namespace tcad::platform

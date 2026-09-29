#include "platform/env_path.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace tcad::platform {
namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> splitPath(const std::string& path, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == sep) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& v, char sep) {
    std::string o;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) o.push_back(sep);
        o += v[i];
    }
    return o;
}

bool isAbsolutePath(const std::string& v) {
    if (v.empty()) return false;
    if (v[0] == '/' || v[0] == '\\') return true;                                     // rooted / UNC
    return v.size() >= 2 && std::isalpha(static_cast<unsigned char>(v[0])) && v[1] == ':';  // C:...
}

}  // namespace

std::string normalizedDir(const std::string& d) {
    std::string raw = d;
    std::replace(raw.begin(), raw.end(), '\\', '/');  // both separators, whatever the platform's own
    std::string g = fs::path(raw).lexically_normal().generic_string();
    while (g.size() > 1 && g.back() == '/') g.pop_back();
    return lowerAscii(g);
}

std::string pythonPathEnvironment(const std::string& current_path, const std::string& python,
                                  const std::vector<std::string>& strip, char sep, bool native_seps) {
    std::vector<std::string> path = splitPath(current_path, sep);
    std::vector<std::string> stripped;
    for (const auto& d : strip) stripped.push_back(normalizedDir(d));
    std::erase_if(path, [&](const std::string& d) {
        return std::find(stripped.begin(), stripped.end(), normalizedDir(d)) != stripped.end();
    });
    std::error_code ec;
    if (!python.empty()) {
        const fs::path prefix = fs::absolute(fs::path(python), ec).parent_path();
        if (!ec && fs::is_directory(prefix / "Library" / "bin", ec)) {
            std::vector<std::string> add;
            for (const char* sub : {".", "Library/mingw-w64/bin", "Library/usr/bin", "Library/bin", "Scripts", "bin"}) {
                const fs::path d = (prefix / sub).lexically_normal();
                if (fs::is_directory(d, ec)) {
                    std::string s = d.generic_string();
                    while (s.size() > 1 && s.back() == '/') s.pop_back();
                    if (native_seps) std::replace(s.begin(), s.end(), '/', '\\');
                    add.push_back(s);
                }
            }
            std::erase_if(path, [&](const std::string& d) {
                return std::any_of(add.begin(), add.end(), [&](const std::string& a) { return normalizedDir(a) == normalizedDir(d); });
            });
            add.insert(add.end(), path.begin(), path.end());
            path = std::move(add);
        }
    }
    return join(path, sep);
}

std::string resolveManifestPath(const std::string& app_dir, const std::string& value) {
    if (value.empty() || isAbsolutePath(value)) return value;
    return (fs::path(app_dir) / value).lexically_normal().string();
}

RpcConfig resolveBackendConfig(const std::string& app_dir, const std::string& manifest_text,
                               const std::string& settings_python,
                               const std::function<std::optional<std::string>(const char*)>& getenv) {
    RpcConfig c;
    std::string runtime_bin;
    if (!manifest_text.empty()) {
        try {
            const auto j = nlohmann::json::parse(manifest_text);
            auto path = [&](const char* key) { return resolveManifestPath(app_dir, j[key].get<std::string>()); };
            if (j.contains("backend_python")) c.python = path("backend_python");
            if (j.contains("backend_root")) c.working_dir = path("backend_root");
            if (j.contains("runtime_bin")) runtime_bin = path("runtime_bin");
        } catch (const nlohmann::json::exception&) {
            // an unreadable manifest: fall through to the other sources
        }
    }
    if (!settings_python.empty()) c.python = settings_python;
    if (auto e = getenv("TCAD_BACKEND_PYTHON"); e && !e->empty()) c.python = *e;
    if (auto e = getenv("TCAD_BACKEND_ROOT"); e && !e->empty()) c.working_dir = *e;
    if (!runtime_bin.empty()) c.strip_from_path.push_back(runtime_bin);
    return c;
}

}  // namespace tcad::platform

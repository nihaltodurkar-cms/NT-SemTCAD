#include "platform/settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace tcad::platform {

struct Settings::Impl {
    nlohmann::json doc = {{"version", Settings::kVersion}, {"recent", {{"files", nlohmann::json::array()}, {"projects", nlohmann::json::array()}}}, {"values", nlohmann::json::object()}};
};

Settings::~Settings() = default;
Settings::Settings(Settings&&) noexcept = default;
Settings& Settings::operator=(Settings&&) noexcept = default;

namespace {

std::string normalized(const std::string& p) {
    std::error_code ec;
    fs::path a = fs::absolute(fs::path(p), ec);
    if (ec) a = fs::path(p);
    return a.lexically_normal().string();
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool samePath(const std::string& a, const std::string& b) { return lowerAscii(normalized(a)) == lowerAscii(normalized(b)); }

Settings Settings::atFile(const fs::path& file) {
    Settings s;
    s.file_ = file;
    s.impl_ = std::make_unique<Impl>();
    s.load();
    return s;
}

Settings Settings::ephemeral() {
    Settings s;
    s.impl_ = std::make_unique<Impl>();
    return s;
}

Settings Settings::userDefault(const fs::path& dir) { return atFile(dir / "PyTCAD Native.json"); }

void Settings::load() {
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    std::ifstream in(file_, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    auto j = nlohmann::json::parse(buf.str(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || !j.contains("version") || !j["version"].is_number_integer()) {
        load_problem_ = "the settings file is not valid JSON of this application; it is kept as .corrupt when settings are next saved";
        quarantine_pending_ = true;
        return;
    }
    if (j["version"].get<int>() != kVersion) {
        load_problem_ = "the settings file is of another version (" + std::to_string(j["version"].get<int>()) + "); ignored, kept as .corrupt when settings are next saved";
        quarantine_pending_ = true;
        return;
    }
    for (const char* k : {"recent", "values"})
        if (j.contains(k) && j[k].is_object()) impl_->doc[k].update(j[k], /*merge_objects=*/true);
    if (j.contains("window") && j["window"].is_object()) impl_->doc["window"] = j["window"];
}

bool Settings::writable() const {
    if (file_.empty()) return true;
    std::error_code ec;
    if (fs::exists(file_, ec)) {
        const auto perms = fs::status(file_, ec).permissions();
        return (perms & fs::perms::owner_write) != fs::perms::none;
    }
    const fs::path dir = file_.parent_path().empty() ? fs::path(".") : file_.parent_path();
    return !fs::exists(dir, ec) || (fs::status(dir, ec).permissions() & fs::perms::owner_write) != fs::perms::none;
}

std::vector<std::string> Settings::list(const char* key) const {
    std::vector<std::string> out;
    const auto& r = impl_->doc["recent"];
    if (r.contains(key) && r[key].is_array())
        for (const auto& v : r[key])
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

void Settings::setList(const char* key, const std::vector<std::string>& v) { impl_->doc["recent"][key] = v; }

void Settings::addTo(const char* key, const std::string& path) {
    auto l = list(key);
    std::erase_if(l, [&](const std::string& p) { return samePath(p, path); });
    l.insert(l.begin(), normalized(path));
    if (l.size() > kMaxRecent) l.resize(kMaxRecent);
    setList(key, l);
}

void Settings::removeFrom(const char* key, const std::string& path) {
    auto l = list(key);
    std::erase_if(l, [&](const std::string& p) { return samePath(p, path); });
    setList(key, l);
}

std::optional<WindowPlacement> Settings::windowPlacement() const {
    if (!impl_->doc.contains("window")) return std::nullopt;
    const auto& w = impl_->doc["window"];
    for (const char* k : {"x", "y", "w", "h"})
        if (!w.contains(k) || !w[k].is_number_integer()) return std::nullopt;
    if (w["w"].get<int>() <= 0 || w["h"].get<int>() <= 0) return std::nullopt;
    WindowPlacement p;
    p.x = w["x"].get<int>();
    p.y = w["y"].get<int>();
    p.w = w["w"].get<int>();
    p.h = w["h"].get<int>();
    p.maximized = w.value("maximized", false);
    return p;
}

void Settings::setWindowPlacement(const WindowPlacement& p) {
    impl_->doc["window"] = {{"x", p.x}, {"y", p.y}, {"w", p.w}, {"h", p.h}, {"maximized", p.maximized}};
}

std::string Settings::value(const std::string& key, const std::string& fallback) const {
    const auto& v = impl_->doc["values"];
    return v.contains(key) && v[key].is_string() ? v[key].get<std::string>() : fallback;
}

void Settings::setValue(const std::string& key, const std::string& v) { impl_->doc["values"][key] = v; }

bool Settings::sync() {
    last_error_.clear();
    if (file_.empty()) return true;
    std::error_code ec;
    if (!file_.parent_path().empty()) fs::create_directories(file_.parent_path(), ec);
    if (quarantine_pending_) {
        fs::path bad = file_;
        bad += ".corrupt";
        fs::remove(bad, ec);
        fs::rename(file_, bad, ec);
        if (ec) {
            last_error_ = "cannot set the unreadable settings file aside: " + ec.message();
            return false;
        }
        quarantine_pending_ = false;
    }
    fs::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            last_error_ = "cannot write " + tmp.string();
            return false;
        }
        out << impl_->doc.dump(2) << "\n";
        out.flush();
        if (!out) {
            last_error_ = "write to " + tmp.string() + " failed";
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, file_, ec);  // replaces an existing file (POSIX rename; MoveFileEx REPLACE_EXISTING on Windows)
    if (ec) {
        last_error_ = "cannot replace " + file_.string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace tcad::platform

// The application's persistent settings (N1; replaces AppSettings' QSettings INI): one JSON document,
//   {"version":1, "recent":{"files":[...],"projects":[...]}, "window":{...}, "values":{"key":"text"}}
// Three modes, as before: the per-user default file, a given file (tests), and ephemeral (nothing read or
// written: a measurement never depends on, or overwrites, the user's settings).
// Writes are ATOMIC (temp file + rename). A file that cannot be parsed is not overwritten silently: it is moved
// to "<name>.corrupt" the first time a write would replace it, and the settings start empty.
// Pure std + nlohmann::json: unit-tested on any platform. The legacy INI (layout/docks blobs are ADS-specific) is
// NOT migrated: the native window layout is a new format (N4).
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tcad::platform {

// A window's saved placement (DEVICE pixels of the monitor it was on, the normal - not maximised - rectangle).
struct WindowPlacement {
    int x = 0, y = 0, w = 0, h = 0;
    bool maximized = false;
    bool operator==(const WindowPlacement&) const = default;
};

// True when a and b name the same file: absolute, lexically normalised, compared case-insensitively.
bool samePath(const std::string& a, const std::string& b);

class Settings {
public:
    static constexpr int kVersion = 1;
    static constexpr std::size_t kMaxRecent = 10;

    ~Settings();
    Settings(Settings&&) noexcept;
    Settings& operator=(Settings&&) noexcept;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    static Settings atFile(const std::filesystem::path& file);
    static Settings ephemeral();
    // <dir>/PyTCAD Native.json, dir given (Win32 code passes %APPDATA%\PyTCAD).
    static Settings userDefault(const std::filesystem::path& dir);

    bool persistent() const { return !file_.empty(); }
    const std::filesystem::path& fileName() const { return file_; }
    // Non-empty when an existing file could not be read (it will be quarantined, not lost).
    const std::string& loadProblem() const { return load_problem_; }
    // False when the file exists but is read-only, or its directory is not writable.
    bool writable() const;

    // Recent result files and recent project files: most recent first, de-duplicated by samePath, at most kMaxRecent.
    std::vector<std::string> recentFiles() const { return list("files"); }
    void addRecent(const std::string& path) { addTo("files", path); }
    void removeRecent(const std::string& path) { removeFrom("files", path); }
    void clearRecent() { setList("files", {}); }
    std::vector<std::string> recentProjects() const { return list("projects"); }
    void addRecentProject(const std::string& path) { addTo("projects", path); }
    void removeRecentProject(const std::string& path) { removeFrom("projects", path); }
    void clearRecentProjects() { setList("projects", {}); }

    std::optional<WindowPlacement> windowPlacement() const;
    void setWindowPlacement(const WindowPlacement& p);

    std::string value(const std::string& key, const std::string& fallback = {}) const;
    void setValue(const std::string& key, const std::string& v);

    // Write to disk (atomically). False if that failed (why in lastError()); true for ephemeral settings.
    bool sync();
    const std::string& lastError() const { return last_error_; }

private:
    Settings() = default;
    void load();
    std::vector<std::string> list(const char* key) const;
    void setList(const char* key, const std::vector<std::string>& v);
    void addTo(const char* key, const std::string& path);
    void removeFrom(const char* key, const std::string& path);

    std::filesystem::path file_;  // empty: ephemeral
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string load_problem_;
    std::string last_error_;
    bool quarantine_pending_ = false;
};

}  // namespace tcad::platform

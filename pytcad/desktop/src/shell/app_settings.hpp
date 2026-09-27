// The shell's persistent settings (NATIVE-DESKTOP-PLAN.md section
// 15.13, S3b): window geometry, dock layout and recent files. (S3c's theme
// choice is gone: one black-and-white scheme since 2026-09-26; an old
// "theme/choice" key is ignored.)
//
// INI, not the registry, so a user or a test can read, reset or delete
// it. Three modes:
//   - default: <AppData>/PyTCAD/PyTCAD Desktop.ini (a normal launch);
//   - a given file: `tcad_desktop --settings <file>` (every test);
//   - ephemeral: nothing read or written (--bench, --selftest), so a
//     measurement never depends on, or overwrites, the user's layout.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

class QSettings;

namespace tcad::desktop {

class AppSettings {
public:
    // Bump when the dock layout changes incompatibly: a saved layout of
    // another version is ignored (default layout), never half-applied.
    // 2: the Info panel joined the layout (S3d). 3: the Display panel (S5e).
    // 4: S6's 3D and Playback docks. 5: the Plot panel and the central splitter (P2-S3).
    // 6: the Run and Console docks (P3-S4). 7: the Telemetry dock (P3-S5).
    static constexpr int kLayoutVersion = 9;  // section 25: the Compact Model dock added
    static constexpr int kMaxRecent = 10;

    static std::unique_ptr<AppSettings> userDefault();
    static std::unique_ptr<AppSettings> atFile(const QString& ini_path);
    static std::unique_ptr<AppSettings> ephemeral();
    ~AppSettings();

    bool persistent() const { return settings_ != nullptr; }
    // False when the file exists but cannot be written (read-only).
    bool writable() const;
    QString fileName() const;

    // Recent result files, most recent first, de-duplicated by absolute
    // path compared case-insensitively (Windows paths are).
    QStringList recentFiles() const;
    void addRecent(const QString& path);
    void removeRecent(const QString& path);
    void clearRecent();

    // Recent PROJECT files (section 24) -- the exact same shape/limit/
    // de-duplication as recentFiles() above, kept as a SEPARATE list
    // (a different QSettings key) since a project and a result are
    // different things opened from different menus.
    QStringList recentProjects() const;
    void addRecentProject(const QString& path);
    void removeRecentProject(const QString& path);
    void clearRecentProjects();

    struct Layout {
        QByteArray geometry;  // QWidget::saveGeometry()
        QByteArray docks;     // ads::CDockManager::saveState()
    };
    // Nothing when absent, or saved by another layout version.
    bool loadLayout(Layout* out) const;
    void saveLayout(const Layout& layout);

    QString value(const QString& key, const QString& fallback = {}) const;
    void setValue(const QString& key, const QString& v);

    // Flush to disk; false if the write failed.
    bool sync();

private:
    explicit AppSettings(std::unique_ptr<QSettings> s);
    // `key`/`memory` generalize recentFiles()/recentProjects()'s
    // identical add/remove/clear logic over two independent lists.
    QStringList recentList(const char* key, const QStringList& memory) const;
    void setRecentList(const char* key, QStringList* memory, const QStringList& list);
    void addToRecentList(const char* key, QStringList* memory, const QString& path);
    void removeFromRecentList(const char* key, QStringList* memory, const QString& path);

    std::unique_ptr<QSettings> settings_;  // null: ephemeral
    QStringList memory_recent_;            // ephemeral mode's in-memory list
    QStringList memory_recent_projects_;   // ephemeral mode's in-memory project list
};

// True when a and b name the same file: absolute, cleaned, and compared
// case-insensitively.
bool samePath(const QString& a, const QString& b);

}  // namespace tcad::desktop

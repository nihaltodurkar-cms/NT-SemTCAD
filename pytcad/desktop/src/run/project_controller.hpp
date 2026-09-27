// Native project load/save (P4 S9, NATIVE-DESKTOP-PLAN.md section 20.4)
// through the backend's project.load/project.save RPC (backend_service/
// server.py) -- distinct from RunController's DeviceSource::Project,
// which only ever resolves a bare, already-run-ready DeviceSpec via the
// older, read-only project.spec. This owns the FULL editable project:
// the three S1 documents plus the sweep/models JSON the QML GUI already
// treats as opaque, and the S8 UndoStack over them.
//
// Qt Core only (same layer as RunController): StructureDocument/
// MeshDocument/ProcessFlowDocument are Qt-free, so this can be used
// without pulling in Widgets/VTK.
#pragma once

#include "backend/backend_client.hpp"
#include "document/mesh_document.hpp"
#include "document/process_document.hpp"
#include "document/structure_document.hpp"
#include "document/undo_stack.hpp"

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

#include <functional>

namespace tcad::desktop {

class ProjectController : public QObject {
    Q_OBJECT

public:
    // `backend` gives the (lazily created) client, which must outlive this
    // controller; it is called only when a backend call is made.
    explicit ProjectController(std::function<BackendClient*()> backend, QObject* parent = nullptr);

    void newProject();
    void load(const QString& path);
    // target_version: 5 or 6 (project_store.SCHEMA_VERSION) -- a
    // schema-6-only project (specVersion() > 1) asked to save at 5
    // fails loudly (projectSaveFailed, "IncompatibleDowngradeError"),
    // never silently.
    void save(const QString& path, int target_version = 6);

    const QString& name() const { return name_; }
    void setName(const QString& name) { name_ = name; }
    const QString& path() const { return path_; }

    StructureDocument& structure() { return structure_; }
    const StructureDocument& structure() const { return structure_; }
    MeshDocument& mesh() { return mesh_; }
    const MeshDocument& mesh() const { return mesh_; }
    ProcessFlowDocument& process() { return process_; }
    const ProcessFlowDocument& process() const { return process_; }
    const nlohmann::json& sweep() const { return sweep_; }
    const nlohmann::json& models() const { return models_; }
    // The Physics Lab model config (catalog.py's own config dict) is
    // carried opaque here (S9's own decision), same as `sweep()` -- not
    // one of S8's three undo-tracked documents, so it gets its own
    // small dirty flag instead of an UndoStack command.
    void setModels(nlohmann::json models) {
        models_ = std::move(models);
        models_dirty_ = true;
    }
    bool modelsDirty() const { return models_dirty_; }

    bool hasStructure() const { return has_structure_; }
    bool hasMesh() const { return has_mesh_; }
    int specVersion() const { return spec_version_; }
    void setSpecVersion(int v) { spec_version_ = v; }

    UndoStack& undoStack() { return undo_; }
    bool isDirty() const { return undo_.is_dirty() || models_dirty_; }

signals:
    void projectLoaded(const QString& path);
    void projectLoadFailed(const QString& title, const QString& detail);
    void projectSaved(const QString& path);
    void projectSaveFailed(const QString& title, const QString& detail);

private:
    std::function<BackendClient*()> backend_;
    QString name_ = "Untitled";
    QString path_;
    StructureDocument structure_ = StructureDocument::parse(R"({"width_cm": 1e-4, "height_cm": 1e-4})");
    MeshDocument mesh_ = MeshDocument::parse("{}");
    ProcessFlowDocument process_ = ProcessFlowDocument::parse(R"({"steps": []})");
    nlohmann::json sweep_ = nullptr;
    nlohmann::json models_ = nullptr;
    bool has_structure_ = true;
    bool has_mesh_ = true;
    bool models_dirty_ = false;
    int spec_version_ = 1;
    UndoStack undo_;
    int load_generation_ = 0;
    int save_generation_ = 0;
};

}  // namespace tcad::desktop

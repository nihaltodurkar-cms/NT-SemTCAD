#include "project_controller.hpp"

namespace tcad::desktop {

namespace {
constexpr int kBackendTimeoutMs = 30000;

// The exception's own class name (backend_service/server.py's -32000
// envelope: {"type": ..., ...}) when there is one, else a generic title
// -- the same "surface the real type, don't invent a message" idiom
// RunController::titleFor uses.
QString errorTitle(const BackendReply* reply) {
    const auto& data = reply->errorData();
    if (data.is_object() && data.contains("type") && data["type"].is_string())
        return QString::fromStdString(data["type"].get<std::string>());
    return QStringLiteral("Backend error");
}
}  // namespace

ProjectController::ProjectController(std::function<BackendClient*()> backend, QObject* parent)
    : QObject(parent), backend_(std::move(backend)) {}

void ProjectController::newProject() {
    ++load_generation_;
    ++save_generation_;
    name_ = "Untitled";
    path_.clear();
    structure_ = StructureDocument::parse(R"({"width_cm": 1e-4, "height_cm": 1e-4})");
    mesh_ = MeshDocument::parse("{}");
    process_ = ProcessFlowDocument::parse(R"({"steps": []})");
    sweep_ = nullptr;
    models_ = nullptr;
    has_structure_ = true;
    has_mesh_ = true;
    spec_version_ = 1;
    undo_ = UndoStack();
}

void ProjectController::load(const QString& path) {
    const int generation = ++load_generation_;
    nlohmann::json params;
    params["path"] = path.toStdString();
    BackendReply* reply = backend_()->call("project.load", params, kBackendTimeoutMs);
    connect(reply, &BackendReply::finished, this, [this, reply, generation, path] {
        reply->deleteLater();
        if (generation != load_generation_) return;  // superseded by a newer load/new
        if (!reply->ok()) {
            emit projectLoadFailed(errorTitle(reply), reply->errorMessage());
            return;
        }
        const auto& r = reply->result();
        name_ = QString::fromStdString(r.value("name", std::string()));
        path_ = path;
        has_structure_ = !r.at("structure").is_null();
        structure_ = StructureDocument::parse(
            has_structure_ ? r.at("structure").dump()
                           : std::string(R"({"width_cm": 1e-4, "height_cm": 1e-4})"));
        has_mesh_ = !r.at("mesh").is_null();
        mesh_ = MeshDocument::parse(has_mesh_ ? r.at("mesh").dump() : std::string("{}"));
        process_ = ProcessFlowDocument::parse(r.at("process").dump());
        sweep_ = r.value("sweep", nlohmann::json(nullptr));
        models_ = r.value("models", nlohmann::json(nullptr));
        spec_version_ = 1;  // project.load's own result carries no spec_version echo yet
        undo_ = UndoStack();
        emit projectLoaded(path_);
    });
}

void ProjectController::save(const QString& path, int target_version) {
    const int generation = ++save_generation_;
    nlohmann::json params;
    params["path"] = path.toStdString();
    params["name"] = name_.toStdString();
    params["structure"] = has_structure_ ? nlohmann::json(structure_.json()) : nlohmann::json(nullptr);
    params["mesh"] = has_mesh_ ? nlohmann::json(mesh_.json()) : nlohmann::json(nullptr);
    params["process"] = process_.json();
    params["sweep"] = sweep_;
    params["models"] = models_;
    params["spec_version"] = spec_version_;
    params["target_version"] = target_version;
    BackendReply* reply = backend_()->call("project.save", params, kBackendTimeoutMs);
    connect(reply, &BackendReply::finished, this, [this, reply, generation, path] {
        reply->deleteLater();
        if (generation != save_generation_) return;  // superseded
        if (!reply->ok()) {
            emit projectSaveFailed(errorTitle(reply), reply->errorMessage());
            return;
        }
        path_ = path;
        undo_.mark_clean();
        emit projectSaved(path_);
    });
}

}  // namespace tcad::desktop

// BuildPanel (P4 shell assembly, NATIVE-DESKTOP-PLAN.md section 20.4 /
// 21): the Device Builder UI every S2-S8 slice built and gated as
// isolated widgets, finally assembled into one panel over a single
// live ProjectController. "No second device-editing path exists" --
// BuilderController.py's own rule -- carries over: this is the ONE
// place structure/mesh/process edits happen in the native app.
//
// Every mutating editor signal (MeshEditor::meshEdited(),
// RegionListWidget::regionsChanged(), DopingEditor::regionEdited(),
// StructureEditorView::regionEdited(), ContactEditor::contactEdited(),
// GateEditor::gateEdited(), ProcessStepListWidget::stepsChanged(), each
// step editor's stepEdited()) is caught here and turned into an S8
// UndoStack command via a before/after JSON snapshot -- these editors
// already mutate their document directly (verified by reading each
// one before wiring it, not assumed), so the "before" snapshot is
// whatever this panel last recorded, not something the editor hands
// back.
#pragma once

#include <nlohmann/json.hpp>

#include <QWidget>

#include <string>
#include <vector>

namespace tcad::desktop {

class ProjectController;
class MeshEditor;
class StructureEditorView;
class RegionListWidget;
class DopingEditor;
class ContactListWidget;
class ContactEditor;
class GateListWidget;
class GateEditor;
class ProcessStepListWidget;
class SubstrateStepEditor;
class ImplantStepEditor;
class AnnealStepEditor;
class OxidizeStepEditor;
class ValidationPanel;
class CatalogPanel;

class BuildPanel : public QWidget {
    Q_OBJECT

public:
    explicit BuildPanel(QWidget* parent = nullptr);

    // Non-owning; caller keeps it alive. Re-reads every widget from the
    // controller's live documents -- call after load()/newProject() and
    // after any undo()/redo() done outside this panel (e.g. a toolbar
    // action), since those replace the document objects wholesale.
    void setProject(ProjectController* project);
    void refreshAll();

    CatalogPanel* catalogPanel() const { return catalog_panel_; }

signals:
    // MainWindow owns the BackendClient; this panel only asks.
    void validateStructureRequested();
    void validateProcessRequested();
    // Re-emitted from CatalogPanel (MainWindow owns the BackendClient
    // and the actual template/model RPC calls, same division as
    // validation above).
    void buildTemplateRequested(const QString& id, const nlohmann::json& values);
    void modelConfigChanged(const nlohmann::json& config);
    // Fired once, the first time the "Templates & Models" tab is shown --
    // every OTHER backend touch in this app follows a user/project action
    // (Run, Validate, Save, ...); fetching the (static) template/catalog
    // registry at construction time instead of on first need was a real
    // bug (see NATIVE-DESKTOP-PLAN.md section 24): it raced the very
    // first backend handshake against whatever a test (or a fast user)
    // did immediately after opening the window, corrupting the reply
    // that arrived while a nested event loop (e.g. openBuildProject's
    // own wait-for-load) was pumping.
    void templateCatalogRequested();

public slots:
    void setStructureErrors(const std::vector<std::string>& messages);
    void setProcessErrors(const std::vector<std::string>& messages);
    // Replaces the structure/mesh documents wholesale (a built template
    // adopted into the shared document) as ONE undoable command -- the
    // same "no second device-editing path" rule BuilderController.py
    // followed: this reuses the SAME live StructureDocument/MeshDocument
    // every other editor here mutates, never a parallel one.
    void adoptStructureAndMesh(const nlohmann::json& structure, const nlohmann::json& mesh);

private:
    void wireStructureEditors();
    void wireProcessEditors();
    void onRegionSelectionChanged(const QString& id);
    void onContactSelectionChanged(const QString& id);
    void onGateSelectionChanged(const QString& id);
    void onProcessStepSelectionChanged(const QString& id);
    void noteStructureChanged();
    void noteMeshChanged();
    void noteProcessChanged();

    ProjectController* project_ = nullptr;
    std::string structure_snapshot_;
    std::string mesh_snapshot_;
    std::string process_snapshot_;

    MeshEditor* mesh_editor_ = nullptr;
    StructureEditorView* canvas_ = nullptr;
    RegionListWidget* region_list_ = nullptr;
    DopingEditor* doping_editor_ = nullptr;
    ContactListWidget* contact_list_ = nullptr;
    ContactEditor* contact_editor_ = nullptr;
    GateListWidget* gate_list_ = nullptr;
    GateEditor* gate_editor_ = nullptr;
    ValidationPanel* structure_validation_ = nullptr;

    ProcessStepListWidget* process_list_ = nullptr;
    SubstrateStepEditor* substrate_editor_ = nullptr;
    ImplantStepEditor* implant_editor_ = nullptr;
    AnnealStepEditor* anneal_editor_ = nullptr;
    OxidizeStepEditor* oxidize_editor_ = nullptr;
    QWidget* step_editor_stack_ = nullptr;
    ValidationPanel* process_validation_ = nullptr;

    CatalogPanel* catalog_panel_ = nullptr;
    bool catalog_requested_ = false;
};

}  // namespace tcad::desktop

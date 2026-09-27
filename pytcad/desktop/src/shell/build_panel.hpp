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

signals:
    // MainWindow owns the BackendClient; this panel only asks.
    void validateStructureRequested();
    void validateProcessRequested();

public slots:
    void setStructureErrors(const std::vector<std::string>& messages);
    void setProcessErrors(const std::vector<std::string>& messages);

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
};

}  // namespace tcad::desktop

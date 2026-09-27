#include "build_panel.hpp"

#include "document/undo_stack.hpp"
#include "editors/anneal_step_editor.hpp"
#include "editors/contact_editor.hpp"
#include "editors/contact_list_widget.hpp"
#include "editors/doping_editor.hpp"
#include "editors/gate_editor.hpp"
#include "editors/gate_list_widget.hpp"
#include "editors/implant_step_editor.hpp"
#include "editors/mesh_editor.hpp"
#include "editors/oxidize_step_editor.hpp"
#include "editors/process_step_list_widget.hpp"
#include "editors/region_list_widget.hpp"
#include "editors/structure_editor_view.hpp"
#include "editors/substrate_step_editor.hpp"
#include "run/project_controller.hpp"
#include "shell/validation_panel.hpp"

#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace tcad::desktop {

namespace {
// The step operations, in the same fixed order ProcessStepListWidget's
// own combo uses -- one QStackedWidget page per operation.
enum StepPage { kSubstrate = 0, kImplant, kAnneal, kOxidize, kNone };

StepPage pageFor(const std::string& operation) {
    if (operation == "substrate") return kSubstrate;
    if (operation == "implant") return kImplant;
    if (operation == "anneal") return kAnneal;
    if (operation == "oxidize") return kOxidize;
    return kNone;
}
}  // namespace

BuildPanel::BuildPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName("buildTabs");
    root->addWidget(tabs);

    // -- Structure tab --------------------------------------------------------
    auto* structureSplit = new QSplitter(Qt::Horizontal);
    structureSplit->setObjectName("structureSplit");

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    mesh_editor_ = new MeshEditor(left);
    canvas_ = new StructureEditorView(left);
    canvas_->setObjectName("structureCanvas");
    canvas_->setMinimumHeight(200);
    structure_validation_ = new ValidationPanel(left);
    structure_validation_->setObjectName("structureValidation");
    leftLayout->addWidget(mesh_editor_);
    leftLayout->addWidget(canvas_, /*stretch=*/1);
    leftLayout->addWidget(structure_validation_);
    structureSplit->addWidget(left);

    auto* innerTabs = new QTabWidget;
    innerTabs->setObjectName("structureInnerTabs");

    auto* regionsPage = new QWidget;
    auto* regionsSplit = new QSplitter(Qt::Vertical, regionsPage);
    region_list_ = new RegionListWidget;
    doping_editor_ = new DopingEditor;
    regionsSplit->addWidget(region_list_);
    regionsSplit->addWidget(doping_editor_);
    auto* regionsLayout = new QVBoxLayout(regionsPage);
    regionsLayout->addWidget(regionsSplit);
    innerTabs->addTab(regionsPage, tr("Regions"));

    auto* contactsPage = new QWidget;
    auto* contactsSplit = new QSplitter(Qt::Vertical, contactsPage);
    contact_list_ = new ContactListWidget;
    contact_editor_ = new ContactEditor;
    contactsSplit->addWidget(contact_list_);
    contactsSplit->addWidget(contact_editor_);
    auto* contactsLayout = new QVBoxLayout(contactsPage);
    contactsLayout->addWidget(contactsSplit);
    innerTabs->addTab(contactsPage, tr("Contacts"));

    auto* gatesPage = new QWidget;
    auto* gatesSplit = new QSplitter(Qt::Vertical, gatesPage);
    gate_list_ = new GateListWidget;
    gate_editor_ = new GateEditor;
    gatesSplit->addWidget(gate_list_);
    gatesSplit->addWidget(gate_editor_);
    auto* gatesLayout = new QVBoxLayout(gatesPage);
    gatesLayout->addWidget(gatesSplit);
    innerTabs->addTab(gatesPage, tr("Gates"));

    structureSplit->addWidget(innerTabs);
    tabs->addTab(structureSplit, tr("Structure"));

    // -- Process tab ------------------------------------------------------------
    auto* processSplit = new QSplitter(Qt::Horizontal);
    process_list_ = new ProcessStepListWidget;
    processSplit->addWidget(process_list_);

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    auto* stack = new QStackedWidget;
    step_editor_stack_ = stack;
    substrate_editor_ = new SubstrateStepEditor;
    implant_editor_ = new ImplantStepEditor;
    anneal_editor_ = new AnnealStepEditor;
    oxidize_editor_ = new OxidizeStepEditor;
    stack->insertWidget(kSubstrate, substrate_editor_);
    stack->insertWidget(kImplant, implant_editor_);
    stack->insertWidget(kAnneal, anneal_editor_);
    stack->insertWidget(kOxidize, oxidize_editor_);
    stack->insertWidget(kNone, new QWidget);  // nothing selected
    process_validation_ = new ValidationPanel;
    process_validation_->setObjectName("processValidation");
    rightLayout->addWidget(stack, /*stretch=*/1);
    rightLayout->addWidget(process_validation_);
    processSplit->addWidget(right);
    tabs->addTab(processSplit, tr("Process"));

    wireStructureEditors();
    wireProcessEditors();
}

void BuildPanel::setProject(ProjectController* project) {
    project_ = project;
    refreshAll();
}

void BuildPanel::refreshAll() {
    if (!project_) return;
    structure_snapshot_ = project_->structure().dump();
    mesh_snapshot_ = project_->mesh().dump();
    process_snapshot_ = project_->process().dump();

    mesh_editor_->setDocument(&project_->mesh());
    canvas_->setDocuments(&project_->structure(), &project_->mesh());
    region_list_->setDocument(&project_->structure());
    contact_list_->setDocument(&project_->structure());
    gate_list_->setDocument(&project_->structure());
    doping_editor_->setRegion(&project_->structure(), QString());
    contact_editor_->setContact(&project_->structure(), QString());
    gate_editor_->setGate(&project_->structure(), QString());
    process_list_->setDocument(&project_->process());
    substrate_editor_->setStep(&project_->process(), QString());
    implant_editor_->setStep(&project_->process(), QString());
    anneal_editor_->setStep(&project_->process(), QString());
    oxidize_editor_->setStep(&project_->process(), QString());
    static_cast<QStackedWidget*>(step_editor_stack_)->setCurrentIndex(kNone);
}

void BuildPanel::wireStructureEditors() {
    connect(mesh_editor_, &MeshEditor::meshEdited, this, [this] {
        noteMeshChanged();
        canvas_->update();
    });
    connect(canvas_, &StructureEditorView::regionEdited, this, [this](const QString&) {
        noteStructureChanged();
        region_list_->refresh();
        doping_editor_->refresh();
    });
    connect(canvas_, &StructureEditorView::selectionChanged, this, &BuildPanel::onRegionSelectionChanged);
    connect(region_list_, &RegionListWidget::selectionChanged, this, &BuildPanel::onRegionSelectionChanged);
    connect(region_list_, &RegionListWidget::regionsChanged, this, [this] {
        noteStructureChanged();
        canvas_->update();
    });
    connect(doping_editor_, &DopingEditor::regionEdited, this, [this](const QString&) {
        noteStructureChanged();
        canvas_->update();
        region_list_->refresh();
    });
    connect(contact_list_, &ContactListWidget::selectionChanged, this, &BuildPanel::onContactSelectionChanged);
    connect(contact_editor_, &ContactEditor::contactEdited, this, [this](const QString&) {
        noteStructureChanged();
        contact_list_->refresh();
    });
    connect(gate_list_, &GateListWidget::selectionChanged, this, &BuildPanel::onGateSelectionChanged);
    connect(gate_editor_, &GateEditor::gateEdited, this, [this](const QString&) {
        noteStructureChanged();
        gate_list_->refresh();
    });
}

void BuildPanel::wireProcessEditors() {
    connect(process_list_, &ProcessStepListWidget::selectionChanged, this,
            &BuildPanel::onProcessStepSelectionChanged);
    connect(process_list_, &ProcessStepListWidget::stepsChanged, this, [this] {
        noteProcessChanged();
        onProcessStepSelectionChanged(process_list_->selectedStepId());
    });
    auto stepEdited = [this](const QString&) { noteProcessChanged(); };
    connect(substrate_editor_, &SubstrateStepEditor::stepEdited, this, stepEdited);
    connect(implant_editor_, &ImplantStepEditor::stepEdited, this, stepEdited);
    connect(anneal_editor_, &AnnealStepEditor::stepEdited, this, stepEdited);
    connect(oxidize_editor_, &OxidizeStepEditor::stepEdited, this, stepEdited);
}

void BuildPanel::onRegionSelectionChanged(const QString& id) {
    // The canvas has no programmatic "select" entry point (only mouse-
    // driven selection, S2's own scope) -- syncing stops at the list,
    // which is a no-op re-selection when the canvas is what changed
    // (QListWidget::currentItemChanged does not re-fire for the same
    // row, verified in region_list_widget.cpp before relying on it).
    if (region_list_->selectedRegionId() != id) region_list_->selectRegion(id);
    doping_editor_->setRegion(&project_->structure(), id);
    canvas_->update();
}

void BuildPanel::onContactSelectionChanged(const QString& id) {
    contact_editor_->setContact(&project_->structure(), id);
}

void BuildPanel::onGateSelectionChanged(const QString& id) {
    gate_editor_->setGate(&project_->structure(), id);
}

void BuildPanel::onProcessStepSelectionChanged(const QString& id) {
    auto* stack = static_cast<QStackedWidget*>(step_editor_stack_);
    if (id.isEmpty()) {
        stack->setCurrentIndex(kNone);
        return;
    }
    ProcessFlowDocument& doc = project_->process();
    std::string operation;
    for (std::size_t i = 0; i < doc.step_count(); ++i) {
        if (QString::fromStdString(doc.step(i).id) == id) {
            operation = doc.step(i).operation;
            break;
        }
    }
    const StepPage page = pageFor(operation);
    stack->setCurrentIndex(page);
    switch (page) {
        case kSubstrate: substrate_editor_->setStep(&doc, id); break;
        case kImplant: implant_editor_->setStep(&doc, id); break;
        case kAnneal: anneal_editor_->setStep(&doc, id); break;
        case kOxidize: oxidize_editor_->setStep(&doc, id); break;
        default: break;
    }
}

void BuildPanel::noteStructureChanged() {
    if (!project_) return;
    StructureDocument& doc = project_->structure();
    const std::string after = doc.dump();
    if (after == structure_snapshot_) return;
    const std::string before = structure_snapshot_;
    project_->undoStack().push(Command(
        [&doc, after] { doc = StructureDocument::parse(after); },
        [&doc, before] { doc = StructureDocument::parse(before); }, "Edit structure"));
    structure_snapshot_ = after;
    emit validateStructureRequested();
}

void BuildPanel::noteMeshChanged() {
    if (!project_) return;
    MeshDocument& doc = project_->mesh();
    const std::string after = doc.dump();
    if (after == mesh_snapshot_) return;
    const std::string before = mesh_snapshot_;
    project_->undoStack().push(Command(
        [&doc, after] { doc = MeshDocument::parse(after); },
        [&doc, before] { doc = MeshDocument::parse(before); }, "Edit mesh"));
    mesh_snapshot_ = after;
    emit validateStructureRequested();
}

void BuildPanel::noteProcessChanged() {
    if (!project_) return;
    ProcessFlowDocument& doc = project_->process();
    const std::string after = doc.dump();
    if (after == process_snapshot_) return;
    const std::string before = process_snapshot_;
    project_->undoStack().push(Command(
        [&doc, after] { doc = ProcessFlowDocument::parse(after); },
        [&doc, before] { doc = ProcessFlowDocument::parse(before); }, "Edit process"));
    process_snapshot_ = after;
    emit validateProcessRequested();
}

void BuildPanel::setStructureErrors(const std::vector<std::string>& messages) {
    structure_validation_->setErrors(messages);
}

void BuildPanel::setProcessErrors(const std::vector<std::string>& messages) {
    process_validation_->setErrors(messages);
}

}  // namespace tcad::desktop

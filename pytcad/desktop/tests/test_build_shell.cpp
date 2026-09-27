// The Device Builder shell assembly (NATIVE-DESKTOP-PLAN.md section 21):
// the REAL MainWindow, its Build dock, and the real backend service --
// New/Open/Save project, mesh/region/contact/gate/process edits through
// the actual widget tree, undo/redo, and live validation.
//
// Run by gui/tests/test_desktop_build_shell.py. Needs a GL surface (the
// FieldView central widget), so not run by ctest.
#include "document/mesh_document.hpp"
#include "document/process_document.hpp"
#include "document/structure_document.hpp"
#include "editors/contact_editor.hpp"
#include "editors/contact_list_widget.hpp"
#include "editors/doping_editor.hpp"
#include "editors/gate_editor.hpp"
#include "editors/gate_list_widget.hpp"
#include "editors/mesh_editor.hpp"
#include "editors/process_step_list_widget.hpp"
#include "editors/region_list_widget.hpp"
#include "editors/structure_editor_view.hpp"
#include "editors/substrate_step_editor.hpp"
#include "run/project_controller.hpp"
#include "shell/app_settings.hpp"
#include "shell/build_panel.hpp"
#include "shell/catalog_panel.hpp"
#include "shell/main_window.hpp"
#include "shell/validation_panel.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

#include <cmath>
#include <functional>
#include <memory>

using tcad::desktop::AppSettings;
using tcad::desktop::BuildPanel;
using tcad::desktop::ContactEditor;
using tcad::desktop::ContactListWidget;
using tcad::desktop::DopingEditor;
using tcad::desktop::GateEditor;
using tcad::desktop::GateListWidget;
using tcad::desktop::MainWindow;
using tcad::desktop::MeshEditor;
using tcad::desktop::ProcessStepListWidget;
using tcad::desktop::ProjectController;
using tcad::desktop::RegionListWidget;
using tcad::desktop::CatalogPanel;
using tcad::desktop::StructureEditorView;
using tcad::desktop::SubstrateStepEditor;
using tcad::desktop::ValidationPanel;

namespace {

template <class T>
T* child(QWidget* panel, const char* name) {
    T* c = panel->findChild<T*>(name);
    if (!c) qWarning("no child %s", name);
    return c;
}

bool waitFor(std::function<bool()> cond, int ms) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

}  // namespace

class TestBuildShell : public QObject {
    Q_OBJECT

    QTemporaryDir tmp_;
    QString ini(const QString& name) const { return tmp_.filePath(name); }

    static std::unique_ptr<MainWindow> shown(const QString& ini_path) {
        auto w = std::make_unique<MainWindow>(AppSettings::atFile(ini_path));
        w->show();
        if (!QTest::qWaitForWindowExposed(w.get())) return nullptr;
        return w;
    }

private slots:
    void newOpensACleanEditableProject() {
        auto w = shown(ini("new.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        auto* panel = w->buildPanel();
        QVERIFY(panel);
        QVERIFY(panel->findChild<MeshEditor*>() != nullptr);
        QVERIFY(!w->undoAction()->isEnabled());
        QVERIFY(!w->redoAction()->isEnabled());
    }

    void editingTheMeshPushesAnUndoableCommand() {
        auto w = shown(ini("mesh_edit.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        QCOMPARE(project->mesh().nx(), 40);  // MeshDocument::parse("{}")'s own default

        auto* panel = w->buildPanel();
        auto* build_tabs = child<QTabWidget>(panel, "buildTabs");
        QVERIFY(build_tabs);
        build_tabs->setCurrentIndex(0);  // Structure
        auto* spin = panel->findChild<QSpinBox*>("nx");
        QVERIFY(spin);
        spin->setValue(55);
        spin->editingFinished();
        QCOMPARE(project->mesh().nx(), 55);
        QVERIFY(w->undoAction()->isEnabled());

        w->undoAction()->trigger();
        QCOMPARE(project->mesh().nx(), 40);
        QVERIFY(w->redoAction()->isEnabled());

        w->redoAction()->trigger();
        QCOMPARE(project->mesh().nx(), 55);
    }

    void addingARegionIsUndoableAndValidates() {
        auto w = shown(ini("region_edit.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        project->structure().set_width_cm(1e-4);
        project->structure().set_height_cm(1e-4);

        auto* panel = w->buildPanel();
        auto* regionList = panel->findChild<RegionListWidget*>();
        QVERIFY(regionList);
        auto* addBtn = regionList->findChild<QPushButton*>("addRegion");
        QVERIFY(addBtn);
        QTest::mouseClick(addBtn, Qt::LeftButton);
        QCOMPARE(project->structure().region_count(), std::size_t(1));
        QVERIFY(w->undoAction()->isEnabled());

        // the DopingEditor is now showing the new region -- edit its doping
        auto* doping = panel->findChild<DopingEditor*>();
        QVERIFY(doping);
        QCOMPARE(doping->regionId(), regionList->selectedRegionId());

        w->undoAction()->trigger();
        QCOMPARE(project->structure().region_count(), std::size_t(0));
    }

    void projectSaveLoadRoundTripsThroughTheRealBackend() {
        auto w = shown(ini("save_load.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        project->structure().set_width_cm(2e-4);
        project->structure().set_height_cm(1e-4);
        tcad::desktop::RegionData r;
        r.id = "r1";
        r.name = "R1";
        r.x_max = 2e-4;
        r.y_max = 1e-4;
        r.net_doping_cm3 = 1e17;
        project->structure().add_region(r);

        const QString path = tmp_.filePath("saved_project.json");
        QVERIFY(w->saveBuildProject(path));

        auto w2 = shown(ini("save_load2.ini"));
        QVERIFY(w2 != nullptr);
        QVERIFY(w2->openBuildProject(path));
        ProjectController* project2 = w2->projectController();
        QCOMPARE(project2->structure().region_count(), std::size_t(1));
        QCOMPARE(project2->structure().region(0).id, std::string("r1"));
        QVERIFY(!w2->undoAction()->isEnabled());
    }

    void structureValidationReachesThePanelThroughTheRealBackend() {
        auto w = shown(ini("validate.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        // width_cm=1e-4 (MeshDocument default project), height_cm negative -> invalid
        project->structure().set_height_cm(-1.0);

        auto* panel = w->buildPanel();
        auto* validation = panel->findChild<ValidationPanel*>("structureValidation");
        QVERIFY(validation);
        // Trigger a structure edit through the real widget so BuildPanel's
        // own change-detection (not a direct test call) requests validation.
        auto* mesh_editor = panel->findChild<MeshEditor*>();
        QVERIFY(mesh_editor);
        auto* spin = panel->findChild<QSpinBox*>("nx");
        spin->setValue(spin->value() + 1);
        spin->editingFinished();

        QVERIFY(waitFor([&] { return !validation->ok(); }, 15000));
    }

    // -- closing the asymmetric-coverage gap (section 22's own disclosed
    //    scope): contact/gate/process-step edits use the identical
    //    note*Changed() path region/mesh edits already proved, but had no
    //    dedicated shell-level gate of their own.
    void editingAContactPushesAnUndoableCommand() {
        auto w = shown(ini("contact_edit.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        project->structure().set_width_cm(1e-4);
        project->structure().set_height_cm(1e-4);
        tcad::desktop::ContactData c;
        c.id = "c1";
        c.name = "C1";
        c.boundary.edge = "left";
        c.V = 0.0;
        project->structure().add_contact(c);
        w->buildPanel()->refreshAll();  // pick up the contact added directly on the document

        auto* panel = w->buildPanel();
        auto* innerTabs = panel->findChild<QTabWidget*>("structureInnerTabs");
        QVERIFY(innerTabs);
        innerTabs->setCurrentIndex(1);  // Contacts
        auto* contactList = panel->findChild<ContactListWidget*>();
        QVERIFY(contactList);
        contactList->selectContact("c1");
        auto* voltage = panel->findChild<QDoubleSpinBox*>("contactVoltage");
        QVERIFY(voltage);
        voltage->setValue(1.5);
        voltage->editingFinished();
        QCOMPARE(project->structure().contact(0).V, 1.5);
        QVERIFY(w->undoAction()->isEnabled());

        w->undoAction()->trigger();
        QCOMPARE(project->structure().contact(0).V, 0.0);
    }

    void editingAGatePushesAnUndoableCommand() {
        auto w = shown(ini("gate_edit.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        project->structure().set_width_cm(1e-4);
        project->structure().set_height_cm(1e-4);
        tcad::desktop::GateData g;
        g.id = "g1";
        g.name = "G1";
        g.boundary.edge = "top";
        g.tox_cm = 1e-6;
        g.V = 0.0;
        project->structure().add_gate(g);
        w->buildPanel()->refreshAll();

        auto* panel = w->buildPanel();
        auto* innerTabs = panel->findChild<QTabWidget*>("structureInnerTabs");
        QVERIFY(innerTabs);
        innerTabs->setCurrentIndex(2);  // Gates
        auto* gateList = panel->findChild<GateListWidget*>();
        QVERIFY(gateList);
        gateList->selectGate("g1");
        auto* voltage = panel->findChild<QDoubleSpinBox*>("gateVoltage");
        QVERIFY(voltage);
        voltage->setValue(2.0);
        voltage->editingFinished();
        QCOMPARE(project->structure().gate(0).V, 2.0);
        QVERIFY(w->undoAction()->isEnabled());

        w->undoAction()->trigger();
        QCOMPARE(project->structure().gate(0).V, 0.0);
    }

    void editingAProcessStepPushesAnUndoableCommand() {
        auto w = shown(ini("process_edit.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        tcad::desktop::ProcessStepData s;
        s.id = "s1";
        s.name = "Substrate";
        s.operation = "substrate";
        s.parameters = {{"length_cm", 1e-3}, {"background_doping_cm3", 1e15},
                        {"mesh", {{"h_min_cm", 1e-7}, {"h_max_cm", 1e-5}, {"ratio", 1.2}}}};
        project->process().add_step(s);
        w->buildPanel()->refreshAll();

        auto* panel = w->buildPanel();
        auto* buildTabs = panel->findChild<QTabWidget*>("buildTabs");
        QVERIFY(buildTabs);
        buildTabs->setCurrentIndex(1);  // Process
        auto* stepList = panel->findChild<ProcessStepListWidget*>();
        QVERIFY(stepList);
        stepList->selectStep("s1");
        auto* length = panel->findChild<QDoubleSpinBox*>("substrateLengthCm");
        QVERIFY(length);
        QCOMPARE(length->value(), 1e-3);  // SubstrateStepEditor's field is cm, no unit conversion
        length->setValue(2e-3);
        length->editingFinished();
        QCOMPARE(project->process().step(0).parameters.at("length_cm").get<double>(), 2e-3);
        QVERIFY(w->undoAction()->isEnabled());

        w->undoAction()->trigger();
        QCOMPARE(project->process().step(0).parameters.at("length_cm").get<double>(), 1e-3);
    }

    // -- the dirty-flag close prompt (section 22's other disclosed gap) --------
    void closingADirtyProjectPromptsAndCancelKeepsItOpen() {
        auto w = shown(ini("close_cancel.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        w->projectController()->mesh().set_nx(41);
        w->buildPanel()->refreshAll();
        auto* spin = w->buildPanel()->findChild<QSpinBox*>("nx");
        spin->setValue(42);
        spin->editingFinished();
        QVERIFY(w->projectController()->isDirty());

        QTimer::singleShot(200, w.get(), [] {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (top->objectName() == "CloseConfirmBox" && top->isVisible()) {
                    QTest::keyClick(top, Qt::Key_Escape);  // Cancel is the escape button
                    return;
                }
        });
        w->close();
        QVERIFY(w->isVisible());  // Cancel kept the window open
    }

    void closingADirtyProjectAndDiscardingCloses() {
        auto w = shown(ini("close_discard.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        w->projectController()->mesh().set_nx(41);
        w->buildPanel()->refreshAll();
        auto* spin = w->buildPanel()->findChild<QSpinBox*>("nx");
        spin->setValue(42);
        spin->editingFinished();
        QVERIFY(w->projectController()->isDirty());

        QTimer::singleShot(200, w.get(), [] {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (top->objectName() == "CloseConfirmBox" && top->isVisible()) {
                    auto* box = qobject_cast<QMessageBox*>(top);
                    box->button(QMessageBox::Discard)->click();
                    return;
                }
        });
        w->close();
        QVERIFY(!w->isVisible());
    }

    void aCleanProjectClosesWithoutAnyPrompt() {
        auto w = shown(ini("close_clean.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        QVERIFY(!w->projectController()->isDirty());
        w->close();
        QVERIFY(!w->isVisible());
    }

    // -- section 24: the template picker and the model catalog -----------------
    void buildingFromATemplateAdoptsTheDeviceAsOneUndoableCommand() {
        auto w = shown(ini("template_build.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        QCOMPARE(project->structure().region_count(), std::size_t(0));

        auto* panel = w->buildPanel();
        auto* buildTabs = panel->findChild<QTabWidget*>("buildTabs");
        QVERIFY(buildTabs);
        buildTabs->setCurrentIndex(2);  // Templates & Models -- triggers the lazy fetch
        auto* catalog = panel->catalogPanel();
        QVERIFY(catalog);
        auto* templateBox = catalog->findChild<QComboBox*>("catalogTemplateBox");
        QVERIFY(templateBox);
        QVERIFY(waitFor([&] { return templateBox->count() > 0; }, 15000));
        const int idx = templateBox->findText("Resistor");
        QVERIFY(idx >= 0);
        templateBox->setCurrentIndex(idx);

        auto* buildButton = catalog->findChild<QPushButton*>("catalogBuildButton");
        QVERIFY(buildButton);
        QTest::mouseClick(buildButton, Qt::LeftButton);
        QVERIFY(waitFor([&] { return project->structure().region_count() == std::size_t(1); }, 15000));
        QCOMPARE(project->structure().region(0).net_doping_cm3, 1e17);  // resistor's documented default
        QVERIFY(w->undoAction()->isEnabled());

        w->undoAction()->trigger();
        QCOMPARE(project->structure().region_count(), std::size_t(0));
    }

    void buildingFromATemplateSendsTheEditedParameterValues() {
        // Not "a bad parameter is refused": QDoubleSpinBox::setRange
        // (set from the template's own lo/hi, CatalogPanel::
        // rebuildParamForm) makes an out-of-range value UNREACHABLE
        // through this real widget -- Qt clamps it silently before the
        // click handler ever reads it, discovered while first writing
        // this test with a deliberately-invalid value that never
        // actually left the spin box. That is arguably the better
        // outcome (a bad value cannot even be entered), not a gap, so
        // this gates the thing this widget CAN prove instead: an
        // in-range edit actually reaches templates.build, rather than
        // the template silently building with only its defaults.
        auto w = shown(ini("template_build_edit.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();

        auto* panel = w->buildPanel();
        panel->findChild<QTabWidget*>("buildTabs")->setCurrentIndex(2);
        auto* catalog = panel->catalogPanel();
        auto* templateBox = catalog->findChild<QComboBox*>("catalogTemplateBox");
        QVERIFY(waitFor([&] { return templateBox->count() > 0; }, 15000));
        templateBox->setCurrentIndex(templateBox->findText("Resistor"));
        // The resistor's parameter form is dynamically built (no per-field
        // objectName): its first field is length_cm, its third is
        // doping_cm3 (length_cm, height_cm, doping_cm3, .... -- the exact
        // order TEMPLATES["resistor"]'s own params tuple declares).
        auto boxes = catalog->findChildren<QDoubleSpinBox*>();
        QVERIFY(boxes.size() >= 3);
        QCOMPARE(boxes[2]->value(), 1e17);  // the documented default, confirmed before editing it
        boxes[2]->setValue(-5e17);

        auto* buildButton = catalog->findChild<QPushButton*>("catalogBuildButton");
        QTest::mouseClick(buildButton, Qt::LeftButton);
        QVERIFY(waitFor([&] { return project->structure().region_count() == std::size_t(1); }, 15000));
        QCOMPARE(project->structure().region(0).net_doping_cm3, -5e17);
    }

    void theMeshNxFieldRejectsAFractionalValue() {
        // Resistor's params are length_cm, height_cm, doping_cm3, v_left,
        // v_right, nx, ny (templates.cpp's PLHI("nx", ...) call) -- nx is
        // integer=true, so CatalogPanel::rebuildParamForm must give its
        // spin box decimals(0), which makes typing a "." rejected by the
        // widget's own validator instead of reaching templates.build and
        // failing there with a message this field can't be traced to.
        auto w = shown(ini("template_build_nx.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();

        auto* panel = w->buildPanel();
        panel->findChild<QTabWidget*>("buildTabs")->setCurrentIndex(2);
        auto* catalog = panel->catalogPanel();
        auto* templateBox = catalog->findChild<QComboBox*>("catalogTemplateBox");
        QVERIFY(waitFor([&] { return templateBox->count() > 0; }, 15000));
        templateBox->setCurrentIndex(templateBox->findText("Resistor"));
        auto boxes = catalog->findChildren<QDoubleSpinBox*>();
        QVERIFY(boxes.size() >= 6);
        QDoubleSpinBox* nx = boxes[5];
        QCOMPARE(nx->decimals(), 0);

        nx->setFocus();
        nx->selectAll();
        QTest::keyClicks(nx, "40.5");
        nx->interpretText();  // commits the typed text the way losing focus/Enter would
        QVERIFY2(nx->value() != 40.5, qPrintable(nx->text()));
        QCOMPARE(nx->value(), std::floor(nx->value()));
    }

    void togglingAModelMarksTheProjectDirty() {
        auto w = shown(ini("model_toggle.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        ProjectController* project = w->projectController();
        QVERIFY(!project->isDirty());

        auto* panel = w->buildPanel();
        panel->findChild<QTabWidget*>("buildTabs")->setCurrentIndex(2);
        auto* catalog = panel->catalogPanel();
        auto* modelList = catalog->findChild<QListWidget*>("catalogModelList");
        QVERIFY(modelList);
        QVERIFY(waitFor([&] { return modelList->count() > 0; }, 15000));

        auto* item = modelList->item(0);
        const auto newState = item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
        item->setCheckState(newState);
        QVERIFY(waitFor([&] { return project->isDirty(); }, 5000));
        QVERIFY(project->modelsDirty());
        QVERIFY(!project->undoStack().is_dirty());  // model toggles are not undo-tracked (by design)
    }

    // -- section 24: project recent-files ---------------------------------------
    void savingAndOpeningAProjectAddsItToTheRecentProjectsMenu() {
        auto w = shown(ini("recent_projects.ini"));
        QVERIFY(w != nullptr);
        w->newBuildProject();
        const QString path = tmp_.filePath("recent_test_project.json");
        QVERIFY(w->saveBuildProject(path));

        auto* recentMenu = w->findChild<QMenu*>("ProjectRecentMenu");
        QVERIFY(recentMenu);
        emit recentMenu->aboutToShow();  // rebuildProjectRecentMenu() is lazy, on-show
        bool found = false;
        for (QAction* act : recentMenu->actions())
            if (act->toolTip() == path) found = true;
        QVERIFY(found);

        // Opening through the SAME entry (MainWindow::openBuildProject,
        // exactly what the menu action itself calls) must not raise it
        // to the front of its OWN entry list oddly or duplicate it.
        QVERIFY(w->openBuildProject(path));
        emit recentMenu->aboutToShow();
        int count = 0;
        for (QAction* act : recentMenu->actions())
            if (act->toolTip() == path) ++count;
        QCOMPARE(count, 1);
    }
};

QTEST_MAIN(TestBuildShell)
#include "test_build_shell.moc"

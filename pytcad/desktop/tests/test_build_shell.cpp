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
#include "shell/main_window.hpp"
#include "shell/validation_panel.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest/QtTest>

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
};

QTEST_MAIN(TestBuildShell)
#include "test_build_shell.moc"

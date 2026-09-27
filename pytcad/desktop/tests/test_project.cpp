// ProjectController tests (P4 S9, NATIVE-DESKTOP-PLAN.md section 20.4):
// the real backend_service's project.load/project.save, driven through
// ProjectController's own async signals. Run by
// gui/tests/test_desktop_project.py, which sets TCAD_TEST_PYTHON/
// TCAD_TEST_ROOT the same way test_backend.cpp's own Python wrapper does.
#include "run/project_controller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QtTest/QtTest>

using tcad::desktop::BackendClient;
using tcad::desktop::BackendConfig;
using tcad::desktop::ProjectController;

namespace {

QString env(const char* name) { return qEnvironmentVariable(name); }

BackendConfig real() {
    BackendConfig c;
    c.python = env("TCAD_TEST_PYTHON");
    c.working_dir = env("TCAD_TEST_ROOT");
    return c;
}

bool waitForSignal(QSignalSpy& spy, int ms) {
    if (!spy.isEmpty()) return true;
    QElapsedTimer t;
    t.start();
    while (spy.isEmpty() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !spy.isEmpty();
}

}  // namespace

class TestProject : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        for (const char* v : {"TCAD_TEST_PYTHON", "TCAD_TEST_ROOT"}) QVERIFY2(!env(v).isEmpty(), v);
    }

    void saveThenLoadRoundTripsThroughTheRealBackend() {
        BackendClient client(real());
        ProjectController controller([&client] { return &client; });

        controller.structure().add_region([] {
            tcad::desktop::RegionData r;
            r.id = "r1";
            r.name = "R1";
            r.x_max = 1e-4;
            r.y_max = 1e-4;
            r.net_doping_cm3 = 1e17;
            return r;
        }());
        controller.mesh().set_nx(30);
        controller.setName("Roundtrip");

        const QString path = QDir::temp().absoluteFilePath("tcad_project_controller_test.json");
        QSignalSpy savedSpy(&controller, &ProjectController::projectSaved);
        QSignalSpy saveFailedSpy(&controller, &ProjectController::projectSaveFailed);
        controller.save(path);
        QVERIFY(waitForSignal(savedSpy, 30000));
        QVERIFY2(saveFailedSpy.isEmpty(), "save failed unexpectedly");
        QVERIFY(!controller.isDirty());  // mark_clean() on a successful save

        ProjectController loader([&client] { return &client; });
        QSignalSpy loadedSpy(&loader, &ProjectController::projectLoaded);
        QSignalSpy loadFailedSpy(&loader, &ProjectController::projectLoadFailed);
        loader.load(path);
        QVERIFY(waitForSignal(loadedSpy, 30000));
        QVERIFY2(loadFailedSpy.isEmpty(), "load failed unexpectedly");

        QCOMPARE(loader.name(), QStringLiteral("Roundtrip"));
        QVERIFY(loader.hasStructure());
        QCOMPARE(loader.structure().region_count(), std::size_t(1));
        QCOMPARE(loader.structure().region(0).id, std::string("r1"));
        QVERIFY(loader.hasMesh());
        QCOMPARE(loader.mesh().nx(), 30);
        QCOMPARE(loader.process().step_count(), std::size_t(0));

        QFile::remove(path);
    }

    void loadingAMissingFileFailsWithANamedError() {
        BackendClient client(real());
        ProjectController controller([&client] { return &client; });
        QSignalSpy failedSpy(&controller, &ProjectController::projectLoadFailed);
        controller.load(QDir::temp().absoluteFilePath("tcad_project_controller_missing.json"));
        QVERIFY(waitForSignal(failedSpy, 30000));
        QCOMPARE(failedSpy.at(0).at(0).toString(), QStringLiteral("FileNotFoundError"));
    }

    void savingASchema6OnlyProjectAtSchema5IsRefused() {
        BackendClient client(real());
        ProjectController controller([&client] { return &client; });
        controller.setSpecVersion(2);
        const QString path = QDir::temp().absoluteFilePath("tcad_project_controller_refused.json");
        QFile::remove(path);
        QSignalSpy failedSpy(&controller, &ProjectController::projectSaveFailed);
        controller.save(path, /*target_version=*/5);
        QVERIFY(waitForSignal(failedSpy, 30000));
        QCOMPARE(failedSpy.at(0).at(0).toString(), QStringLiteral("IncompatibleDowngradeError"));
        QVERIFY(!QFile::exists(path));
    }

    void newProjectResetsEverythingAndTheUndoStack() {
        BackendClient client(real());
        ProjectController controller([&client] { return &client; });
        controller.setName("Something");
        controller.structure().set_width_cm(5.0);
        controller.undoStack().push(tcad::desktop::Command([] {}, [] {}, "noop"));
        QVERIFY(controller.isDirty());

        controller.newProject();
        QCOMPARE(controller.name(), QStringLiteral("Untitled"));
        QVERIFY(!controller.isDirty());
        QCOMPARE(controller.structure().width_cm(), 1e-4);
    }
};

QTEST_MAIN(TestProject)
#include "test_project.moc"

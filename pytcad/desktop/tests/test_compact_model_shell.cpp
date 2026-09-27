// Compact Model dock, in the real window (NATIVE-DESKTOP-PLAN.md section
// 25): the dock's form -> CompactModelController -> a real JobRunner
// subprocess running `python -m gui.services.compact_runner` (the M38
// Phase 4 extractor) -> the parsed JSON manifest back in the panel
// (netlist, converged flag, plotted fit curve). No TCAD_TEST_DATA fixture
// is needed: compact_runner.py builds its own Device1D/Device2D from the
// form's scalar geometry, unlike the Run dock's DeviceSpec-based path.
#include "run/compact_model_controller.hpp"
#include "shell/app_settings.hpp"
#include "shell/compact_model_panel.hpp"
#include "shell/main_window.hpp"
#include "views/plot/plot_view.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

using tcad::desktop::AppSettings;
using tcad::desktop::CompactModelController;
using tcad::desktop::CompactModelPanel;
using tcad::desktop::MainWindow;

class TestCompactModelShell : public QObject {
    Q_OBJECT

    QTemporaryDir tmp_;

    std::unique_ptr<MainWindow> window(const QString& name) {
        auto settings = AppSettings::atFile(tmp_.filePath(name + ".ini"));
        settings->setValue("run/dir", tmp_.filePath("runs_" + name));
        auto w = std::make_unique<MainWindow>(std::move(settings));
        w->show();
        if (!QTest::qWaitForWindowExposed(w.get())) return nullptr;
        return w;
    }

private slots:
    void initTestCase() { QVERIFY(tmp_.isValid()); }

    // -- the lazy backend rule, kept -------------------------------------------------
    void thePanelStartsNoPythonUntilExtractIsClicked() {
        auto w = window("lazy");
        QVERIFY(w);
        QTest::qWait(500);
        QVERIFY(!w->compactModelController()->busy());
        QVERIFY(!w->backendClient());
    }

    void extractingADiodeFitsAConvergedNetlist() {
        auto w = window("diode");
        QVERIFY(w);
        CompactModelPanel* p = w->compactModelPanel();
        QVERIFY(p);
        p->kindCombo()->setCurrentIndex(p->kindCombo()->findData("diode"));
        QSignalSpy finished(w->compactModelController(), &CompactModelController::finished);
        QSignalSpy failed(w->compactModelController(), &CompactModelController::failed);
        QTest::mouseClick(p->extractButton(), Qt::LeftButton);
        QVERIFY(w->compactModelController()->busy());
        QVERIFY2(QTest::qWaitFor([&] { return finished.count() > 0 || failed.count() > 0; }, 180000),
                 "extraction did not end");
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(0).toString() +
                                                                       ": " + failed.first().at(1).toString()));
        QVERIFY(!w->compactModelController()->busy());
        const QString netlist = p->netlistView()->toPlainText();
        QVERIFY2(netlist.contains(".MODEL") && netlist.contains(" D "), qPrintable(netlist));
        QVERIFY2(p->statusLabel()->text().contains("converged"), qPrintable(p->statusLabel()->text()));
        QVERIFY(!p->plotView()->model().series.empty());
    }

    void extractingAMosfetFitsAConvergedNetlist() {
        auto w = window("mosfet");
        QVERIFY(w);
        CompactModelPanel* p = w->compactModelPanel();
        p->kindCombo()->setCurrentIndex(p->kindCombo()->findData("mosfet1"));
        QSignalSpy finished(w->compactModelController(), &CompactModelController::finished);
        QSignalSpy failed(w->compactModelController(), &CompactModelController::failed);
        QTest::mouseClick(p->extractButton(), Qt::LeftButton);
        QVERIFY2(QTest::qWaitFor([&] { return finished.count() > 0 || failed.count() > 0; }, 240000),
                 "extraction did not end");
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(0).toString() +
                                                                       ": " + failed.first().at(1).toString()));
        const QString netlist = p->netlistView()->toPlainText();
        QVERIFY2(netlist.contains(".MODEL"), qPrintable(netlist));
        QVERIFY(!p->plotView()->model().series.empty());
    }

    void invalidNumberIsRefusedBeforeAnythingStarts() {
        auto w = window("bad");
        QVERIFY(w);
        CompactModelPanel* p = w->compactModelPanel();
        p->kindCombo()->setCurrentIndex(p->kindCombo()->findData("diode"));
        p->field("DiodeLUm")->setText("not a number");
        QSignalSpy rejected(p, &CompactModelPanel::inputRejected);
        QTest::mouseClick(p->extractButton(), Qt::LeftButton);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!w->compactModelController()->busy());
    }
};

QTEST_MAIN(TestCompactModelShell)
#include "test_compact_model_shell.moc"

// P4 S2 gates (NATIVE-DESKTOP-PLAN.md section 20.4): MeshEditor and
// StructureEditorView, driven directly through their public/testable
// methods rather than a real mouse cursor -- QTest::mouseMove moves the
// actual OS cursor (breaks under another focused window; see this
// project's own recorded gotcha), so drag interaction here goes through
// beginDragAt/dragTo/endDrag, the same methods the real mouse handlers
// call, exactly as plot_view.hpp's hoverAt() is tested.
#include "document/mesh_document.hpp"
#include "document/process_document.hpp"
#include "document/structure_document.hpp"
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

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtTest/QtTest>

#include <cmath>

using tcad::desktop::AnnealStepEditor;
using tcad::desktop::BoundaryData;
using tcad::desktop::ContactData;
using tcad::desktop::ContactEditor;
using tcad::desktop::ContactListWidget;
using tcad::desktop::DopingEditor;
using tcad::desktop::GateData;
using tcad::desktop::GateEditor;
using tcad::desktop::GateListWidget;
using tcad::desktop::ImplantStepEditor;
using tcad::desktop::MeshDocument;
using tcad::desktop::MeshEditor;
using tcad::desktop::OxidizeStepEditor;
using tcad::desktop::ProcessFlowDocument;
using tcad::desktop::ProcessStepData;
using tcad::desktop::ProcessStepListWidget;
using tcad::desktop::RegionData;
using tcad::desktop::RegionListWidget;
using tcad::desktop::StructureDocument;
using tcad::desktop::StructureEditorView;
using tcad::desktop::SubstrateStepEditor;

namespace {

StructureDocument make_structure() {
    auto d = StructureDocument::parse(R"({"width_cm": 0.01, "height_cm": 0.01})");
    RegionData r;
    r.id = "r1";
    r.name = "R1";
    // On the uniform mesh's own nodes (nx=ny=5 over 0.01 cm -> spacing
    // 0.0025 cm), so a one-spacing drag has an exact, unambiguous
    // snapped result -- an off-node start makes "nearest node" land
    // somewhere a naive delta calculation would not predict (found by
    // running this test for real: a first version of this fixture used
    // 0.001-0.009, and 0.001 is not a mesh node).
    r.x_min = 0.0025;
    r.x_max = 0.0075;
    r.y_min = 0.0025;
    r.y_max = 0.0075;
    r.net_doping_cm3 = 1e16;
    d.add_region(r);
    ContactData c;
    c.id = "c1";
    c.name = "C1";
    c.boundary = BoundaryData{"left", std::nullopt, std::nullopt};
    d.add_contact(c);
    return d;
}

StructureDocument make_structure_3d() {
    auto d = StructureDocument::parse(R"({"width_cm": 0.01, "height_cm": 0.01, "depth_cm": 0.002})");
    RegionData r;
    r.id = "r1";
    r.name = "R1";
    r.x_min = 0.0025;
    r.x_max = 0.0075;
    r.y_min = 0.0025;
    r.y_max = 0.0075;
    r.z_min = 0.0;
    r.z_max = 0.002;
    r.net_doping_cm3 = 1e16;
    d.add_region(r);
    return d;
}

// S4's own gate: "a gated (MOSFET-style) fixture with contacts and a gate."
StructureDocument make_mosfet_style_structure() {
    auto d = StructureDocument::parse(R"({"width_cm": 0.01, "height_cm": 0.01})");
    ContactData source;
    source.id = "source";
    source.name = "Source";
    source.boundary = BoundaryData{"left", std::nullopt, std::nullopt};
    source.V = 0.0;
    d.add_contact(source);
    ContactData drain;
    drain.id = "drain";
    drain.name = "Drain";
    drain.boundary = BoundaryData{"right", std::nullopt, std::nullopt};
    drain.V = 1.0;
    d.add_contact(drain);
    GateData gate;
    gate.id = "gate";
    gate.name = "Gate";
    gate.boundary = BoundaryData{"top", std::nullopt, std::nullopt};
    gate.tox_cm = 2e-6;  // 20 nm
    gate.V = 0.0;
    d.add_gate(gate);
    return d;
}

ProcessFlowDocument make_process_flow() {
    auto d = ProcessFlowDocument::parse(
        R"({"steps": [{"id": "p1", "name": "Substrate", "operation": "substrate",)"
        R"(   "enabled": true, "parameters": {"length_cm": 1e-3, "background_doping_cm3": 1e15,)"
        R"(   "mesh": {"h_min_cm": 1e-7, "h_max_cm": 1e-5, "ratio": 1.2}}}]})");
    return d;
}

MeshDocument make_mesh() {
    auto d = MeshDocument::parse(R"({})");  // defaults, then overridden below
    d.set_nx(5);
    d.set_ny(5);
    return d;  // grading defaults to "uniform"
}

}  // namespace

class TestEditors : public QObject {
    Q_OBJECT

private slots:
    // -- MeshEditor -----------------------------------------------------

    void meshEditorDisabledWithoutADocument() {
        MeshEditor editor;
        QVERIFY(!editor.findChild<QSpinBox*>("nx")->isEnabled());
    }

    void meshEditorReflectsTheDocument() {
        auto mesh = make_mesh();
        MeshEditor editor;
        editor.setDocument(&mesh);
        QVERIFY(editor.findChild<QSpinBox*>("nx")->isEnabled());
        QCOMPARE(editor.findChild<QSpinBox*>("nx")->value(), 5);
        QCOMPARE(editor.findChild<QSpinBox*>("ny")->value(), 5);
        QCOMPARE(editor.findChild<QComboBox*>("grading")->currentText(), QString("uniform"));
        QCOMPARE(editor.findChild<QSpinBox*>("nz")->value(), 0);  // None -> the "(2D)" sentinel
    }

    void meshEditorWritesFieldEditsIntoTheDocument() {
        auto mesh = make_mesh();
        MeshEditor editor;
        editor.setDocument(&mesh);
        QSignalSpy spy(&editor, &MeshEditor::meshEdited);

        auto* nx = editor.findChild<QSpinBox*>("nx");
        nx->setValue(55);
        nx->editingFinished();  // Qt signals are ordinary callable functions; no real key event needed
        QCOMPARE(mesh.nx(), 55);
        QCOMPARE(spy.count(), 1);

        auto* nz = editor.findChild<QSpinBox*>("nz");
        nz->setValue(12);
        nz->editingFinished();
        QCOMPARE(mesh.nz().value(), 12);
        nz->setValue(0);
        nz->editingFinished();
        QVERIFY(!mesh.nz().has_value());

        editor.findChild<QComboBox*>("grading")->setCurrentText("graded");
        QCOMPARE(mesh.grading(), std::string("graded"));
    }

    // -- StructureEditorView --------------------------------------------

    void structureEditorViewMapsPixelsToMeshSpaceAndBack() {
        auto structure = make_structure();
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);  // domainRect: a 24px margin -> a 200x200 square (1:1 aspect)
        view.setDocuments(&structure, &mesh);

        const QRectF dom = view.domainRect();
        QCOMPARE(dom.width(), dom.height());  // width_cm == height_cm above
        const QPointF center_px = view.toPixel(0.005, 0.005);
        QVERIFY(dom.contains(center_px));
        const QPointF back = view.toMeshSpace(center_px);
        QVERIFY(std::abs(back.x() - 0.005) < 1e-9);
        QVERIFY(std::abs(back.y() - 0.005) < 1e-9);
    }

    void structureEditorViewHitTestsTopmostRegionFirst() {
        auto structure = make_structure();  // one region, 0.001-0.009 both axes
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&structure, &mesh);

        QCOMPARE(view.hitTestRegion(view.toPixel(0.005, 0.005)), QString("r1"));
        QVERIFY(view.hitTestRegion(view.toPixel(0.0001, 0.0001)).isEmpty());  // outside the region
    }

    void structureEditorViewSnapsToTheUniformMesh() {
        auto structure = make_structure();
        auto mesh = make_mesh();  // nx=ny=5 -> axis {0, 0.0025, 0.005, 0.0075, 0.01}
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&structure, &mesh);
        QCOMPARE(view.snapX(0.0026), 0.0025);
        QCOMPARE(view.snapY(0.0074), 0.0075);

        mesh.set_grading("graded");
        QCOMPARE(view.snapX(0.0026), 0.0026);  // S2's disclosed scope: no snap for a graded mesh
    }

    void structureEditorViewDragMovesTheRegionAndEmitsSignals() {
        auto structure = make_structure();
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&structure, &mesh);
        QSignalSpy selected(&view, &StructureEditorView::selectionChanged);
        QSignalSpy edited(&view, &StructureEditorView::regionEdited);

        const QPointF start = view.toPixel(0.005, 0.005);  // inside the region body -> Move
        view.beginDragAt(start);
        QCOMPARE(view.selectedRegionId(), QString("r1"));
        QCOMPARE(selected.count(), 1);
        QVERIFY(view.isDragging());

        const RegionData before = structure.region(0);
        view.dragTo(view.toPixel(0.0075, 0.005));  // +0.0025 cm in x, snaps to the same mesh spacing
        const RegionData moved = structure.region(0);
        QCOMPARE(moved.x_min - before.x_min, 0.0025);
        QCOMPARE(moved.x_max - before.x_max, 0.0025);
        QCOMPARE(moved.y_min, before.y_min);  // unchanged: no y motion in this drag

        view.endDrag();
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.at(0).at(0).toString(), QString("r1"));
        QVERIFY(!view.isDragging());
    }

    void structureEditorViewResizesFromTheRightEdgeOnly() {
        auto structure = make_structure();  // r1: x_max = 0.0075
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&structure, &mesh);

        const QPointF right_edge = view.toPixel(0.0075, 0.005);  // on the region's right edge
        view.beginDragAt(right_edge);
        const RegionData before = structure.region(0);
        view.dragTo(view.toPixel(0.0125, 0.005));  // drag the edge further right
        const RegionData resized = structure.region(0);
        QVERIFY(resized.x_max > before.x_max);
        QCOMPARE(resized.x_min, before.x_min);  // the opposite edge does not move
        view.endDrag();
    }

    void structureEditorViewSelectingEmptySpaceClearsSelection() {
        auto structure = make_structure();
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&structure, &mesh);
        view.beginDragAt(view.toPixel(0.005, 0.005));
        QCOMPARE(view.selectedRegionId(), QString("r1"));
        view.beginDragAt(view.toPixel(0.0001, 0.0001));  // outside every region
        QVERIFY(view.selectedRegionId().isEmpty());
        QVERIFY(!view.isDragging());
    }

    void structureEditorViewHoverReportsMeshSpaceCoordinates() {
        auto structure = make_structure();
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&structure, &mesh);
        QVERIFY(view.readout().isEmpty());
        view.hoverAt(view.toPixel(0.005, 0.005));
        QVERIFY(view.readout().contains("50.000"));  // 0.005 cm = 50 um
    }

    // -- RegionListWidget -------------------------------------------------

    void regionListWidgetDisabledWithoutADocument() {
        RegionListWidget list;
        QVERIFY(!list.findChild<QListWidget*>("regionList")->isEnabled());
    }

    void regionListWidgetAddsAFullDomainRegionAndSelectsIt() {
        auto structure = StructureDocument::parse(R"({"width_cm": 0.02, "height_cm": 0.01})");
        RegionListWidget list;
        list.setDocument(&structure);
        QSignalSpy changed(&list, &RegionListWidget::regionsChanged);

        list.findChild<QPushButton*>("addRegion")->click();
        QCOMPARE(structure.region_count(), std::size_t(1));
        const RegionData r = structure.region(0);
        QCOMPARE(r.x_min, 0.0);
        QCOMPARE(r.x_max, 0.02);
        QCOMPARE(r.y_max, 0.01);
        QCOMPARE(r.net_doping_cm3, 1e15);
        QCOMPARE(list.selectedRegionId(), QString::fromStdString(r.id));
        QCOMPARE(changed.count(), 1);

        // A second Add gets a DIFFERENT id -- ids are not gated for exact
        // equality against Python (they are random on both sides, the
        // same disclosed exception ProcessFlowDocument's duplicate_step
        // has in S1), only for being distinct and well-formed (8 hex chars).
        list.findChild<QPushButton*>("addRegion")->click();
        QCOMPARE(structure.region_count(), std::size_t(2));
        QVERIFY(structure.region(0).id != structure.region(1).id);
        QCOMPARE(QString::fromStdString(structure.region(1).id).length(), 8);
    }

    void regionListWidgetRemovesAndReordersBySelection() {
        auto structure = make_structure();  // one region, "r1"
        RegionData r2;
        r2.id = "r2";
        r2.name = "R2";
        r2.net_doping_cm3 = -1e15;
        structure.add_region(r2);
        RegionListWidget list;
        list.setDocument(&structure);

        list.selectRegion("r2");
        list.findChild<QPushButton*>("moveRegionUp")->click();
        QCOMPARE(structure.region(0).id, std::string("r2"));  // moved ahead of r1

        list.findChild<QPushButton*>("removeRegion")->click();  // r2 still selected
        QCOMPARE(structure.region_count(), std::size_t(1));
        QCOMPARE(structure.region(0).id, std::string("r1"));
    }

    // -- DopingEditor -------------------------------------------------------

    void dopingEditorDisabledWithoutARegion() {
        DopingEditor editor;
        QVERIFY(!editor.findChild<QLineEdit*>("regionName")->isEnabled());
    }

    void dopingEditorLoadsTheSelectedRegionsFields() {
        auto structure = make_structure();  // "r1", x_min=0.0025 etc, net_doping_cm3=1e16
        DopingEditor editor;
        editor.setRegion(&structure, "r1");
        QCOMPARE(editor.findChild<QLineEdit*>("regionName")->text(), QString("R1"));
        QCOMPARE(editor.findChild<QLabel*>("regionMaterial")->text(), QString("SILICON"));
        QCOMPARE(editor.findChild<QDoubleSpinBox*>("x_min")->value(), 0.0025);
        QCOMPARE(editor.findChild<QDoubleSpinBox*>("net_doping_cm3")->value(), 1e16);
    }

    void dopingEditorWritesFieldEditsBackIntoTheDocument() {
        auto structure = make_structure();
        DopingEditor editor;
        editor.setRegion(&structure, "r1");
        QSignalSpy edited(&editor, &DopingEditor::regionEdited);

        auto* net = editor.findChild<QDoubleSpinBox*>("net_doping_cm3");
        net->setValue(-2e16);
        net->editingFinished();
        QCOMPARE(structure.region(0).net_doping_cm3, -2e16);
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.at(0).at(0).toString(), QString("r1"));

        auto* xmax = editor.findChild<QDoubleSpinBox*>("x_max");
        xmax->setValue(0.008);
        xmax->editingFinished();
        QCOMPARE(structure.region(0).x_max, 0.008);
        // fields this edit did not touch survive unchanged
        QCOMPARE(structure.region(0).y_min, 0.0025);
    }

    void dopingEditorHidesZFieldsFor2DAndProfileFieldsForUniform() {
        auto structure2d = make_structure();
        DopingEditor editor;
        editor.setRegion(&structure2d, "r1");
        auto* form = editor.findChild<QFormLayout*>();
        QVERIFY(form);
        QVERIFY(!form->isRowVisible(editor.findChild<QDoubleSpinBox*>("z_min")));
        QVERIFY(!form->isRowVisible(editor.findChild<QDoubleSpinBox*>("profile_peak_cm3")));

        auto structure3d = make_structure_3d();
        editor.setRegion(&structure3d, "r1");
        QVERIFY(form->isRowVisible(editor.findChild<QDoubleSpinBox*>("z_min")));

        editor.findChild<QComboBox*>("doping_profile")->setCurrentText("gaussian_erfc");
        QVERIFY(form->isRowVisible(editor.findChild<QDoubleSpinBox*>("profile_peak_cm3")));
        QCOMPARE(structure3d.region(0).doping_profile, std::string("gaussian_erfc"));
    }

    // -- S3's own gate: the canvas and the form must agree with each other --

    void canvasDragAndFormEditProduceTheSameRegionState() {
        auto viaCanvas = make_structure();
        auto mesh = make_mesh();
        StructureEditorView view;
        view.resize(248, 248);
        view.setDocuments(&viaCanvas, &mesh);
        view.beginDragAt(view.toPixel(0.005, 0.005));   // inside r1's body
        view.dragTo(view.toPixel(0.0075, 0.005));       // +0.0025 cm, snapped (see the drag test above)
        view.endDrag();
        const RegionData byCanvas = viaCanvas.region(0);

        auto viaForm = make_structure();
        DopingEditor editor;
        editor.setRegion(&viaForm, "r1");
        auto setNum = [&](const char* name, double v) {
            auto* box = editor.findChild<QDoubleSpinBox*>(name);
            box->setValue(v);
            box->editingFinished();
        };
        setNum("x_min", byCanvas.x_min);
        setNum("x_max", byCanvas.x_max);
        const RegionData byForm = viaForm.region(0);

        QCOMPARE(byForm.x_min, byCanvas.x_min);
        QCOMPARE(byForm.x_max, byCanvas.x_max);
        QCOMPARE(byForm.y_min, byCanvas.y_min);
        QCOMPARE(byForm.y_max, byCanvas.y_max);
        QCOMPARE(byForm.net_doping_cm3, byCanvas.net_doping_cm3);
    }

    // -- ContactListWidget / ContactEditor, GateListWidget / GateEditor --
    // (the MOSFET-style fixture S4's own gate names: contacts + a gate)

    void contactListWidgetSelectsAndFeedsTheEditor() {
        auto structure = make_mosfet_style_structure();
        ContactListWidget list;
        list.setDocument(&structure);
        ContactEditor editor;
        connect(&list, &ContactListWidget::selectionChanged, &editor,
               [&](const QString& id) { editor.setContact(&structure, id); });
        list.selectContact("drain");
        QCOMPARE(editor.contactId(), QString("drain"));
        QCOMPARE(editor.findChild<QLabel*>("contactName")->text(), QString("Drain"));
        QCOMPARE(editor.findChild<QLabel*>("contactEdge")->text(), QString("right"));
        QCOMPARE(editor.findChild<QDoubleSpinBox*>("contactVoltage")->value(), 1.0);
    }

    void contactEditorWritesVoltageOnly() {
        auto structure = make_mosfet_style_structure();
        ContactEditor editor;
        editor.setContact(&structure, "source");
        QSignalSpy edited(&editor, &ContactEditor::contactEdited);

        auto* v = editor.findChild<QDoubleSpinBox*>("contactVoltage");
        v->setValue(0.05);
        v->editingFinished();
        QCOMPARE(structure.contact(0).V, 0.05);
        QCOMPARE(edited.count(), 1);
        // name and edge are read-only in this editor -- unchanged
        QCOMPARE(structure.contact(0).name, std::string("Source"));
        QCOMPARE(structure.contact(0).boundary.edge, std::string("left"));
    }

    void gateListWidgetSelectsAndFeedsTheEditor() {
        auto structure = make_mosfet_style_structure();
        GateListWidget list;
        list.setDocument(&structure);
        GateEditor editor;
        connect(&list, &GateListWidget::selectionChanged, &editor,
               [&](const QString& id) { editor.setGate(&structure, id); });
        list.selectGate("gate");
        QCOMPARE(editor.gateId(), QString("gate"));
        QCOMPARE(editor.findChild<QLabel*>("gateName")->text(), QString("Gate"));
        QVERIFY(std::abs(editor.findChild<QDoubleSpinBox*>("gateToxNm")->value() - 20.0) < 1e-6);
    }

    void gateEditorWritesToxVoltageAndVfb() {
        auto structure = make_mosfet_style_structure();
        GateEditor editor;
        editor.setGate(&structure, "gate");
        QSignalSpy edited(&editor, &GateEditor::gateEdited);

        auto* tox = editor.findChild<QDoubleSpinBox*>("gateToxNm");
        tox->setValue(15.0);
        tox->editingFinished();
        QVERIFY(std::abs(structure.gate(0).tox_cm - 15e-7) < 1e-15);
        QCOMPARE(edited.count(), 1);

        auto* form = editor.findChild<QFormLayout*>();
        auto* manual = editor.findChild<QDoubleSpinBox*>("gateVfbManual");
        QVERIFY(!form->isRowVisible(manual));  // "computed" is the default -> hidden

        editor.findChild<QComboBox*>("gateVfbMode")->setCurrentText("manual");
        QVERIFY(form->isRowVisible(manual));
        QCOMPARE(structure.gate(0).vfb_mode, std::string("manual"));

        manual->setValue(-0.8);
        manual->editingFinished();
        QCOMPARE(structure.gate(0).vfb_manual.value(), -0.8);
    }

    // -- ProcessStepListWidget + the four step editors ---------------------

    void processStepListWidgetAddsStepsWithQmlsOwnDefaultParameters() {
        auto flow = make_process_flow();  // one step, "p1" substrate
        ProcessStepListWidget list;
        list.setDocument(&flow);
        QSignalSpy changed(&list, &ProcessStepListWidget::stepsChanged);

        list.findChild<QComboBox*>("processOperationBox")->setCurrentText("implant");
        list.findChild<QPushButton*>("addProcessStep")->click();
        QCOMPARE(flow.step_count(), std::size_t(2));
        const ProcessStepData added = flow.step(1);
        QCOMPARE(added.operation, std::string("implant"));
        QCOMPARE(added.enabled, true);
        // ProcessPanel.qml's own default implant parameters, exactly:
        QCOMPARE(added.parameters.at("species").get<std::string>(), std::string("B"));
        QCOMPARE(added.parameters.at("energy_keV").get<double>(), 30.0);
        QCOMPARE(added.parameters.at("dose_cm2").get<double>(), 1e13);
        QCOMPARE(added.parameters.at("tilt_deg").get<double>(), 7.0);
        QCOMPARE(list.selectedStepId(), QString::fromStdString(added.id));
        QCOMPARE(changed.count(), 1);
    }

    void processStepListWidgetReordersDuplicatesTogglesAndRemoves() {
        auto flow = make_process_flow();
        ProcessStepListWidget list;
        list.setDocument(&flow);

        list.findChild<QComboBox*>("processOperationBox")->setCurrentText("anneal");
        list.findChild<QPushButton*>("addProcessStep")->click();  // "p1", then the new anneal step
        QCOMPARE(flow.step_count(), std::size_t(2));
        const QString annealId = list.selectedStepId();

        list.findChild<QPushButton*>("moveProcessStepUp")->click();
        QCOMPARE(flow.step(0).id, annealId.toStdString());

        list.findChild<QPushButton*>("duplicateProcessStep")->click();
        QCOMPARE(flow.step_count(), std::size_t(3));
        const QString dupId = list.selectedStepId();
        QVERIFY(dupId != annealId);
        QCOMPARE(flow.step(1).id, dupId.toStdString());  // inserted right after the original

        list.findChild<QCheckBox*>("processStepEnabled")->setChecked(false);
        bool found = false;
        for (std::size_t i = 0; i < flow.step_count(); ++i)
            if (flow.step(i).id == dupId.toStdString()) { QVERIFY(!flow.step(i).enabled); found = true; }
        QVERIFY(found);

        list.findChild<QPushButton*>("removeProcessStep")->click();
        QCOMPARE(flow.step_count(), std::size_t(2));
    }

    void substrateStepEditorLoadsAndWritesFields() {
        auto flow = make_process_flow();
        SubstrateStepEditor editor;
        editor.setStep(&flow, "p1");
        QCOMPARE(editor.findChild<QDoubleSpinBox*>("substrateLengthCm")->value(), 1e-3);

        auto* len = editor.findChild<QDoubleSpinBox*>("substrateLengthCm");
        len->setValue(2e-3);
        len->editingFinished();
        QCOMPARE(flow.step(0).parameters.at("length_cm").get<double>(), 2e-3);
        // untouched fields, including the nested mesh object, survive:
        QCOMPARE(flow.step(0).parameters.at("mesh").at("ratio").get<double>(), 1.2);
    }

    void implantStepEditorTogglesTheWindow() {
        auto flow = make_process_flow();
        flow.add_step(ProcessStepData{"p2", "Implant", "implant", true,
                                      {{"species", "P"}, {"energy_keV", 60}, {"dose_cm2", 1e14},
                                       {"tilt_deg", 0}}});
        ImplantStepEditor editor;
        editor.setStep(&flow, "p2");
        auto* form = editor.findChild<QFormLayout*>();
        auto* from = editor.findChild<QDoubleSpinBox*>("implantWindowFromUm");
        QVERIFY(!form->isRowVisible(from));  // no window yet -> hidden

        editor.findChild<QCheckBox*>("implantWindowEnabled")->setChecked(true);
        QVERIFY(form->isRowVisible(from));
        from->setValue(1.0);
        from->editingFinished();
        editor.findChild<QDoubleSpinBox*>("implantWindowToUm")->setValue(3.0);
        editor.findChild<QDoubleSpinBox*>("implantWindowToUm")->editingFinished();

        const auto params = flow.step(1).parameters;
        QVERIFY(std::abs(params.at("x_range_cm").at(0).get<double>() - 1e-4) < 1e-12);
        QVERIFY(std::abs(params.at("x_range_cm").at(1).get<double>() - 3e-4) < 1e-12);

        editor.findChild<QCheckBox*>("implantWindowEnabled")->setChecked(false);
        QVERIFY(!flow.step(1).parameters.contains("x_range_cm"));
    }

    void annealAndOxidizeStepEditorsWriteTheirFields() {
        auto flow = make_process_flow();
        flow.add_step(ProcessStepData{"p2", "Anneal", "anneal", true, {{"temperature_C", 900}, {"time_s", 60}}});
        flow.add_step(ProcessStepData{"p3", "Oxidize", "oxidize", true,
                                      {{"temperature_C", 1000}, {"time_hours", 0.5}, {"ambient", "dry"}}});

        AnnealStepEditor anneal;
        anneal.setStep(&flow, "p2");
        auto* t = anneal.findChild<QDoubleSpinBox*>("annealTemperatureC");
        t->setValue(950.0);
        t->editingFinished();
        QCOMPARE(flow.step(1).parameters.at("temperature_C").get<double>(), 950.0);

        OxidizeStepEditor oxidize;
        oxidize.setStep(&flow, "p3");
        QCOMPARE(oxidize.findChild<QComboBox*>("oxidizeAmbient")->currentText(), QString("dry"));
        oxidize.findChild<QComboBox*>("oxidizeAmbient")->setCurrentText("wet");
        QCOMPARE(flow.step(2).parameters.at("ambient").get<std::string>(), std::string("wet"));
    }
};

QTEST_MAIN(TestEditors)
#include "test_editors.moc"

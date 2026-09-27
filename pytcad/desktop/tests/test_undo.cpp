// P4 S8 (NATIVE-DESKTOP-PLAN.md section 20.4): the UndoStack/Command
// port. Gate: a scripted sequence of edits across region/contact/gate/
// mesh/process, undo all, redo all, checked byte-identical to the start
// and end states respectively AT EACH STEP -- not just the final one
// (the plan's own stated gate).
#include <QtTest/QtTest>

#include "document/mesh_document.hpp"
#include "document/process_document.hpp"
#include "document/structure_document.hpp"
#include "document/undo_stack.hpp"

using tcad::desktop::Command;
using tcad::desktop::ContactData;
using tcad::desktop::GateData;
using tcad::desktop::make_command;
using tcad::desktop::MeshDocument;
using tcad::desktop::ProcessFlowDocument;
using tcad::desktop::ProcessStepData;
using tcad::desktop::RegionData;
using tcad::desktop::StructureDocument;
using tcad::desktop::UndoStack;

namespace {

RegionData region(const char* id, double doping) {
    RegionData r;
    r.id = id;
    r.name = id;
    r.x_max = 1e-4;
    r.y_max = 1e-4;
    r.net_doping_cm3 = doping;
    return r;
}

ProcessStepData substrate_step(const char* id) {
    ProcessStepData s;
    s.id = id;
    s.name = "Substrate";
    s.operation = "substrate";
    return s;
}

}  // namespace

class TestUndo : public QObject {
    Q_OBJECT

private slots:
    void basicUndoRedoStackSemantics() {
        // Mirrors gui/services/undo_stack.py's own contract exactly:
        // push clears redo, undo/redo move commands between the two
        // stacks, mark_clean/is_dirty track a clean INDEX (not a bool),
        // so undoing back to it (even past other redo activity) is
        // clean again.
        UndoStack stack;
        QVERIFY(!stack.can_undo());
        QVERIFY(!stack.can_redo());
        QVERIFY(!stack.is_dirty());

        auto doc = StructureDocument::parse(R"({"width_cm": 1.0, "height_cm": 2.0})");
        stack.push(make_command(doc, "add r1", [](StructureDocument& d) { d.add_region(region("r1", 1e16)); }));
        QCOMPARE(doc.region_count(), std::size_t(1));
        QVERIFY(stack.can_undo());
        QVERIFY(!stack.can_redo());
        QVERIFY(stack.is_dirty());

        stack.mark_clean();
        QVERIFY(!stack.is_dirty());

        stack.push(make_command(doc, "add r2", [](StructureDocument& d) { d.add_region(region("r2", -1e15)); }));
        QCOMPARE(doc.region_count(), std::size_t(2));
        QVERIFY(stack.is_dirty());

        stack.undo();
        QCOMPARE(doc.region_count(), std::size_t(1));
        QVERIFY(!stack.is_dirty());  // back at the clean index

        stack.undo();
        QCOMPARE(doc.region_count(), std::size_t(0));
        QVERIFY(stack.is_dirty());  // one past the clean index, the other way
        QVERIFY(!stack.can_undo());

        // pushing after undoing clears the redo stack (Python's own rule).
        stack.redo();
        QVERIFY(stack.can_redo());
        stack.push(make_command(doc, "add r3", [](StructureDocument& d) { d.add_region(region("r3", 1e14)); }));
        QVERIFY(!stack.can_redo());
    }

    void scriptedSequenceAcrossEveryDocumentType_undoAllRedoAll() {
        auto structure = StructureDocument::parse(
            R"({"width_cm": 1e-4, "height_cm": 1e-4, "regions": [], "contacts": [], "gates": []})");
        auto mesh = MeshDocument::parse(R"({})");
        auto process = ProcessFlowDocument::parse(R"({"steps": []})");

        UndoStack stack;
        std::vector<std::string> structure_history{structure.dump()};
        std::vector<std::string> mesh_history{mesh.dump()};
        std::vector<std::string> process_history{process.dump()};

        auto push_structure = [&](const char* label, auto mutate) {
            stack.push(make_command(structure, label, mutate));
            structure_history.push_back(structure.dump());
            mesh_history.push_back(mesh.dump());
            process_history.push_back(process.dump());
        };
        auto push_mesh = [&](const char* label, auto mutate) {
            stack.push(make_command(mesh, label, mutate));
            structure_history.push_back(structure.dump());
            mesh_history.push_back(mesh.dump());
            process_history.push_back(process.dump());
        };
        auto push_process = [&](const char* label, auto mutate) {
            stack.push(make_command(process, label, mutate));
            structure_history.push_back(structure.dump());
            mesh_history.push_back(mesh.dump());
            process_history.push_back(process.dump());
        };

        // region, contact, gate, mesh, process -- one mutation of each
        // kind, in that order (the plan's own gate list).
        push_structure("add region", [](StructureDocument& d) { d.add_region(region("r1", 1e17)); });
        push_structure("add contact", [](StructureDocument& d) {
            ContactData c;
            c.id = "c1";
            c.name = "C1";
            c.boundary.edge = "left";
            d.add_contact(c);
        });
        push_structure("add gate", [](StructureDocument& d) {
            GateData g;
            g.id = "g1";
            g.name = "G1";
            g.boundary.edge = "top";
            g.tox_cm = 1e-6;
            d.add_gate(g);
        });
        push_mesh("set nx", [](MeshDocument& d) { d.set_nx(80); });
        push_process("add step", [](ProcessFlowDocument& d) { d.add_step(substrate_step("s1")); });

        QCOMPARE(structure.region_count(), std::size_t(1));
        QCOMPARE(structure.contact_count(), std::size_t(1));
        QCOMPARE(structure.gate_count(), std::size_t(1));
        QCOMPARE(mesh.nx(), 80);
        QCOMPARE(process.step_count(), std::size_t(1));

        const std::size_t n = structure_history.size();  // 6: initial + 5 edits
        QCOMPARE(n, std::size_t(6));

        // undo all: after undoing k commands, state must match history[n-1-k].
        for (std::size_t k = 1; k < n; ++k) {
            stack.undo();
            QVERIFY(structure.json() == StructureDocument::parse(structure_history[n - 1 - k]).json());
            QVERIFY(mesh.json() == MeshDocument::parse(mesh_history[n - 1 - k]).json());
            QVERIFY(process.json() == ProcessFlowDocument::parse(process_history[n - 1 - k]).json());
        }
        QVERIFY(!stack.can_undo());
        QVERIFY(structure.json() == StructureDocument::parse(structure_history[0]).json());

        // redo all: after redoing k commands, state must match history[k].
        for (std::size_t k = 1; k < n; ++k) {
            stack.redo();
            QVERIFY(structure.json() == StructureDocument::parse(structure_history[k]).json());
            QVERIFY(mesh.json() == MeshDocument::parse(mesh_history[k]).json());
            QVERIFY(process.json() == ProcessFlowDocument::parse(process_history[k]).json());
        }
        QVERIFY(!stack.can_redo());
        QVERIFY(structure.json() == StructureDocument::parse(structure_history[n - 1]).json());
        QVERIFY(mesh.json() == MeshDocument::parse(mesh_history[n - 1]).json());
        QVERIFY(process.json() == ProcessFlowDocument::parse(process_history[n - 1]).json());
    }
};

QTEST_MAIN(TestUndo)
#include "test_undo.moc"

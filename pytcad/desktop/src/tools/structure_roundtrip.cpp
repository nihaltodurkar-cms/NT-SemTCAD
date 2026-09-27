// tcad_structure_roundtrip: exercise StructureDocument/MeshDocument/
// ProcessFlowDocument's typed mutators with one FIXED, deterministic
// script, for the contract gate in gui/tests/test_desktop_contracts.py:
// the same script applied to real StructureModel/MeshModel/ProcessFlow
// objects in Python must produce an equal result (compared as parsed
// JSON, never raw text -- new array entries are not required to match
// Python's asdict() key order byte-for-byte).
//
//   tcad_structure_roundtrip <structure_in.json> <structure_out.json>
//                             <mesh_in.json> <mesh_out.json>
//                             <process_in.json> <process_out.json>
//
// The script deliberately never calls duplicate_step (see
// process_document.hpp: it mints a random id, so no exact-output gate
// can compare it) and includes one no-op call per document (an unknown
// id passed to remove/move), matching StructureModel's/ProcessFlow's
// own documented no-op contract for those cases.
#include "document/mesh_document.hpp"
#include "document/process_document.hpp"
#include "document/structure_document.hpp"

#include <exception>
#include <iostream>

using tcad::desktop::BoundaryData;
using tcad::desktop::ContactData;
using tcad::desktop::GateData;
using tcad::desktop::MeshDocument;
using tcad::desktop::ProcessFlowDocument;
using tcad::desktop::ProcessStepData;
using tcad::desktop::RegionData;
using tcad::desktop::StructureDocument;

namespace {

void run_structure_script(StructureDocument& doc) {
    doc.set_width_cm(doc.width_cm() * 2.0);

    RegionData r;
    r.id = "r_new";
    r.name = "New Region";
    r.x_min = 1e-4;
    r.x_max = 2e-4;
    r.y_min = 0.0;
    r.y_max = 5e-5;
    r.net_doping_cm3 = 1e17;
    doc.add_region(r);
    doc.move_region("r_new", -1);

    RegionData edited = doc.region(0);  // "r_new" moved to the front above
    edited.net_doping_cm3 = -5e16;
    doc.set_region("r_new", edited);

    ContactData c;
    c.id = "c_new";
    c.name = "New Contact";
    c.boundary.edge = "right";
    doc.add_contact(c);
    ContactData edited_c = doc.contact(0);
    edited_c.V = 1.5;
    doc.set_contact("c_new", edited_c);

    GateData g;
    g.id = "g_new";
    g.name = "New Gate";
    g.boundary.edge = "top";
    g.tox_cm = 2e-7;
    doc.add_gate(g);
    GateData edited_g = doc.gate(0);
    edited_g.V = 2.5;
    edited_g.vfb_mode = "manual";
    edited_g.vfb_manual = -0.7;
    doc.set_gate("g_new", edited_g);

    doc.remove_region("does-not-exist");  // documented no-op
}

void run_mesh_script(MeshDocument& doc) {
    doc.set_nx(doc.nx() + 4);
    doc.set_h_min(1e-7);
    doc.set_ratio(1.2);
}

void run_process_script(ProcessFlowDocument& doc) {
    ProcessStepData s;
    s.id = "p_new";
    s.name = "Anneal";
    s.operation = "anneal";
    s.enabled = true;
    s.parameters = {{"temperature_C", 1000}, {"time_s", 600}};
    doc.add_step(s);
    doc.move_step("p_new", -1);
    doc.set_step_enabled("p_new", false);
    doc.set_step_parameters("p_new", {{"temperature_C", 900}, {"time_s", 300}});
    doc.remove_step("does-not-exist");  // documented no-op
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        std::cerr << "usage: tcad_structure_roundtrip <structure_in.json> <structure_out.json> "
                     "<mesh_in.json> <mesh_out.json> <process_in.json> <process_out.json>\n";
        return 1;
    }
    try {
        auto structure = StructureDocument::load(argv[1]);
        run_structure_script(structure);
        structure.save(argv[2]);

        auto mesh = MeshDocument::load(argv[3]);
        run_mesh_script(mesh);
        mesh.save(argv[4]);

        auto process = ProcessFlowDocument::load(argv[5]);
        run_process_script(process);
        process.save(argv[6]);

        nlohmann::ordered_json view;
        view["region_count"] = structure.region_count();
        view["contact_count"] = structure.contact_count();
        view["gate_count"] = structure.gate_count();
        view["mesh_nx"] = mesh.nx();
        view["step_count"] = process.step_count();
        std::cout << view.dump() << "\n";
    } catch (const std::exception& e) {
        std::cout << nlohmann::json{{"error", e.what()}}.dump() << "\n";
        return 2;
    }
    return 0;
}

#include "structure_document.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tcad::desktop {
namespace {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// null <-> std::optional<double>, matching Python's own None <-> JSON null.
std::optional<double> opt_num(const nlohmann::ordered_json& j) {
    if (j.is_null()) return std::nullopt;
    return j.get<double>();
}

nlohmann::ordered_json opt_json(std::optional<double> v) {
    if (!v) return nullptr;
    return *v;
}

nlohmann::ordered_json boundary_to_json(const BoundaryData& b) {
    nlohmann::ordered_json j;
    j["edge"] = b.edge;
    j["range_lo"] = opt_json(b.range_lo);
    j["range_hi"] = opt_json(b.range_hi);
    return j;
}

BoundaryData boundary_from_json(const nlohmann::ordered_json& j) {
    BoundaryData b;
    b.edge = j.at("edge").get<std::string>();
    b.range_lo = opt_num(j.at("range_lo"));
    b.range_hi = opt_num(j.at("range_hi"));
    return b;
}

nlohmann::ordered_json region_to_json(const RegionData& r) {
    // Key order matches RegionSpec's own field declaration order (the
    // order Python's asdict() produces) -- not required for correctness
    // (round-trip is compared as parsed JSON/dicts, never raw text, per
    // gui/tests/test_desktop_contracts.py's own convention), kept only
    // for a human reading a dumped document.
    nlohmann::ordered_json j;
    j["id"] = r.id;
    j["name"] = r.name;
    j["x_min"] = r.x_min;
    j["x_max"] = r.x_max;
    j["y_min"] = r.y_min;
    j["y_max"] = r.y_max;
    j["net_doping_cm3"] = r.net_doping_cm3;
    j["z_min"] = opt_json(r.z_min);
    j["z_max"] = opt_json(r.z_max);
    j["material"] = r.material;
    j["doping_profile"] = r.doping_profile;
    j["profile_peak_cm3"] = opt_json(r.profile_peak_cm3);
    j["profile_sigma_y"] = opt_json(r.profile_sigma_y);
    j["profile_sigma_lat"] = opt_json(r.profile_sigma_lat);
    j["profile_edge_x"] = opt_json(r.profile_edge_x);
    j["profile_high_side"] = r.profile_high_side;
    return j;
}

RegionData region_from_json(const nlohmann::ordered_json& j) {
    RegionData r;
    r.id = j.at("id").get<std::string>();
    r.name = j.at("name").get<std::string>();
    r.x_min = j.at("x_min").get<double>();
    r.x_max = j.at("x_max").get<double>();
    r.y_min = j.at("y_min").get<double>();
    r.y_max = j.at("y_max").get<double>();
    r.net_doping_cm3 = j.at("net_doping_cm3").get<double>();
    r.z_min = opt_num(j.at("z_min"));
    r.z_max = opt_num(j.at("z_max"));
    r.material = j.at("material").get<std::string>();
    r.doping_profile = j.at("doping_profile").get<std::string>();
    r.profile_peak_cm3 = opt_num(j.at("profile_peak_cm3"));
    r.profile_sigma_y = opt_num(j.at("profile_sigma_y"));
    r.profile_sigma_lat = opt_num(j.at("profile_sigma_lat"));
    r.profile_edge_x = opt_num(j.at("profile_edge_x"));
    r.profile_high_side = j.at("profile_high_side").get<std::string>();
    return r;
}

nlohmann::ordered_json contact_to_json(const ContactData& c) {
    nlohmann::ordered_json j;
    j["id"] = c.id;
    j["name"] = c.name;
    j["boundary"] = boundary_to_json(c.boundary);
    j["V"] = c.V;
    return j;
}

ContactData contact_from_json(const nlohmann::ordered_json& j) {
    ContactData c;
    c.id = j.at("id").get<std::string>();
    c.name = j.at("name").get<std::string>();
    c.boundary = boundary_from_json(j.at("boundary"));
    c.V = j.at("V").get<double>();
    return c;
}

nlohmann::ordered_json gate_to_json(const GateData& g) {
    nlohmann::ordered_json j;
    j["id"] = g.id;
    j["name"] = g.name;
    j["boundary"] = boundary_to_json(g.boundary);
    j["tox_cm"] = g.tox_cm;
    j["gate_type"] = g.gate_type;
    j["vfb_mode"] = g.vfb_mode;
    j["vfb_manual"] = opt_json(g.vfb_manual);
    j["V"] = g.V;
    return j;
}

GateData gate_from_json(const nlohmann::ordered_json& j) {
    GateData g;
    g.id = j.at("id").get<std::string>();
    g.name = j.at("name").get<std::string>();
    g.boundary = boundary_from_json(j.at("boundary"));
    g.tox_cm = j.at("tox_cm").get<double>();
    g.gate_type = j.at("gate_type").get<std::string>();
    g.vfb_mode = j.at("vfb_mode").get<std::string>();
    g.vfb_manual = opt_num(j.at("vfb_manual"));
    g.V = j.at("V").get<double>();
    return g;
}

// Shared by region/contact/gate id lookups: StructureModel's own
// find_region/find_contact/find_gate return None (never raise) for an
// unknown id, and remove_*/move_region are documented no-ops in that
// case -- mirrored here rather than throwing.
std::optional<std::size_t> find_by_id(const nlohmann::ordered_json& arr, const std::string& id) {
    for (std::size_t i = 0; i < arr.size(); ++i)
        if (arr[i].at("id").get<std::string>() == id) return i;
    return std::nullopt;
}

}  // namespace

StructureDocument StructureDocument::parse(const std::string& text) {
    StructureDocument d;
    d.doc_ = Json::parse(text);
    if (!d.doc_.is_object() || !d.doc_.contains("width_cm") || !d.doc_.contains("height_cm"))
        throw std::runtime_error("not a structure document: needs top-level 'width_cm' and 'height_cm'");
    if (!d.doc_.contains("regions")) d.doc_["regions"] = Json::array();
    if (!d.doc_.contains("contacts")) d.doc_["contacts"] = Json::array();
    if (!d.doc_.contains("gates")) d.doc_["gates"] = Json::array();
    return d;
}

StructureDocument StructureDocument::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(utf8(path) + ": cannot open");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

std::string StructureDocument::dump(int indent) const { return doc_.dump(indent); }

void StructureDocument::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error(utf8(path) + ": cannot write");
    out << doc_.dump();
}

double StructureDocument::width_cm() const { return doc_.at("width_cm").get<double>(); }
double StructureDocument::height_cm() const { return doc_.at("height_cm").get<double>(); }
std::string StructureDocument::material() const {
    return doc_.value("material", std::string("Silicon"));
}
std::optional<double> StructureDocument::depth_cm() const {
    return doc_.contains("depth_cm") ? opt_num(doc_.at("depth_cm")) : std::nullopt;
}
void StructureDocument::set_width_cm(double v) { doc_["width_cm"] = v; }
void StructureDocument::set_height_cm(double v) { doc_["height_cm"] = v; }
void StructureDocument::set_depth_cm(std::optional<double> v) { doc_["depth_cm"] = opt_json(v); }

std::size_t StructureDocument::region_count() const { return doc_.at("regions").size(); }
RegionData StructureDocument::region(std::size_t index) const {
    return region_from_json(doc_.at("regions").at(index));
}
void StructureDocument::add_region(const RegionData& r) { doc_["regions"].push_back(region_to_json(r)); }
bool StructureDocument::remove_region(const std::string& id) {
    auto& arr = doc_["regions"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr.erase(arr.begin() + static_cast<long>(*idx));
    return true;
}
bool StructureDocument::move_region(const std::string& id, int offset) {
    auto& arr = doc_["regions"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    const long n = static_cast<long>(arr.size());
    long new_idx = static_cast<long>(*idx) + offset;
    new_idx = std::max<long>(0, std::min<long>(n - 1, new_idx));
    if (new_idx == static_cast<long>(*idx)) return true;
    Json item = arr[*idx];
    arr.erase(arr.begin() + static_cast<long>(*idx));
    arr.insert(arr.begin() + new_idx, item);
    return true;
}

bool StructureDocument::set_region(const std::string& id, const RegionData& r) {
    auto& arr = doc_["regions"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr[*idx] = region_to_json(r);
    return true;
}

std::size_t StructureDocument::contact_count() const { return doc_.at("contacts").size(); }
ContactData StructureDocument::contact(std::size_t index) const {
    return contact_from_json(doc_.at("contacts").at(index));
}
void StructureDocument::add_contact(const ContactData& c) { doc_["contacts"].push_back(contact_to_json(c)); }
bool StructureDocument::remove_contact(const std::string& id) {
    auto& arr = doc_["contacts"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr.erase(arr.begin() + static_cast<long>(*idx));
    return true;
}

bool StructureDocument::set_contact(const std::string& id, const ContactData& c) {
    auto& arr = doc_["contacts"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr[*idx] = contact_to_json(c);
    return true;
}

std::size_t StructureDocument::gate_count() const { return doc_.at("gates").size(); }
GateData StructureDocument::gate(std::size_t index) const {
    return gate_from_json(doc_.at("gates").at(index));
}
void StructureDocument::add_gate(const GateData& g) { doc_["gates"].push_back(gate_to_json(g)); }
bool StructureDocument::remove_gate(const std::string& id) {
    auto& arr = doc_["gates"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr.erase(arr.begin() + static_cast<long>(*idx));
    return true;
}
bool StructureDocument::set_gate(const std::string& id, const GateData& g) {
    auto& arr = doc_["gates"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr[*idx] = gate_to_json(g);
    return true;
}

}  // namespace tcad::desktop

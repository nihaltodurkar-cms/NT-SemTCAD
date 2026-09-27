#include "mesh_document.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tcad::desktop {
namespace {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::optional<double> opt_num(const nlohmann::ordered_json& j) {
    if (j.is_null()) return std::nullopt;
    return j.get<double>();
}

}  // namespace

MeshDocument MeshDocument::parse(const std::string& text) {
    MeshDocument d;
    d.doc_ = Json::parse(text);
    if (!d.doc_.is_object())
        throw std::runtime_error("not a mesh document: expected a JSON object");
    // MeshModel's own dataclass defaults (structure_model.py), applied
    // only when the key is absent -- a real Python-written file always
    // carries every key (asdict()), so this only matters for a
    // hand-built minimal fixture.
    if (!d.doc_.contains("nx")) d.doc_["nx"] = 40;
    if (!d.doc_.contains("ny")) d.doc_["ny"] = 24;
    if (!d.doc_.contains("grading")) d.doc_["grading"] = "uniform";
    if (!d.doc_.contains("ratio")) d.doc_["ratio"] = 1.15;
    if (!d.doc_.contains("h_min")) d.doc_["h_min"] = nullptr;
    if (!d.doc_.contains("h_max")) d.doc_["h_max"] = nullptr;
    if (!d.doc_.contains("nz")) d.doc_["nz"] = nullptr;
    return d;
}

MeshDocument MeshDocument::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(utf8(path) + ": cannot open");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

std::string MeshDocument::dump(int indent) const { return doc_.dump(indent); }

void MeshDocument::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error(utf8(path) + ": cannot write");
    out << doc_.dump();
}

int MeshDocument::nx() const { return doc_.at("nx").get<int>(); }
int MeshDocument::ny() const { return doc_.at("ny").get<int>(); }
std::string MeshDocument::grading() const { return doc_.at("grading").get<std::string>(); }
std::optional<double> MeshDocument::h_min() const { return opt_num(doc_.at("h_min")); }
std::optional<double> MeshDocument::h_max() const { return opt_num(doc_.at("h_max")); }
double MeshDocument::ratio() const { return doc_.at("ratio").get<double>(); }
std::optional<int> MeshDocument::nz() const {
    const auto& j = doc_.at("nz");
    if (j.is_null()) return std::nullopt;
    return j.get<int>();
}

void MeshDocument::set_nx(int v) { doc_["nx"] = v; }
void MeshDocument::set_ny(int v) { doc_["ny"] = v; }
void MeshDocument::set_grading(const std::string& v) { doc_["grading"] = v; }
void MeshDocument::set_h_min(std::optional<double> v) { doc_["h_min"] = v ? Json(*v) : nullptr; }
void MeshDocument::set_h_max(std::optional<double> v) { doc_["h_max"] = v ? Json(*v) : nullptr; }
void MeshDocument::set_ratio(double v) { doc_["ratio"] = v; }
void MeshDocument::set_nz(std::optional<int> v) { doc_["nz"] = v ? Json(*v) : nullptr; }

}  // namespace tcad::desktop

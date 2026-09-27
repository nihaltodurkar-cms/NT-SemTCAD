#include "process_document.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace tcad::desktop {
namespace {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

nlohmann::ordered_json step_to_json(const ProcessStepData& s) {
    nlohmann::ordered_json j;
    j["id"] = s.id;
    j["name"] = s.name;
    j["operation"] = s.operation;
    j["enabled"] = s.enabled;
    j["parameters"] = s.parameters;
    return j;
}

ProcessStepData step_from_json(const nlohmann::ordered_json& j) {
    ProcessStepData s;
    s.id = j.at("id").get<std::string>();
    s.name = j.at("name").get<std::string>();
    s.operation = j.at("operation").get<std::string>();
    s.enabled = j.at("enabled").get<bool>();
    s.parameters = j.at("parameters");
    return s;
}

std::optional<std::size_t> find_by_id(const nlohmann::ordered_json& arr, const std::string& id) {
    for (std::size_t i = 0; i < arr.size(); ++i)
        if (arr[i].at("id").get<std::string>() == id) return i;
    return std::nullopt;
}

// This library (tcad_desktop_data) is Qt-free (CMakeLists.txt's own
// header comment), so `duplicate_step` cannot use QUuid the way
// RegionListWidget::addRegion() does -- same 8-lowercase-hex-character
// shape as `uuid.uuid4().hex[:8]`, generated with <random> instead.
std::string random_hex_id() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> nibble(0, 15);
    std::string id(8, '0');
    for (char& c : id) {
        const int v = nibble(rng);
        c = static_cast<char>(v < 10 ? '0' + v : 'a' + (v - 10));
    }
    return id;
}

}  // namespace

ProcessFlowDocument ProcessFlowDocument::parse(const std::string& text) {
    ProcessFlowDocument d;
    d.doc_ = Json::parse(text);
    if (!d.doc_.is_object() || !d.doc_.contains("steps") || !d.doc_.at("steps").is_array())
        throw std::runtime_error("not a process flow document: needs a top-level 'steps' array");
    return d;
}

ProcessFlowDocument ProcessFlowDocument::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(utf8(path) + ": cannot open");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

std::string ProcessFlowDocument::dump(int indent) const { return doc_.dump(indent); }

void ProcessFlowDocument::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error(utf8(path) + ": cannot write");
    out << doc_.dump();
}

std::size_t ProcessFlowDocument::step_count() const { return doc_.at("steps").size(); }

ProcessStepData ProcessFlowDocument::step(std::size_t index) const {
    return step_from_json(doc_.at("steps").at(index));
}

void ProcessFlowDocument::add_step(const ProcessStepData& s) { doc_["steps"].push_back(step_to_json(s)); }

bool ProcessFlowDocument::remove_step(const std::string& id) {
    auto& arr = doc_["steps"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr.erase(arr.begin() + static_cast<long>(*idx));
    return true;
}

bool ProcessFlowDocument::move_step(const std::string& id, int offset) {
    auto& arr = doc_["steps"];
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

bool ProcessFlowDocument::set_step_enabled(const std::string& id, bool enabled) {
    auto& arr = doc_["steps"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr[*idx]["enabled"] = enabled;
    return true;
}

bool ProcessFlowDocument::set_step_parameters(const std::string& id, const Json& parameters) {
    auto& arr = doc_["steps"];
    auto idx = find_by_id(arr, id);
    if (!idx) return false;
    arr[*idx]["parameters"] = parameters;
    return true;
}

std::string ProcessFlowDocument::duplicate_step(const std::string& id) {
    auto& arr = doc_["steps"];
    auto idx = find_by_id(arr, id);
    if (!idx) return "";
    ProcessStepData dup = step_from_json(arr[*idx]);
    dup.id = random_hex_id();
    dup.name += " (copy)";
    arr.insert(arr.begin() + static_cast<long>(*idx) + 1, step_to_json(dup));
    return dup.id;
}

}  // namespace tcad::desktop

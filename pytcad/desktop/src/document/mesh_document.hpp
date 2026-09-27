// gui/services/structure_model.py's MeshModel.to_dict() shape: nx, ny,
// grading, x_focus, y_focus, h_min, h_max, ratio, nz, z_focus. A project
// file sibling of the structure document (StructureDocument), not
// nested inside it -- matches project_store.save_project's top-level
// "mesh" key.
//
// Narrower lossless guarantee than StructureDocument's: MeshModel.
// from_dict() is `cls(**d)` (checked by reading the code), so Python
// itself already refuses an unrecognised top-level key here -- this
// document still preserves one unchanged on disk (a mutator only
// touches the field it targets), but nothing downstream can consume a
// document carrying one until MeshModel itself grows tolerant of it.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace tcad::desktop {

class MeshDocument {
public:
    using Json = nlohmann::ordered_json;

    static MeshDocument parse(const std::string& text);
    static MeshDocument load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    std::string dump(int indent = -1) const;
    const Json& json() const { return doc_; }

    int nx() const;
    int ny() const;
    std::string grading() const;
    std::optional<double> h_min() const;
    std::optional<double> h_max() const;
    double ratio() const;
    std::optional<int> nz() const;

    void set_nx(int v);
    void set_ny(int v);
    void set_grading(const std::string& v);
    void set_h_min(std::optional<double> v);
    void set_h_max(std::optional<double> v);
    void set_ratio(double v);
    void set_nz(std::optional<int> v);

private:
    Json doc_;
};

}  // namespace tcad::desktop

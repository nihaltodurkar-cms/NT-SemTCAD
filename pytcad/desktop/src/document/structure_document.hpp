// The GUI-authored structure document (gui/services/structure_model.py's
// StructureModel.to_dict() shape): width_cm, height_cm, material,
// regions[], contacts[], gates[], depth_cm. This is NOT the solver wire
// format (DeviceSpecDocument, which needs top-level "mesh"/"doping" and
// is produced only by StructureModel.to_device_spec()) -- it is the
// editable, authoring-level document P4's editors mutate (NATIVE-
// DESKTOP-PLAN.md section 20.1's correction: S1 was first drafted as
// extending DeviceSpecDocument, which cannot represent an editable
// region -- StructureModel's own dataclasses are the real shape).
//
// Lossless for any top-level key this build has no accessor for, same
// P0 rule DeviceSpecDocument follows: a mutator edits the underlying
// JSON in place rather than reconstructing the whole document, so an
// unrecognised key set by a future build survives untouched. This
// guarantee does NOT extend inside an individual region/contact/gate
// object -- StructureModel's own RegionSpec.from_dict()/ContactModel.
// from_dict()/GateModel.from_dict() are `cls(**d)`, so Python itself
// already refuses an extra key inside one of those (verified by
// reading the code, not assumed); add_region()/add_contact()/
// add_gate() therefore write exactly the known key set, matching what
// Python's own asdict() produces.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tcad::desktop {

struct BoundaryData {
    std::string edge;                    // "left" | "right" | "top" | "bottom" | "front" | "back"
    std::optional<double> range_lo;
    std::optional<double> range_hi;
};

struct RegionData {
    std::string id;
    std::string name;
    double x_min = 0.0, x_max = 0.0, y_min = 0.0, y_max = 0.0;
    double net_doping_cm3 = 0.0;
    std::optional<double> z_min, z_max;
    std::string material = "SILICON";
    std::string doping_profile = "uniform";   // "uniform" | "gaussian_erfc"
    std::optional<double> profile_peak_cm3, profile_sigma_y, profile_sigma_lat, profile_edge_x;
    std::string profile_high_side = "left";   // "left" | "right"
};

struct ContactData {
    std::string id;
    std::string name;
    BoundaryData boundary;
    double V = 0.0;
};

struct GateData {
    std::string id;
    std::string name;
    BoundaryData boundary;
    double tox_cm = 0.0;
    std::string gate_type = "n+poly";    // "n+poly" | "p+poly" | "Al" | a float work function, as a string
    std::string vfb_mode = "computed";   // "computed" | "manual"
    std::optional<double> vfb_manual;
    double V = 0.0;
};

class StructureDocument {
public:
    using Json = nlohmann::ordered_json;

    static StructureDocument parse(const std::string& text);
    static StructureDocument load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    std::string dump(int indent = -1) const;
    const Json& json() const { return doc_; }

    double width_cm() const;
    double height_cm() const;
    std::string material() const;
    std::optional<double> depth_cm() const;
    void set_width_cm(double v);
    void set_height_cm(double v);
    void set_depth_cm(std::optional<double> v);

    std::size_t region_count() const;
    RegionData region(std::size_t index) const;
    void add_region(const RegionData& r);
    bool remove_region(const std::string& id);      // false if id not found (no-op, Python's own contract)
    bool move_region(const std::string& id, int offset);  // clamped; false if id not found
    // Replaces the region matching `id` with `r` in place (S2's canvas
    // and S3's form editor both funnel a field edit through this rather
    // than remove+add, which would also disturb compositing order).
    // `r.id` need not equal `id` -- a rename is just another field edit
    // -- but `id` must already exist. False if it does not.
    bool set_region(const std::string& id, const RegionData& r);

    std::size_t contact_count() const;
    ContactData contact(std::size_t index) const;
    void add_contact(const ContactData& c);
    bool remove_contact(const std::string& id);
    bool set_contact(const std::string& id, const ContactData& c);  // see set_region's own comment

    std::size_t gate_count() const;
    GateData gate(std::size_t index) const;
    void add_gate(const GateData& g);
    bool remove_gate(const std::string& id);
    bool set_gate(const std::string& id, const GateData& g);  // see set_region's own comment

private:
    Json doc_;
};

}  // namespace tcad::desktop

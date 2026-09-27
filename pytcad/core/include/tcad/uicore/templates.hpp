// Device templates (NATIVE-DESKTOP-PLAN.md section 20.3 decision 3, P4
// S7): C++ is now the SOLE implementation of what
// `workbench/core/templates.py` used to hold directly -- the parameter
// registry (name/label/unit/default/bounds/integer) and the per-template
// build() logic that turns a values dict into an AUTHORED device's
// geometry (regions/contacts/mesh hint). `workbench/core/templates.py`
// is a thin compatibility layer that reconstructs the actual
// `DomainDevice`/`Region`/`ContactDef`/`Boundary` Python dataclasses
// from this module's output and calls their EXISTING, unmodified
// `.validate()` -- those dataclasses (and their validate() bodies) are
// NOT part of "Templates" in the P4 decision and stay pure Python,
// shared with every other device-authoring path in the workbench.
//
// This is UI/data-layer registry logic, not numerical solver code --
// see catalog.hpp's header comment for why it lives in `uicore/` and
// shares `_core` without carrying the numerical core's frozen-file
// obligations.
#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tcad::uicore {

struct TemplateParamInfo {
    std::string name;
    std::string label;
    std::string unit;
    double default_value = 0.0;
    bool has_lo = false;
    double lo = 0.0;
    bool has_hi = false;
    double hi = 0.0;
    bool integer = false;
};

struct BoundaryOut {
    std::string edge;
    bool has_range_lo = false;
    double range_lo = 0.0;
    bool has_range_hi = false;
    double range_hi = 0.0;
};

struct RegionOut {
    std::string id;
    std::string name;
    double x_min = 0.0, x_max = 0.0, y_min = 0.0, y_max = 0.0;
    double doping_cm3 = 0.0;
    std::string material;  // "" = leave Region's own default ("SILICON")
};

struct ContactOut {
    std::string id;
    std::string name;
    std::string kind;  // "ohmic" | "gate"
    double V = 0.0;
    bool has_boundary = false;
    BoundaryOut boundary;
    bool has_tox_cm = false;
    double tox_cm = 0.0;
    std::string gate_type;  // "" = leave ContactDef's own default ("n+poly")
    std::string vfb_mode;   // "" = leave ContactDef's own default ("computed")
    bool has_vfb_manual = false;
    double vfb_manual = 0.0;
};

// The kwargs `workbench/core/templates.py` passes straight into
// `DomainDevice(**kwargs)` (after turning `regions`/`contacts` into real
// dataclass instances) -- never any field DomainDevice does not already
// default sensibly, so a template that (like most of them) never touches
// `material` leaves it unset here rather than restating "SILICON".
struct DeviceOut {
    std::string id;
    std::string name;
    bool has_material = false;
    std::string material;
    int dimensionality = 2;
    double width_cm = 0.0, height_cm = 0.0;
    int mesh_nx = 0, mesh_ny = 0;
    std::vector<RegionOut> regions;
    std::vector<ContactOut> contacts;
};

struct TemplateInfo {
    std::string id;
    std::string title;
    std::string description;
    std::vector<TemplateParamInfo> params;
};

// Thrown for an unknown template id or an invalid parameter set -- the
// thin Python wrapper re-raises as KeyError/ValueError with the SAME
// message text (or, for InvalidConfig, the same substrings) the pinned
// tests already check (test_device_templates.py, test_m11s5_templates.py).
struct TemplateError : std::runtime_error {
    enum class Kind { UnknownTemplate, InvalidConfig } kind;
    TemplateError(Kind k, const std::string& what) : std::runtime_error(what), kind(k) {}
};

class TemplateCatalog {
public:
    // Sorted template ids.
    static std::vector<std::string> list();
    // Throws TemplateError(UnknownTemplate) if `id` is not registered.
    static const TemplateInfo& describe(const std::string& id);
    // `values` holds only the parameters the CALLER explicitly supplied
    // (already type-checked as real, non-bool, finite numbers by the
    // bindings layer -- see templates_bindings.cpp for why that check
    // needs the raw Python object and can't happen here). Merges with
    // defaults, validates integer/range constraints, and dispatches to
    // the template's own geometry builder. Throws TemplateError:
    //   UnknownTemplate  -- `id` not registered
    //   InvalidConfig    -- unknown parameter name, non-whole value for
    //                       an integer param, or a value outside
    //                       [lo, hi]
    static DeviceOut build(const std::string& id,
                            const std::vector<std::pair<std::string, double>>& values);
};

}  // namespace tcad::uicore

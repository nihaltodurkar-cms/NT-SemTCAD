// Bindings for device Templates (NATIVE-DESKTOP-PLAN.md section 20.3
// decision 3, P4 S7): C++ owns the registry and build() logic; this
// file is the ONLY place that touches a raw Python `values` object,
// translating its shape into `TemplateCatalog::build`'s typed input and
// translating failures into the exact builtin exception types the
// existing, unmodified pinned tests already check
// (test_device_templates.py / test_m11s5_templates.py).
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <cmath>
#include <sstream>

#include "tcad/uicore/templates.hpp"

namespace nb = nanobind;
using tcad::uicore::BoundaryOut;
using tcad::uicore::ContactOut;
using tcad::uicore::DeviceOut;
using tcad::uicore::RegionOut;
using tcad::uicore::TemplateCatalog;
using tcad::uicore::TemplateError;
using tcad::uicore::TemplateInfo;
using tcad::uicore::TemplateParamInfo;

namespace {

nb::dict param_dict(const TemplateParamInfo& p) {
    nb::dict d;
    d["name"] = p.name;
    d["label"] = p.label;
    d["unit"] = p.unit;
    d["default"] = p.default_value;
    d["lo"] = p.has_lo ? nb::cast(p.lo) : nb::none();
    d["hi"] = p.has_hi ? nb::cast(p.hi) : nb::none();
    d["integer"] = p.integer;
    return d;
}

nb::dict describe_dict(const std::string& id) {
    try {
        const TemplateInfo& info = TemplateCatalog::describe(id);
        nb::dict d;
        d["id"] = info.id;
        d["title"] = info.title;
        d["description"] = info.description;
        nb::list params;
        for (const auto& p : info.params) params.append(param_dict(p));
        d["params"] = params;
        return d;
    } catch (const TemplateError& e) {
        throw nb::key_error(e.what());
    }
}

nb::dict boundary_dict(const BoundaryOut& b) {
    nb::dict d;
    d["edge"] = b.edge;
    d["range_lo"] = b.has_range_lo ? nb::cast(b.range_lo) : nb::none();
    d["range_hi"] = b.has_range_hi ? nb::cast(b.range_hi) : nb::none();
    return d;
}

nb::dict region_dict(const RegionOut& r) {
    nb::dict d;
    d["id"] = r.id;
    d["name"] = r.name;
    d["x_min"] = r.x_min;
    d["x_max"] = r.x_max;
    d["y_min"] = r.y_min;
    d["y_max"] = r.y_max;
    d["doping_cm3"] = r.doping_cm3;
    if (!r.material.empty()) d["material"] = r.material;
    return d;
}

nb::dict contact_dict(const ContactOut& c) {
    nb::dict d;
    d["id"] = c.id;
    d["name"] = c.name;
    d["kind"] = c.kind;
    d["V"] = c.V;
    if (c.has_boundary) d["boundary"] = boundary_dict(c.boundary);
    if (c.has_tox_cm) d["tox_cm"] = c.tox_cm;
    if (!c.gate_type.empty()) d["gate_type"] = c.gate_type;
    if (!c.vfb_mode.empty()) d["vfb_mode"] = c.vfb_mode;
    if (c.has_vfb_manual) d["vfb_manual"] = c.vfb_manual;
    return d;
}

nb::dict device_dict(const DeviceOut& dev) {
    nb::dict d;
    d["id"] = dev.id;
    d["name"] = dev.name;
    if (dev.has_material) d["material"] = dev.material;
    d["dimensionality"] = dev.dimensionality;
    d["width_cm"] = dev.width_cm;
    d["height_cm"] = dev.height_cm;
    d["mesh_nx"] = dev.mesh_nx;
    d["mesh_ny"] = dev.mesh_ny;
    nb::list regions, contacts;
    for (const auto& r : dev.regions) regions.append(region_dict(r));
    for (const auto& c : dev.contacts) contacts.append(contact_dict(c));
    d["regions"] = regions;
    d["contacts"] = contacts;
    return d;
}

// Extracts one supplied parameter's value as a real, non-bool, finite
// number -- exactly DeviceTemplate.build()'s own type check, which
// needs the raw Python object (a bool is a bool subclass of int; a JSON
// number surviving the QML/RPC boundary might arrive as either) and so
// cannot be pushed down into TemplateCatalog, which only ever sees
// already-validated doubles.
double numeric_value_or_raise(const std::string& param_name, nb::handle value) {
    bool ok = !nb::isinstance<nb::bool_>(value) &&
              (nb::isinstance<nb::int_>(value) || nb::isinstance<nb::float_>(value));
    double d = 0.0;
    if (ok) {
        d = nb::cast<double>(value);
        ok = std::isfinite(d);
    }
    if (!ok) {
        const std::string value_repr = nb::cast<std::string>(nb::repr(value));
        throw nb::value_error(("parameter '" + param_name + "' must be a finite number, got " +
                               value_repr)
                                  .c_str());
    }
    return d;
}

nb::dict build_dict(const std::string& id, nb::object values) {
    // describe() first: an unknown template id is reported the same way
    // regardless of what `values` looks like, matching get_template()
    // being the first thing every Python call site touches.
    const TemplateInfo& info = [&]() -> const TemplateInfo& {
        try {
            return TemplateCatalog::describe(id);
        } catch (const TemplateError& e) {
            throw nb::key_error(e.what());
        }
    }();

    nb::dict d = values.is_none() ? nb::dict() : nb::borrow<nb::dict>(values);
    if (!values.is_none() && !nb::isinstance<nb::dict>(values)) {
        const std::string type_name = nb::cast<std::string>(values.type().attr("__name__"));
        throw nb::value_error(
            ("template parameter values must be a dict, got " + type_name).c_str());
    }

    std::vector<std::string> known;
    known.reserve(info.params.size());
    for (const auto& p : info.params) known.push_back(p.name);

    std::vector<std::pair<std::string, double>> supplied;
    for (auto item : d) {
        const std::string name = nb::cast<std::string>(item.first);
        if (std::find(known.begin(), known.end(), name) == known.end()) {
            // Let TemplateCatalog::build raise the "unknown parameter"
            // ValueError with its own known/unknown listing -- passing
            // a placeholder value through is safe since it's rejected
            // for its NAME, never evaluated numerically.
            supplied.emplace_back(name, 0.0);
            continue;
        }
        supplied.emplace_back(name, numeric_value_or_raise(name, item.second));
    }

    try {
        return device_dict(TemplateCatalog::build(id, supplied));
    } catch (const TemplateError& e) {
        if (e.kind == TemplateError::Kind::UnknownTemplate) throw nb::key_error(e.what());
        throw nb::value_error(e.what());
    }
}

nb::list list_ids() {
    nb::list out;
    for (const auto& id : TemplateCatalog::list()) out.append(id);
    return out;
}

}  // namespace

void register_templates(nb::module_& m) {
    nb::class_<TemplateCatalog>(m, "TemplateCatalog")
        .def_static("list", &list_ids)
        .def_static("describe", &describe_dict)
        .def_static("build", &build_dict, nb::arg("id"), nb::arg("values") = nb::none());
}

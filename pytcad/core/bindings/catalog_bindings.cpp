// Bindings for the Model Catalog (NATIVE-DESKTOP-PLAN.md section 20.3
// decision 3, P4 S6): C++ owns the registry; this file is the ONLY
// place that touches a raw Python object, translating `validate`'s
// input shape and `describe`'s failure into the exact builtin exception
// types and message substrings the existing, unmodified pinned tests
// already check (test_workbench_m1.py's own `match=` strings, most
// directly) -- never a new message invented here.
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "tcad/uicore/catalog.hpp"

namespace nb = nanobind;
using tcad::uicore::ModelCatalog;
using tcad::uicore::ModelCatalogError;
using tcad::uicore::ModelInfo;

namespace {

nb::dict default_config_dict() {
    nb::dict d;
    for (const auto& [key, enabled] : ModelCatalog::default_config()) d[key.c_str()] = enabled;
    return d;
}

const ModelInfo& describe_or_raise(const std::string& key) {
    try {
        return ModelCatalog::describe(key);
    } catch (const ModelCatalogError& e) {
        throw nb::key_error(e.what());
    }
}

void validate_object(nb::object config) {
    if (!nb::isinstance<nb::dict>(config)) {
        const std::string type_name = nb::cast<std::string>(config.type().attr("__name__"));
        throw nb::value_error(("model config must be a dict of {model_key: bool}, got " +
                               type_name)
                                  .c_str());
    }
    nb::dict d = nb::cast<nb::dict>(config);
    std::vector<std::pair<std::string, bool>> parsed;
    parsed.reserve(d.size());
    for (auto item : d) {
        const std::string key = nb::cast<std::string>(item.first);
        if (!nb::isinstance<nb::bool_>(item.second)) {
            const std::string value_repr = nb::cast<std::string>(nb::repr(item.second));
            throw nb::value_error(("model '" + key + "' must be true or false, got " +
                                   value_repr)
                                      .c_str());
        }
        parsed.emplace_back(key, nb::cast<bool>(item.second));
    }
    try {
        ModelCatalog::validate(parsed);
    } catch (const ModelCatalogError& e) {
        throw nb::value_error(e.what());
    }
}

}  // namespace

void register_catalog(nb::module_& m) {
    nb::class_<ModelInfo>(m, "ModelInfo")
        .def_ro("key", &ModelInfo::key)
        .def_ro("title", &ModelInfo::title)
        .def_ro("equations", &ModelInfo::equations)
        .def_ro("parameters", &ModelInfo::parameters)
        .def_ro("references", &ModelInfo::references)
        .def_ro("applicability", &ModelInfo::applicability)
        .def_ro("enabled_by_default", &ModelInfo::enabled_by_default)
        .def_ro("limitations", &ModelInfo::limitations);

    nb::class_<ModelCatalog>(m, "ModelCatalog")
        .def_static("list", &ModelCatalog::list)
        .def_static("describe", &describe_or_raise, nb::rv_policy::copy)
        .def_static("default_config", &default_config_dict)
        .def_static("validate", &validate_object);
}

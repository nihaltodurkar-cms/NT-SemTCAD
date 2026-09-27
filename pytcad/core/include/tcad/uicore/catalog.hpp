// The Model Catalog (NATIVE-DESKTOP-PLAN.md section 20.3 decision 3,
// P4 S6): C++ is now the SOLE implementation of what
// `workbench/core/catalog.py` used to hold directly -- registry
// metadata (equations, parameters, references, applicability,
// limitations) for every physics model flag, plus wire-format default
// config and config validation. `workbench/core/catalog.py` is a thin
// compatibility layer over this, reached through the EXISTING
// `pytcad._core` nanobind module (one more `register_*` alongside
// mesh/solver/process/... in bindings/module.cpp, not a second module)
// -- not a second implementation kept for comparison (see the plan's
// own "no permanent oracle" language).
//
// This is UI/data-layer registry logic, not numerical solver code, so
// it carries none of the frozen-core sign-off/FD-Jacobian obligations
// root CLAUDE.md's Hard Rules place on `pytcad/*.py`. It shares `_core`'s
// compiled module only because that project's nanobind/MinGW toolchain
// wiring already exists and works, avoiding a second module/import path
// for no functional reason; the SOURCE is kept in its own `uicore/`
// subdirectory, physically separate from every numerical kernel here.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace tcad::uicore {

struct ModelInfo {
    std::string key;
    std::string title;
    std::vector<std::string> equations;
    std::vector<std::string> parameters;
    std::vector<std::string> references;
    std::string applicability;
    bool enabled_by_default = false;
    std::string limitations;  // "" = none stated
};

// Thrown for an unknown model key or an invalid config -- the thin
// Python wrapper catches this and re-raises as ValueError/KeyError
// with the SAME message text the pinned tests already check
// (test_m34_s5_catalog.py's `match="needs it"`, for one), never a new
// message invented here.
struct ModelCatalogError : std::runtime_error {
    enum class Kind { UnknownModel, InvalidConfig } kind;
    ModelCatalogError(Kind k, const std::string& what) : std::runtime_error(what), kind(k) {}
};

class ModelCatalog {
public:
    // Sorted model keys.
    static std::vector<std::string> list();
    // Throws ModelCatalogError(UnknownModel) if `key` is not registered.
    static const ModelInfo& describe(const std::string& key);
    // {key: enabled_by_default}, in registration order -- EXACTLY
    // DeviceSpec._default_models()'s wire-format default (checked by
    // the existing pinned tests, e.g. test_m34_s5_catalog.py).
    static std::vector<std::pair<std::string, bool>> default_config();
    // Throws ModelCatalogError(InvalidConfig) on: an unknown key, a
    // non-bool value (the Python wrapper's job to detect, since a JSON/
    // Python bool has no separate C++ representation to check here --
    // see catalog_bindings.cpp), or impact_nonlocal=true without
    // impact=true (M34-S2's own dependency rule).
    static void validate(const std::vector<std::pair<std::string, bool>>& config);
};

}  // namespace tcad::uicore

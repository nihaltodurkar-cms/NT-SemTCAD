#include "tcad/uicore/catalog.hpp"

#include <algorithm>

namespace tcad::uicore {

#include "catalog_data.inc"

std::vector<std::string> ModelCatalog::list() {
    std::vector<std::string> keys;
    keys.reserve(all_models().size());
    for (const auto& m : all_models()) keys.push_back(m.key);
    std::sort(keys.begin(), keys.end());
    return keys;
}

const ModelInfo& ModelCatalog::describe(const std::string& key) {
    for (const auto& m : all_models())
        if (m.key == key) return m;
    std::string known;
    for (const auto& k : list()) {
        if (!known.empty()) known += ", ";
        known += k;
    }
    throw ModelCatalogError(
        ModelCatalogError::Kind::UnknownModel,
        "unknown physics model '" + key + "' (known models: " + known + ")");
}

std::vector<std::pair<std::string, bool>> ModelCatalog::default_config() {
    std::vector<std::pair<std::string, bool>> out;
    out.reserve(all_models().size());
    for (const auto& m : all_models()) out.emplace_back(m.key, m.enabled_by_default);
    return out;
}

void ModelCatalog::validate(const std::vector<std::pair<std::string, bool>>& config) {
    const auto known_keys = list();
    std::vector<std::string> unknown;
    bool has_impact = false, has_impact_nonlocal = false;
    for (const auto& [key, value] : config) {
        if (std::find(known_keys.begin(), known_keys.end(), key) == known_keys.end())
            unknown.push_back(key);
        if (key == "impact") has_impact = value;
        if (key == "impact_nonlocal") has_impact_nonlocal = value;
    }
    if (!unknown.empty()) {
        std::sort(unknown.begin(), unknown.end());
        std::string joined;
        for (const auto& k : unknown) {
            if (!joined.empty()) joined += ", ";
            joined += "'" + k + "'";
        }
        std::string known;
        for (const auto& k : known_keys) {
            if (!known.empty()) known += ", ";
            known += k;
        }
        throw ModelCatalogError(
            ModelCatalogError::Kind::InvalidConfig,
            "unknown physics model(s) [" + joined + "]; known models: " + known);
    }
    // M34-S2: the effective field only modifies the local impact model's
    // coefficients -- on its own it would do nothing (catalog.py's own comment).
    if (has_impact_nonlocal && !has_impact) {
        throw ModelCatalogError(
            ModelCatalogError::Kind::InvalidConfig,
            "model 'impact_nonlocal' modifies 'impact' and needs it enabled too");
    }
}

}  // namespace tcad::uicore

#include "physics_lab_data.hpp"

namespace tcad::desktop {

std::vector<ConvergenceStageView> convergence_data(const ResultModel& model) {
    const auto record = model.record();
    if (!record) return {};
    const auto trace = model.trace();
    if (!trace || trace->empty()) return {};
    std::vector<ConvergenceStageView> out;
    out.reserve(trace->size());
    for (const auto& step : *trace) {
        ConvergenceStageView view;
        view.stage = step.stage;
        view.iterations = step.iterations;
        for (const auto& channel : step.metrics) view.metrics[channel.name] = channel.values;
        out.push_back(std::move(view));
    }
    return out;
}

std::vector<ContinuationRow> continuation_data(const ResultModel& model) {
    const auto record = model.record();
    if (!record) return {};
    const auto records = model.continuation_records();
    if (!records || !records->is_array()) return {};
    std::vector<ContinuationRow> out;
    out.reserve(records->size());
    int index = 0;
    for (const auto& rec : *records) {
        ContinuationRow row;
        row.index = index++;
        // rec.get("parameter", rec.get("V", "")) -- Python's own fallback chain.
        if (rec.contains("parameter") && !rec.at("parameter").is_null())
            row.parameter = rec.at("parameter").is_string() ? rec.at("parameter").get<std::string>()
                                                             : rec.at("parameter").dump();
        else if (rec.contains("V") && !rec.at("V").is_null())
            row.parameter = rec.at("V").is_string() ? rec.at("V").get<std::string>() : rec.at("V").dump();
        if (rec.contains("nodes") && !rec.at("nodes").is_null())
            row.nodes = rec.at("nodes").is_string() ? rec.at("nodes").get<std::string>() : rec.at("nodes").dump();
        // bool(rec.get("accepted", True)): absent -> True; present -> Python
        // truthiness (a real driver always writes a bool here, so only that
        // case and "explicitly null/false" are worth being exact about).
        if (!rec.contains("accepted"))
            row.accepted = true;
        else if (rec.at("accepted").is_boolean())
            row.accepted = rec.at("accepted").get<bool>();
        else
            row.accepted = !rec.at("accepted").is_null();
        out.push_back(std::move(row));
    }
    return out;
}

}  // namespace tcad::desktop

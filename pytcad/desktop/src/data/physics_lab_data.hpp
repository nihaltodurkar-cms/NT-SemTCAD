// The Physics Lab's convergence and continuation views (NATIVE-DESKTOP-
// PLAN.md P4 S6b): the native equivalents of `gui/controllers/
// lab_controller.py`'s `PhysicsLabController.convergenceData()`/
// `continuationData()`, read directly from `ResultModel` -- no new
// backend RPC, no new data: `ResultModel::trace()` (P2-S1) already
// parses everything `convergenceData()` needs, and `ResultModel::
// record()` (P1 S3d) already carries `continuation_records` in the raw
// JSON.
//
// `provenanceRows()`'s basic fields (backend/created_utc/material/T/
// models-on) are deliberately NOT duplicated here: `InfoPanel` (P1 S3d)
// already shows all of them, in its own format, in the "Run" group --
// building a second, separate display of the same data would be
// inventing UI the plan's own transition rule (no duplicated surfaces)
// argues against, not a gap. `InfoPanel`'s per-model summary is a
// condensed "Models on: a, b, c" line rather than `provenanceRows()`'s
// one-row-per-model form; that is a display-format difference over
// already-shown data, not new data.
//
// `convergenceData()`'s `residuals` field (its own docstring: "kept for
// backward compatibility") is not reproduced -- it is the first entry
// of the same `metrics` map this returns in full, kept there only for a
// pre-M52 QML consumer this native app has no equivalent of.
#pragma once

#include "data/result_model.hpp"

#include <map>
#include <string>
#include <vector>

namespace tcad::desktop {

struct ConvergenceStageView {
    std::string stage;
    std::vector<double> iterations;
    std::map<std::string, std::vector<double>> metrics;  // name -> per-iteration values, NaN for null
};

struct ContinuationRow {
    int index = 0;
    std::string parameter;  // record's "parameter", falling back to "V", falling back to ""
    std::string nodes;      // record's "nodes" as text ("" when absent or non-string)
    bool accepted = true;
};

// Empty when the result has no record (pre-v2 file) or no trace at all
// -- mirrors `convergenceData()` returning None in exactly those two
// cases (`record is None or not record.trace`); callers distinguish
// "no data" from "empty stage list" the same way (both empty here).
std::vector<ConvergenceStageView> convergence_data(const ResultModel& model);

// Empty when the result has no record or no continuation_records --
// mirrors `continuationData()` returning `[]` in both cases.
std::vector<ContinuationRow> continuation_data(const ResultModel& model);

}  // namespace tcad::desktop

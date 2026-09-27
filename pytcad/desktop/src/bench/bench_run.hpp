// The Run pipeline's own overhead (NATIVE-DESKTOP-PLAN.md 17.3, P3-S9):
//
//   tcad_desktop --bench-run <job.json> [--repeats N]
//
// Measures, through the real JobRunner and a real solve:
//   - Run-click-to-first-progress: wall time from start() to the first
//     REAL Newton-iteration PYTCAD_PROGRESS record -- not the "stage"
//     record every job emits first regardless (progress_channel.py's
//     own protocol), which would make every job look identical -- what
//     a user actually waits looking at a blank Telemetry dock before
//     the residual plot gets its first point;
//   - native total: start() to finished(), through JobRunner's full
//     line-assembly/progress-parsing machinery;
//   - a bare subprocess running the SAME entry point (python -u -m
//     gui.services.solver_runner <job> <out>), timed with a plain
//     QProcess::waitForFinished -- no incremental stdout parsing at
//     all -- so "native total" minus this isolates what JobRunner's
//     own bookkeeping costs on top of the subprocess Python already
//     needs, rather than conflating the two.
#pragma once

#include <nlohmann/json.hpp>

#include <QString>

namespace tcad::desktop {

struct RunBenchOptions {
    QString job_path;
    int repeats = 5;
};

nlohmann::ordered_json run_run_benchmark(const RunBenchOptions& options);

}  // namespace tcad::desktop

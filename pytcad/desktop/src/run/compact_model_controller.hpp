// Compact-model extraction as a local job (NATIVE-DESKTOP-PLAN.md
// section 25): a thin QObject wrapper around one JobRunner pointed at
// `python -m gui.services.compact_runner`, the M38 Phase 4 subprocess
// entry point already used by the (removed) QML app's CompactModelPanel.
// Its result is a JSON manifest, not an npz -- a fitted parameter set is
// not sweep/mesh data, so it goes through no ResultStore/ResultModel; this
// controller reads the file itself and hands the parsed JSON straight up.
#pragma once

#include "run/job_runner.hpp"

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

namespace tcad::desktop {

class CompactModelController : public QObject {
    Q_OBJECT

public:
    CompactModelController(RunnerConfig config, QObject* parent = nullptr);

    bool busy() const { return runner_->isRunning(); }
    // `job` is compact_runner.py's own job dict ({"kind": "diode"|"mosfet1", ...}).
    // False (and nothing started) when a run is already going.
    bool extract(const nlohmann::json& job);
    void stop();

signals:
    void started();
    void finished(const nlohmann::json& manifest);
    void failed(const QString& summary, const QString& details);

private:
    void onFinished(const QString& run_id, const QString& result_path);

    JobRunner* runner_;
};

}  // namespace tcad::desktop

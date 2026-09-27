// The local Study (NATIVE-DESKTOP-PLAN.md 17.16, P3-S7). Qt Core only.
//
// A Study is a split matrix: one device per combination of parameter
// levels (workbench.splits.run_split_matrix, through the backend's
// study.rows -- decision 1, the backend builds the jobs), each solved as
// an ordinary equilibrium job (decision 1 of 17.16: no bias/sweep in S7,
// as every QML study row solves today) in a pool of JobRunners.
//
// A row that the template rejects is "build_error" and never dispatched
// -- run_split_matrix's own per-row isolation, restated through this
// controller's status vocabulary (build_error | pending | running | done
// | failed), same as QML's StudyController. A failed row does not stop
// the others. Cancel all returns running rows to pending (their job is
// killed, not counted as failed); Run then re-runs pending and failed
// rows. Independent of the main run and the family/comparison batch
// (decision 2 of 17.16): its own pool, its own directory.
//
// Each study writes into its own runs/study-<stamp>/ directory (decision
// 4 wants the main run's keep-20 retention to never touch a study's
// rows, since they never match "result-*.npz" loose in the runs root);
// the 5 most recent study-* directories are kept, and the one holding
// the currently open result is never removed.
#pragma once

#include "backend/backend_client.hpp"
#include "run/job_runner.hpp"
#include "run/remote_host.hpp"

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace tcad::desktop {

class StudyController : public QObject {
    Q_OBJECT

public:
    enum class RowStatus { BuildError, Pending, Running, Done, Failed };
    Q_ENUM(RowStatus)

    struct Row {
        nlohmann::json params;    // {name: value}, this row's split point
        RowStatus status = RowStatus::Pending;
        QString error;            // build_error's reason, or a failed row's summary
        QByteArray job_text;      // empty for build_error
        QString result_path;      // set once "done"
    };

    // `backend` as BatchController's; `runner` is python/working_dir/
    // strip_from_path only -- work_dir is set per study, below.
    // `runsRoot` is MainWindow::runsDir(): each study's own directory is
    // created under it. `sshCmd`/`scpCmd` (default the real binaries)
    // are overridable purely so a test can point a remote pool at a
    // local stand-in (gui/tests/fixtures/fake_ssh.py/fake_scp.py)
    // instead -- the same seam RemoteJobRunner itself takes.
    StudyController(std::function<BackendClient*()> backend, RunnerConfig runner, QString runsRoot,
                    QObject* parent = nullptr, QStringList sshCmd = {QStringLiteral("ssh")},
                    QStringList scpCmd = {QStringLiteral("scp")});
    ~StudyController() override;

    // study.templates, once per window (cheap, unlikely to change mid-session).
    void requestTemplates();
    const nlohmann::json& templates() const { return templates_; }
    bool templatesReady() const { return templates_ready_; }

    // Build every row of the split matrix (study.rows) -- no solving yet.
    // A previous study's rows and pool are dropped; a busy study refuses
    // (false) rather than tearing down a running pool. `error` on refusal.
    bool buildRows(const QString& templateId, const nlohmann::json& base, const nlohmann::json& splits,
                   QString* error = nullptr);

    // Dispatch every "pending"/"failed" row across a pool of `poolSize`
    // JobRunners (clamped to [1, 6], and to the number of such rows).
    // False when there is nothing to run or a study is already running.
    bool run(int poolSize);
    void cancelAll();

    // P3-S8: remote hosts, a Study-only feature (decision 8, §17.7).
    // `hosts`: plain host-name strings, same "type what you mean" style
    // as QML's; a hostile or malformed one (18.1/18.2) is refused and
    // reported per-host through failed(), the rest kept. Empty ->
    // back to a local pool. Each host gets RemoteHostConfig's defaults
    // (current SSH user via ssh's own config, port 22, "python" on
    // PATH, /tmp/pytcad-remote); per-host overrides are not exposed.
    void setRemoteHosts(const QStringList& hosts);
    QStringList remoteHosts() const;
    // The lower-level entry point setRemoteHosts builds on: full
    // per-host configs (a per-host python/remote_workdir/port/identity
    // override, none of which the QML-mirroring string field exposes)
    // -- tests use this to make ONE of several hosts behave badly
    // while sharing the same ssh/scp stand-in as the others. Each
    // host is still validated; a hostile one is refused and reported,
    // same as setRemoteHosts.
    void setRemoteHostConfigs(std::vector<RemoteHostConfig> hosts);

    const std::vector<Row>& rows() const { return rows_; }
    const nlohmann::json& axes() const { return axes_; }
    bool busy() const { return busy_; }
    int poolSize() const { return pool_size_; }
    QString studyDir() const { return study_dir_; }
    QString templateId() const { return template_id_; }
    // The process ids of every currently-running row (tests: verifying a
    // window close kills them).
    std::vector<qint64> livePids() const;

    // The 5 most recent "study-*" directories under `runsRoot` are kept
    // (plus `keepDir`, if not empty and under `runsRoot`); everything else
    // (files and subdirectories alike) is removed. Returns how many
    // top-level entries were removed.
    static int pruneStudies(const QString& runsRoot, const QString& keepDir, int max_studies = 5);

signals:
    void templatesChanged();
    void rowsChanged();
    void busyChanged(bool busy);
    void message(const QString& text);
    void failed(const QString& title, const QString& detail);

private:
    void dispatchNext(RunnerBase* runner);
    void onRowFinished(RunnerBase* runner, int idx, const QString& resultPath);
    void onRowFailed(RunnerBase* runner, int idx, const QString& summary, const QString& details);
    void onRowCanceled(RunnerBase* runner, int idx);
    void finishIfDone();

    std::function<BackendClient*()> backend_;
    RunnerConfig runner_template_;
    QString runs_root_;
    QStringList ssh_cmd_;
    QStringList scp_cmd_;

    nlohmann::json templates_;
    bool templates_ready_ = false;

    QString template_id_;
    nlohmann::json axes_ = nlohmann::json::object();
    std::vector<Row> rows_;
    QString study_dir_;

    std::vector<RemoteHostConfig> remote_hosts_;
    std::vector<std::unique_ptr<RunnerBase>> pool_;
    std::vector<int> active_row_;   // pool_[i] -> row index, or -1
    std::vector<int> pending_;      // row indices still queued (front = next)
    int pool_size_ = 0;
    bool busy_ = false;
    bool canceled_ = false;
    int generation_ = 0;  // bumped by buildRows/cancelAll: stale replies/signals are ignored
};

}  // namespace tcad::desktop

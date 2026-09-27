// The native remote runner (NATIVE-DESKTOP-PLAN.md 18.3, P3-S8). Qt
// Core only. The C++ counterpart of gui/services/remote_job_runner.py:
// runs a job on a remote host over SSH, chaining four QProcess stages
// (mkdir -> push -> pull) through each one's own `finished` signal, so
// nothing ever blocks the Qt event loop on network I/O -- Stop is
// always a safe OS-level kill of whichever stage is currently live.
//
// Same `RunnerBase` surface as `JobRunner` (start/cancel/isRunning/
// currentPid, and the finished/failed/canceled signals), so
// StudyController's pool can hold either kind uniformly (18.3's
// RunnerBase decision) -- it has none of JobRunner's extra
// started/line/stage/progress signals, since a remote row's stdout is
// never parsed live (matching remote_job_runner.py, which has no such
// signals either).
//
// Host validation (a leading '-' in host/user) and command quoting
// (remote_workdir/python, and the job/result paths) are `remote_host.
// hpp`'s job, called from here at the one place they matter --
// `start()` refuses before any QProcess exists, and `argvForStage`
// quotes every token the "mkdir"/"run" commands hand to the remote
// shell (18.1/18.2).
#pragma once

#include "run/remote_host.hpp"
#include "run/runner_base.hpp"

#include <QProcess>
#include <QString>
#include <QStringList>

namespace tcad::desktop {

class RemoteJobRunner : public RunnerBase {
    Q_OBJECT

public:
    // `sshCmd`/`scpCmd` are overridable (default `{"ssh"}`/`{"scp"}`)
    // purely so a test can point them at a local stand-in -- see
    // gui/tests/fixtures/fake_ssh.py/fake_scp.py, the SAME fixtures
    // the Python RemoteJobRunner's own tests use. `localWorkingDir`
    // (default empty: unset) is each stage's OWN QProcess working
    // directory -- fake_ssh.py's "remote" command runs through a
    // real local Python subprocess with no cwd override of its own,
    // so it needs to inherit one that can `-m gui.services.
    // solver_runner` (pytcad's root); a real ssh binary ignores it.
    RemoteJobRunner(RemoteHostConfig host, QString workDir, QObject* parent = nullptr,
                    QStringList sshCmd = {QStringLiteral("ssh")}, QStringList scpCmd = {QStringLiteral("scp")},
                    QString localWorkingDir = QString());
    ~RemoteJobRunner() override;

    QString start(const JobRequest& request, QString* error = nullptr) override;
    void cancel() override;
    bool isRunning() const override { return proc_ != nullptr; }
    qint64 currentPid() const override;

    QString currentResultPath() const { return result_path_; }
    QString currentRunId() const { return run_id_; }
    // Which of kMkdir/kPush/kRun/kPull is live right now (tests: timing
    // cancel() to land during a specific stage).
    int currentStage() const { return stage_; }

    // The argv a stage would run, WITHOUT starting it -- tests inspect
    // the host-injection and quoting gates directly (18.4).
    QStringList argvForStage(int stage) const;
    static constexpr int kMkdir = 0, kPush = 1, kRun = 2, kPull = 3;

signals:
    // Not part of RunnerBase (JobRunner's own `started` is likewise
    // additional to it): no current caller needs it, kept for
    // parity with remote_job_runner.py's own `started` signal.
    void started(const QString& run_id);

private:
    void runStage(int stage);
    void onStageFinished(int exit_code, QProcess::ExitStatus status);
    void onStderr();
    void cleanupJobFile();
    QStringList sshOpts() const;
    QStringList scpOpts() const;

    RemoteHostConfig host_;
    QString work_dir_;
    QStringList ssh_cmd_;
    QStringList scp_cmd_;
    QString local_working_dir_;
    QProcess* proc_ = nullptr;
    int stage_ = kMkdir;
    bool canceling_ = false;
    QString stderr_tail_;
    QString run_id_;
    QString job_path_;
    QString result_path_;
    QString remote_job_;
    QString remote_out_;
    JobRequest request_;
};

}  // namespace tcad::desktop

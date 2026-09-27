#include "remote_job_runner.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

namespace tcad::desktop {
namespace {
constexpr int kStageCount = 4;
}  // namespace

RemoteJobRunner::RemoteJobRunner(RemoteHostConfig host, QString workDir, QObject* parent, QStringList sshCmd,
                                 QStringList scpCmd, QString localWorkingDir)
    : RunnerBase(parent), host_(std::move(host)), work_dir_(std::move(workDir)), ssh_cmd_(std::move(sshCmd)),
      scp_cmd_(std::move(scpCmd)), local_working_dir_(std::move(localWorkingDir)) {}

RemoteJobRunner::~RemoteJobRunner() {
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->deleteLater();
        proc_ = nullptr;
    }
}

qint64 RemoteJobRunner::currentPid() const { return proc_ ? proc_->processId() : 0; }

QStringList RemoteJobRunner::sshOpts() const {
    QStringList argv = {QStringLiteral("-p"), QString::number(host_.port), QStringLiteral("-o"),
                       QStringLiteral("BatchMode=yes"), QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10")};
    if (!host_.identity_file.isEmpty()) argv += {QStringLiteral("-i"), host_.identity_file};
    return argv;
}

QStringList RemoteJobRunner::scpOpts() const {
    QStringList argv = {QStringLiteral("-P"), QString::number(host_.port), QStringLiteral("-o"),
                       QStringLiteral("BatchMode=yes"), QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10")};
    if (!host_.identity_file.isEmpty()) argv += {QStringLiteral("-i"), host_.identity_file};
    return argv;
}

// `--` ends option parsing before the target on every stage (defense
// in depth: start() already refuses a hostile host/user before any of
// this runs). The "mkdir"/"run" commands are ONE shell-quoted string
// per remote shell convention (ssh(1): trailing arguments are
// concatenated and handed to the remote shell) -- an unquoted
// `remote_workdir`/`python` containing a space or a shell
// metacharacter previously broke the command, or ran a second one, on
// the remote host (18.1 finding 2).
QStringList RemoteJobRunner::argvForStage(int stage) const {
    const QString target = remoteHostTarget(host_);
    if (stage == kMkdir) {
        const QString cmd = QStringLiteral("mkdir -p ") + posixQuote(host_.remote_workdir);
        return ssh_cmd_ + sshOpts() + QStringList{QStringLiteral("--"), target, cmd};
    }
    if (stage == kPush) {
        return scp_cmd_ + scpOpts() +
               QStringList{QStringLiteral("--"), job_path_, target + QLatin1Char(':') + remote_job_};
    }
    if (stage == kRun) {
        QStringList tokens = {host_.python};
        tokens += request_.entry;
        tokens += {remote_job_, remote_out_};
        QStringList quoted;
        quoted.reserve(tokens.size());
        for (const QString& t : tokens) quoted << posixQuote(t);
        return ssh_cmd_ + sshOpts() + QStringList{QStringLiteral("--"), target, quoted.join(QLatin1Char(' '))};
    }
    // kPull
    return scp_cmd_ + scpOpts() +
           QStringList{QStringLiteral("--"), target + QLatin1Char(':') + remote_out_, result_path_};
}

QString RemoteJobRunner::start(const JobRequest& request, QString* error) {
    auto refuse = [error](const QString& why) {
        if (error) *error = why;
        return QString();
    };
    if (proc_) return refuse(QStringLiteral("a run is already going"));
    const QString host_error = validateRemoteHost(host_);
    if (!host_error.isEmpty()) return refuse(host_error);
    if (work_dir_.isEmpty() || !QDir().mkpath(work_dir_))
        return refuse(QStringLiteral("cannot create the local work directory: ") + work_dir_);

    request_ = request;
    run_id_ = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QDir work(work_dir_);
    job_path_ = work.absoluteFilePath(QStringLiteral("job-%1.json").arg(run_id_));
    result_path_ = work.absoluteFilePath(QStringLiteral("result-%1%2").arg(run_id_, request.result_suffix));
    remote_job_ = host_.remote_workdir + QStringLiteral("/job-") + run_id_ + QStringLiteral(".json");
    remote_out_ = host_.remote_workdir + QStringLiteral("/result-") + run_id_ + request.result_suffix;

    QSaveFile f(job_path_);
    if (!f.open(QIODevice::WriteOnly) || f.write(request.job_text) != request.job_text.size() || !f.commit())
        return refuse(QStringLiteral("cannot write the job file ") + job_path_ + ": " + f.errorString());

    canceling_ = false;
    stage_ = kMkdir;
    runStage(kMkdir);
    emit started(run_id_);
    return run_id_;
}

void RemoteJobRunner::runStage(int stage) {
    stage_ = stage;
    stderr_tail_.clear();
    const QStringList argv = argvForStage(stage);
    proc_ = new QProcess(this);
    if (!local_working_dir_.isEmpty()) proc_->setWorkingDirectory(local_working_dir_);
    connect(proc_, &QProcess::readyReadStandardError, this, &RemoteJobRunner::onStderr);
    connect(proc_, &QProcess::finished, this, &RemoteJobRunner::onStageFinished);
    proc_->start(argv.value(0), argv.mid(1));
}

void RemoteJobRunner::onStderr() {
    if (proc_) stderr_tail_ += QString::fromUtf8(proc_->readAllStandardError());
}

void RemoteJobRunner::cleanupJobFile() {
    if (!job_path_.isEmpty()) QFile::remove(job_path_);
}

void RemoteJobRunner::onStageFinished(int exit_code, QProcess::ExitStatus) {
    QProcess* finished_proc = proc_;
    proc_ = nullptr;
    if (finished_proc) finished_proc->deleteLater();

    if (canceling_) {
        cleanupJobFile();
        emit canceled(run_id_);
        return;
    }
    if (exit_code != 0) {
        cleanupJobFile();
        static const char* kStageNames[kStageCount] = {"mkdir", "push", "run", "pull"};
        emit failed(run_id_,
                   QStringLiteral("Remote %1 failed on %2 (exit %3)")
                       .arg(kStageNames[stage_], host_.host)
                       .arg(exit_code),
                   stderr_tail_.isEmpty() ? QStringLiteral("no output") : stderr_tail_);
        return;
    }
    if (stage_ + 1 < kStageCount) {
        runStage(stage_ + 1);
        return;
    }
    // "pull" just finished.
    cleanupJobFile();
    if (QFileInfo::exists(result_path_)) {
        emit finished(run_id_, result_path_);
    } else {
        emit failed(run_id_, QStringLiteral("Remote result missing after pull from %1").arg(host_.host),
                   stderr_tail_.isEmpty() ? QStringLiteral("no output") : stderr_tail_);
    }
}

void RemoteJobRunner::cancel() {
    // Kill at once, as JobRunner does (decision 3, §17.5): QProcess::
    // terminate() cannot stop a plain console Python process on Windows
    // (measured here too -- a terminate()-then-3s-grace-kill pattern,
    // mirroring remote_job_runner.py's own unfixed dead time, left the
    // "run" stage alive for the full 3 s every time). kill() is
    // immediate; the local ssh/scp/python stage process this "remote"
    // fake talks to is always this machine's own, so there is no
    // Windows-job-object process TREE to worry about the way a local
    // solver's MPI/pool children can be (JobRunner's own reason for
    // TerminateJobObject) -- a single kill() is enough here.
    if (!proc_) return;
    canceling_ = true;
    proc_->kill();
}

}  // namespace tcad::desktop

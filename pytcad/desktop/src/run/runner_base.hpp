// A local-or-remote job runner (NATIVE-DESKTOP-PLAN.md 18.3, P3-S8).
// Qt Core only.
//
// `JobRunner` (local, one QProcess) and `RemoteJobRunner` (SSH-backed,
// four chained QProcess stages) share this interface so
// StudyController's pool -- and only StudyController's pool; the main
// Run/family/batch controllers are local-only by decision 2 (§17.5)
// -- can hold either kind uniformly, dispatching and reacting to
// `finished`/`failed`/`canceled` without knowing which one a given
// slot is. `JobRequest` lives here (not in job_runner.hpp) so this
// header has no dependency on JobRunner's own, heavier header.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tcad::desktop {

struct JobRequest {
    QStringList entry;               // e.g. moduleEntry(...); the job and result paths are appended
    QByteArray job_text;             // written verbatim to the job file
    QString result_suffix = ".npz";
};

class RunnerBase : public QObject {
    Q_OBJECT

public:
    explicit RunnerBase(QObject* parent = nullptr) : QObject(parent) {}
    ~RunnerBase() override = default;

    // Start a run: its id, or an empty string with the reason in *error.
    virtual QString start(const JobRequest& request, QString* error = nullptr) = 0;
    // Cancel the current run: canceled() follows when it has actually
    // ended (a process kill is not instantaneous). A new run may be
    // started straight away.
    virtual void cancel() = 0;
    virtual bool isRunning() const = 0;
    // The current run's local process id (0 if none is live locally --
    // a remote runner between stages, or between "cancel" and the
    // stage process actually exiting, may report 0 even while busy()).
    virtual qint64 currentPid() const = 0;

signals:
    void finished(const QString& run_id, const QString& result_path);
    void failed(const QString& run_id, const QString& summary, const QString& details);
    void canceled(const QString& run_id);
};

}  // namespace tcad::desktop

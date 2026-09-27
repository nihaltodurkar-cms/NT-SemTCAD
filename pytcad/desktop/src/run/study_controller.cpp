#include "study_controller.hpp"

#include "run/remote_job_runner.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <thread>

namespace tcad::desktop {
namespace {
constexpr int kBackendTimeoutMs = 60000;
constexpr const char* kSolver = "gui.services.solver_runner";

StudyController::RowStatus statusFromString(const std::string& s) {
    if (s == "build_error") return StudyController::RowStatus::BuildError;
    if (s == "running") return StudyController::RowStatus::Running;
    if (s == "done") return StudyController::RowStatus::Done;
    if (s == "failed") return StudyController::RowStatus::Failed;
    return StudyController::RowStatus::Pending;
}
}  // namespace

StudyController::StudyController(std::function<BackendClient*()> backend, RunnerConfig runner, QString runsRoot,
                                 QObject* parent, QStringList sshCmd, QStringList scpCmd)
    : QObject(parent), backend_(std::move(backend)), runner_template_(std::move(runner)),
      runs_root_(std::move(runsRoot)), ssh_cmd_(std::move(sshCmd)), scp_cmd_(std::move(scpCmd)) {}

StudyController::~StudyController() {
    pool_.clear();  // each JobRunner kills its own process tree silently
}

void StudyController::requestTemplates() {
    BackendReply* reply = backend_()->call("study.templates", nlohmann::json::object(), kBackendTimeoutMs);
    connect(reply, &BackendReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!reply->ok()) {
            emit failed(tr("Could not list study templates"), reply->errorMessage());
            return;
        }
        templates_ = reply->result();
        templates_ready_ = true;
        emit templatesChanged();
    });
}

bool StudyController::buildRows(const QString& templateId, const nlohmann::json& base, const nlohmann::json& splits,
                                QString* error) {
    if (busy_) {
        if (error) *error = tr("A study is already running; cancel it first.");
        return false;
    }
    const int generation = ++generation_;
    nlohmann::json params = {{"template_id", templateId.toStdString()},
                             {"base", base.is_null() ? nlohmann::json::object() : base},
                             {"splits", splits.is_null() ? nlohmann::json::object() : splits}};
    BackendReply* reply = backend_()->call("study.rows", std::move(params), kBackendTimeoutMs);
    connect(reply, &BackendReply::finished, this, [this, reply, generation, templateId] {
        reply->deleteLater();
        if (generation != generation_) return;  // superseded by a later build/cancel
        if (!reply->ok()) {
            const auto& d = reply->errorData();
            const bool refused = d.is_object() && d.value("type", std::string()) == "RunConfigError";
            emit failed(refused ? QString::fromStdString(d.value("title", std::string()))
                                : tr("Could not configure the study"),
                        refused ? QString::fromStdString(d.value("detail", std::string())) : reply->errorMessage());
            return;
        }
        const nlohmann::json& r = reply->result();
        template_id_ = templateId;
        axes_ = r.value("axes", nlohmann::json::object());
        rows_.clear();
        pending_.clear();
        for (const auto& j : r.value("rows", nlohmann::json::array())) {
            Row row;
            row.params = j.value("params", nlohmann::json::object());
            row.status = statusFromString(j.value("status", std::string("pending")));
            row.error = QString::fromStdString(j.value("error", std::string()));
            if (j.contains("job_text") && j["job_text"].is_string())
                row.job_text = QByteArray::fromStdString(j["job_text"].get<std::string>());
            rows_.push_back(std::move(row));
        }
        // A fresh study: a new directory, and the previous 5 kept (a study
        // never in progress here, since buildRows refuses while busy_).
        StudyController::pruneStudies(runs_root_, {});
        study_dir_ = QDir(runs_root_).absoluteFilePath(
            QString("study-%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz")));
        QDir().mkpath(study_dir_);
        emit message(tr("Study: %1 row(s) built (%2 rejected).")
                         .arg(rows_.size())
                         .arg(std::count_if(rows_.begin(), rows_.end(),
                                            [](const Row& r) { return r.status == RowStatus::BuildError; })));
        emit rowsChanged();
    });
    return true;
}

void StudyController::setRemoteHosts(const QStringList& hosts) {
    std::vector<RemoteHostConfig> good;
    for (const QString& raw : hosts) {
        const QString name = raw.trimmed();
        if (name.isEmpty()) continue;
        RemoteHostConfig h;
        h.host = name;
        const QString err = validateRemoteHost(h);
        if (!err.isEmpty()) {
            emit failed(tr("Invalid remote host"), err);
            continue;
        }
        good.push_back(h);
    }
    remote_hosts_ = std::move(good);
}

QStringList StudyController::remoteHosts() const {
    QStringList out;
    for (const auto& h : remote_hosts_) out << h.host;
    return out;
}

void StudyController::setRemoteHostConfigs(std::vector<RemoteHostConfig> hosts) {
    std::vector<RemoteHostConfig> good;
    for (auto& h : hosts) {
        const QString err = validateRemoteHost(h);
        if (!err.isEmpty()) {
            emit failed(tr("Invalid remote host"), err);
            continue;
        }
        good.push_back(std::move(h));
    }
    remote_hosts_ = std::move(good);
}

bool StudyController::run(int poolSize) {
    if (busy_) return false;
    pending_.clear();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        if (rows_[i].status == RowStatus::Pending || rows_[i].status == RowStatus::Failed) {
            rows_[i].status = RowStatus::Pending;
            rows_[i].error.clear();
            pending_.push_back(i);
        }
    }
    if (pending_.empty()) return false;
    canceled_ = false;
    ++generation_;
    // The same cap workbench.batch.default_worker_count reasons about
    // (never more workers than jobs, never more than the CPU count, never
    // more than 6): a caller that leaves the pool size at ITS OWN default
    // (>= both caps) gets exactly default_worker_count(n) here too.
    const int cpu = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    pool_size_ = std::max(1, std::min({poolSize, 6, cpu, static_cast<int>(pending_.size())}));

    pool_.clear();
    active_row_.assign(pool_size_, -1);
    RunnerConfig rc = runner_template_;
    rc.work_dir = study_dir_;
    for (int i = 0; i < pool_size_; ++i) {
        RunnerBase* runner;
        if (!remote_hosts_.empty()) {
            const RemoteHostConfig& h = remote_hosts_[static_cast<std::size_t>(i) % remote_hosts_.size()];
            runner = new RemoteJobRunner(h, study_dir_, this, ssh_cmd_, scp_cmd_, rc.working_dir);
        } else {
            runner = new JobRunner(rc, this);
        }
        connect(runner, &RunnerBase::finished, this, [this, runner, i](const QString&, const QString& path) {
            onRowFinished(runner, i, path);
        });
        connect(runner, &RunnerBase::failed, this,
                [this, runner, i](const QString&, const QString& summary, const QString& details) {
                    onRowFailed(runner, i, summary, details);
                });
        connect(runner, &RunnerBase::canceled, this,
                [this, runner, i](const QString&) { onRowCanceled(runner, i); });
        pool_.emplace_back(runner);
    }
    busy_ = true;
    emit busyChanged(true);
    emit rowsChanged();
    for (auto& r : pool_) dispatchNext(r.get());
    return true;
}

void StudyController::dispatchNext(RunnerBase* runner) {
    const int slot = static_cast<int>(
        std::find_if(pool_.begin(), pool_.end(), [runner](const auto& p) { return p.get() == runner; }) -
        pool_.begin());
    if (canceled_ || pending_.empty()) {
        active_row_[slot] = -1;
        finishIfDone();
        return;
    }
    const int idx = pending_.front();
    pending_.erase(pending_.begin());
    active_row_[slot] = idx;
    Row& row = rows_[idx];
    row.status = RowStatus::Running;
    QString err;
    if (runner->start({JobRunner::moduleEntry(kSolver), row.job_text}, &err).isEmpty()) {
        row.status = RowStatus::Failed;
        row.error = err;
        active_row_[slot] = -1;
        emit rowsChanged();
        dispatchNext(runner);
        return;
    }
    emit rowsChanged();
}

void StudyController::onRowFinished(RunnerBase* runner, int slot, const QString& resultPath) {
    if (slot >= static_cast<int>(active_row_.size())) return;
    const int idx = active_row_[slot];
    if (idx >= 0) {
        rows_[idx].status = RowStatus::Done;
        rows_[idx].result_path = resultPath;
    }
    active_row_[slot] = -1;
    emit rowsChanged();
    if (!canceled_) dispatchNext(runner);
    else finishIfDone();
}

void StudyController::onRowFailed(RunnerBase* runner, int slot, const QString& summary, const QString& details) {
    if (slot >= static_cast<int>(active_row_.size())) return;
    const int idx = active_row_[slot];
    if (idx >= 0) {
        rows_[idx].status = RowStatus::Failed;
        rows_[idx].error = details.isEmpty() ? summary : details;
    }
    active_row_[slot] = -1;
    emit rowsChanged();
    if (!canceled_) dispatchNext(runner);
    else finishIfDone();
}

void StudyController::onRowCanceled(RunnerBase*, int slot) {
    // Interrupted by cancelAll(), not a solver failure: back to pending,
    // as QML's StudyController does.
    if (slot < static_cast<int>(active_row_.size())) {
        const int idx = active_row_[slot];
        if (idx >= 0) rows_[idx].status = RowStatus::Pending;
        active_row_[slot] = -1;
    }
    emit rowsChanged();
    finishIfDone();
}

void StudyController::finishIfDone() {
    const bool any_active = std::any_of(active_row_.begin(), active_row_.end(), [](int i) { return i >= 0; });
    if (any_active || !busy_) return;
    busy_ = false;
    emit busyChanged(false);
    if (canceled_) emit message(tr("Study canceled: running rows returned to pending."));
}

void StudyController::cancelAll() {
    if (!busy_) return;
    canceled_ = true;
    pending_.clear();
    for (auto& r : pool_)
        if (r->isRunning()) r->cancel();
    emit rowsChanged();
    // finishIfDone() runs from each canceled() signal as the runners exit;
    // a pool with nothing currently running finishes immediately.
    finishIfDone();
}

std::vector<qint64> StudyController::livePids() const {
    std::vector<qint64> out;
    for (const auto& r : pool_)
        if (r->isRunning() && r->currentPid() > 0) out.push_back(r->currentPid());
    return out;
}

int StudyController::pruneStudies(const QString& runsRoot, const QString& keepDir, int max_studies) {
    const QDir root(runsRoot);
    if (!root.exists()) return 0;
    const QString keep_norm =
        keepDir.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(keepDir).absoluteFilePath()).toLower();
    QFileInfoList dirs = root.entryInfoList({"study-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(dirs.begin(), dirs.end(),
             [](const QFileInfo& a, const QFileInfo& b) { return a.fileName() > b.fileName(); });  // newest-stamp first
    int removed = 0;
    int kept = 0;
    for (const QFileInfo& d : dirs) {
        const QString norm = QDir::cleanPath(d.absoluteFilePath()).toLower();
        if (!keep_norm.isEmpty() && norm == keep_norm) continue;  // never counted against the cap either
        if (kept < max_studies) {
            ++kept;
            continue;
        }
        QDir victim(d.absoluteFilePath());
        if (victim.removeRecursively()) ++removed;
    }
    return removed;
}

}  // namespace tcad::desktop

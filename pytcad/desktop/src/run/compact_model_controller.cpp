#include "run/compact_model_controller.hpp"

#include <QFile>

namespace tcad::desktop {

CompactModelController::CompactModelController(RunnerConfig config, QObject* parent)
    : QObject(parent), runner_(new JobRunner(std::move(config), this)) {
    connect(runner_, &JobRunner::finished, this, &CompactModelController::onFinished);
    connect(runner_, &JobRunner::failed, this,
            [this](const QString&, const QString& summary, const QString& details) {
                emit failed(summary, details);
            });
}

bool CompactModelController::extract(const nlohmann::json& job) {
    if (runner_->isRunning()) return false;
    JobRequest req;
    req.entry = JobRunner::moduleEntry("gui.services.compact_runner");
    req.job_text = QByteArray::fromStdString(job.dump());
    req.result_suffix = ".json";
    QString error;
    const QString id = runner_->start(req, &error);
    if (id.isEmpty()) {
        emit failed(tr("Could not start extraction"), error);
        return false;
    }
    emit started();
    return true;
}

void CompactModelController::stop() { runner_->cancel(); }

void CompactModelController::onFinished(const QString&, const QString& result_path) {
    QFile f(result_path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(tr("Could not read the result"), result_path);
        return;
    }
    const QByteArray bytes = f.readAll();
    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size());
    } catch (const std::exception& e) {
        emit failed(tr("Malformed result"), QString::fromUtf8(e.what()));
        return;
    }
    emit finished(manifest);
}

}  // namespace tcad::desktop

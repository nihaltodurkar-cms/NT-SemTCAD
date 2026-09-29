#include "bench_run.hpp"

#include "backend/backend_client.hpp"
#include "run/job_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace tcad::desktop {
namespace {

nlohmann::ordered_json stats(std::vector<double> ms) {
    std::sort(ms.begin(), ms.end());
    auto pct = [&](double p) {
        if (ms.empty()) return 0.0;
        const auto i = static_cast<std::size_t>(p * static_cast<double>(ms.size() - 1) + 0.5);
        return ms[std::min(i, ms.size() - 1)];
    };
    return {{"n", ms.size()}, {"p50_ms", pct(0.5)}, {"p95_ms", pct(0.95)}, {"max_ms", ms.empty() ? 0.0 : ms.back()}};
}

bool waitFor(const std::function<bool()>& done, int ms) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}

// The same env JobRunner::start() builds (PYTHONUNBUFFERED/IOENCODING,
// the app's own runtime dir kept off PATH) -- so the "direct" bare
// subprocess measurement below is a fair comparison, not penalized (or
// flattered) by a different environment than what JobRunner actually uses.
QProcessEnvironment runnerEnv(const QString& python, const QStringList& stripFromPath) {
    QProcessEnvironment env = pythonProcessEnvironment(python, stripFromPath);
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    return env;
}

}  // namespace

nlohmann::ordered_json run_run_benchmark(const RunBenchOptions& o) {
    QFile jf(o.job_path);
    if (!jf.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read " + o.job_path.toStdString());
    const QByteArray job_text = jf.readAll();

    const BackendConfig cfg = resolveBackendConfig();
    RunnerConfig rc;
    rc.python = cfg.python;
    rc.working_dir = cfg.working_dir;
    rc.strip_from_path = cfg.strip_from_path;
    if (rc.python.isEmpty()) throw std::runtime_error("no solver interpreter configured (backend_python)");

    QTemporaryDir tmp;
    if (!tmp.isValid()) throw std::runtime_error("cannot create a scratch directory");

    std::vector<double> first_newton, native_total;
    int no_newton_count = 0;  // repeats with no "newton" progress record at all (e.g. an MPI-engine run)
    for (int i = 0; i < o.repeats; ++i) {
        rc.work_dir = QDir(tmp.path()).absoluteFilePath(QString("native-%1").arg(i));
        JobRunner runner(rc);
        QElapsedTimer clock;
        // A "stage" PYTCAD_PROGRESS record (progress_channel.py's own
        // ProgressTap.emit("stage", ...)) ALWAYS precedes the first
        // "newton" one for every job, by protocol design -- it is not a
        // fallback for "no Newton data," so it must not be counted here,
        // or every job would misreport as stage-only.
        bool progressed = false, ended = false;
        QObject::connect(&runner, &JobRunner::progress, [&](const ProgressRecord& r) {
            if (!progressed && r.value.value("event", std::string()) == "newton") {
                progressed = true;
                first_newton.push_back(static_cast<double>(clock.nsecsElapsed()) / 1e6);
            }
        });
        QObject::connect(&runner, &JobRunner::finished, [&](const QString&, const QString&) {
            ended = true;
            if (!progressed) ++no_newton_count;
        });
        QObject::connect(&runner, &JobRunner::failed, [&](const QString&, const QString& s, const QString& d) {
            ended = true;
            throw std::runtime_error("solve failed: " + s.toStdString() + ": " + d.toStdString());
        });
        clock.start();
        QString err;
        if (runner.start({JobRunner::moduleEntry("gui.services.solver_runner"), job_text}, &err).isEmpty())
            throw std::runtime_error("could not start: " + err.toStdString());
        if (!waitFor([&] { return ended; }, 300000)) throw std::runtime_error("run did not finish in time");
        native_total.push_back(static_cast<double>(clock.nsecsElapsed()) / 1e6);
    }

    std::vector<double> direct_total;
    const QProcessEnvironment env = runnerEnv(rc.python, rc.strip_from_path);
    for (int i = 0; i < o.repeats; ++i) {
        const QDir dir(QDir(tmp.path()).absoluteFilePath(QString("direct-%1").arg(i)));
        QDir().mkpath(dir.absolutePath());
        const QString job_path = dir.absoluteFilePath("job.json");
        const QString out_path = dir.absoluteFilePath("result.npz");
        QFile out(job_path);
        if (!out.open(QIODevice::WriteOnly) || out.write(job_text) != job_text.size())
            throw std::runtime_error("cannot write " + job_path.toStdString());
        out.close();

        QProcess proc;
        proc.setProcessEnvironment(env);
        proc.setWorkingDirectory(rc.working_dir);
        QElapsedTimer clock;
        clock.start();
        proc.start(rc.python, QStringList{"-u", "-m", "gui.services.solver_runner", job_path, out_path});
        if (!proc.waitForFinished(300000)) throw std::runtime_error("direct run did not finish in time");
        direct_total.push_back(static_cast<double>(clock.nsecsElapsed()) / 1e6);
        if (proc.exitCode() != 0)
            throw std::runtime_error("direct run failed, exit " + std::to_string(proc.exitCode()));
    }

    nlohmann::ordered_json out;
    out["job"] = o.job_path.toStdString();
    out["repeats"] = o.repeats;
    // Run-click-to-first-progress: wall time to the first REAL Newton
    // iteration record, over repeats that had one (see no_newton_count).
    out["first_newton_progress_ms"] = stats(first_newton);
    // Repeats with no "newton" record at all by the time the run finished
    // (e.g. an MPI-engine run, which the Telemetry dock already documents
    // as stage-level only) -- read alongside first_newton_progress_ms,
    // never folded into it silently.
    out["no_newton_progress_count"] = no_newton_count;
    out["native_total_ms"] = stats(native_total);
    out["direct_subprocess_total_ms"] = stats(direct_total);
    double native_p50 = out["native_total_ms"]["p50_ms"].get<double>();
    double direct_p50 = out["direct_subprocess_total_ms"]["p50_ms"].get<double>();
    out["jobrunner_overhead_ms_p50"] = native_p50 - direct_p50;
    return out;
}

}  // namespace tcad::desktop

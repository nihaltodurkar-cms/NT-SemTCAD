// RemoteJobRunner tests (NATIVE-DESKTOP-PLAN.md section 18, P3-S8): the
// four-stage chain against gui/tests/fixtures/fake_ssh.py/fake_scp.py/
// fake_ssh_always_fail.py -- the SAME loopback stand-ins the Python
// RemoteJobRunner's own tests use ("a second local process pretending
// to be remote over loopback," no real network).
//
// Run by gui/tests/test_desktop_remote_job_runner.py, which sets:
//   TCAD_TEST_PYTHON       the solver's interpreter (tcad-dev)
//   TCAD_TEST_ROOT         pytcad/ (the modules' root)
//   TCAD_TEST_DATA         job.json (an equilibrium resistor job) + a
//                          scratch area
//   TCAD_TEST_FAKE_SSH     gui/tests/fixtures/fake_ssh.py
//   TCAD_TEST_FAKE_SSH_FAIL  gui/tests/fixtures/fake_ssh_always_fail.py
//   TCAD_TEST_FAKE_SCP     gui/tests/fixtures/fake_scp.py
#include "run/remote_host.hpp"
#include "run/remote_job_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QtTest/QtTest>

#include <functional>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using tcad::desktop::JobRequest;
using tcad::desktop::posixQuote;
using tcad::desktop::RemoteHostConfig;
using tcad::desktop::RemoteJobRunner;
using tcad::desktop::RunnerBase;
using tcad::desktop::validateRemoteHost;

namespace {

QString env(const char* name) { return qEnvironmentVariable(name); }
QString data(const QString& name) { return QDir(env("TCAD_TEST_DATA")).absoluteFilePath(name); }

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool processAlive(qint64 pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

bool waitUntil(const std::function<bool()>& cond, int ms) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

// Everything a run reports.
struct Log {
    QStringList started, finished, canceled, failed;
    QString result, summary, details;

    explicit Log(RunnerBase& r) {
        QObject::connect(&r, &RunnerBase::finished, [this](const QString& id, const QString& path) {
            finished << id;
            result = path;
        });
        QObject::connect(&r, &RunnerBase::failed,
                         [this](const QString& id, const QString& s, const QString& d) {
                             failed << id;
                             summary = s;
                             details = d;
                         });
        QObject::connect(&r, &RunnerBase::canceled, [this](const QString& id) { canceled << id; });
    }
    int ended() const { return finished.size() + failed.size() + canceled.size(); }
};

}  // namespace

class TestRemoteJobRunner : public QObject {
    Q_OBJECT

    QString work_;

    RemoteHostConfig host(const QString& remoteWorkdir) const {
        RemoteHostConfig h;
        h.host = "loopback-worker";
        h.python = env("TCAD_TEST_PYTHON");
        h.remote_workdir = remoteWorkdir;
        return h;
    }

    std::unique_ptr<RemoteJobRunner> runner(const RemoteHostConfig& h, const QString& sub,
                                           const QStringList& sshCmd = {}) {
        const QStringList ssh = sshCmd.isEmpty() ? QStringList{env("TCAD_TEST_PYTHON"), env("TCAD_TEST_FAKE_SSH")}
                                                 : sshCmd;
        const QStringList scp = {env("TCAD_TEST_PYTHON"), env("TCAD_TEST_FAKE_SCP")};
        return std::make_unique<RemoteJobRunner>(h, QDir(work_).absoluteFilePath(sub), nullptr, ssh, scp,
                                                 env("TCAD_TEST_ROOT"));
    }

    static JobRequest solverJob() {
        return {{"-m", "gui.services.solver_runner"}, readFile(data("job.json"))};
    }

    static std::unique_ptr<Log> runToEnd(RunnerBase& r, const JobRequest& req, int ms = 120000) {
        auto log = std::make_unique<Log>(r);
        QString err;
        const QString id = r.start(req, &err);
        if (id.isEmpty()) {
            log->summary = "start refused: " + err;
            return log;
        }
        waitUntil([&] { return log->ended() > 0; }, ms);
        return log;
    }

private slots:
    void initTestCase() {
        for (const char* v :
            {"TCAD_TEST_PYTHON", "TCAD_TEST_ROOT", "TCAD_TEST_DATA", "TCAD_TEST_FAKE_SSH", "TCAD_TEST_FAKE_SCP",
             "TCAD_TEST_FAKE_SSH_FAIL"})
            QVERIFY2(!env(v).isEmpty(), v);
        work_ = data("work");
        QVERIFY(QDir().mkpath(work_));
    }

    // -- host validation (18.1 finding 1) ----------------------------------------------
    void aHostileHostIsRefusedBeforeAnyProcessStarts() {
        RemoteHostConfig h = host(QDir(work_).absoluteFilePath("wd1"));
        h.host = "-oProxyCommand=touch pwned";
        auto r = runner(h, "run1");
        QString err;
        QCOMPARE(r->start(solverJob(), &err), QString());
        QVERIFY2(err.contains("must not start with '-'"), qPrintable(err));
        QVERIFY(!r->isRunning());
    }

    void aHostWithWhitespaceIsRefused() {
        RemoteHostConfig h = host(QDir(work_).absoluteFilePath("wd2"));
        h.host = "has space";
        auto r = runner(h, "run2");
        QString err;
        QCOMPARE(r->start(solverJob(), &err), QString());
        QVERIFY2(err.contains("whitespace"), qPrintable(err));
    }

    // -- argv shape: "--" before the target, one quoted command string ----------------
    void everyStageHasDoubleDashBeforeTheTarget() {
        RemoteHostConfig h = host("/tmp/pytcad-remote");
        auto r = runner(h, "run3");
        for (int stage : {RemoteJobRunner::kMkdir, RemoteJobRunner::kPush, RemoteJobRunner::kRun,
                          RemoteJobRunner::kPull}) {
            const QStringList argv = r->argvForStage(stage);
            QVERIFY(argv.contains("--"));
        }
    }

    void mkdirAndRunCommandsAreShellQuoted() {
        RemoteHostConfig h = host("/tmp/a b");
        h.python = "/usr/bin/python 3";
        auto r = runner(h, "run4");
        const QString mkdir_cmd = r->argvForStage(RemoteJobRunner::kMkdir).last();
        QCOMPARE(mkdir_cmd, QStringLiteral("mkdir -p ") + posixQuote("/tmp/a b"));
        // argvForStage(kRun) reads request_.entry, set only once start() has
        // been called (the mkdir stage's own -- fake -- process is real, but
        // that is fine: this test only inspects the NEXT stage's argv, then
        // cancels before it runs).
        QString err;
        QVERIFY(!r->start(solverJob(), &err).isEmpty());
        const QString run_cmd = r->argvForStage(RemoteJobRunner::kRun).last();
        QVERIFY(run_cmd.startsWith(posixQuote("/usr/bin/python 3") + " -m gui.services.solver_runner "));
        r->cancel();
        QVERIFY(waitUntil([&] { return !r->isRunning(); }, 15000));
    }

    // -- the full chain against the real fixtures, to a real result ---------------------
    void theFullChainReachesARealResult() {
        const QString wd = QDir(work_).absoluteFilePath("remote-wd-happy");
        QVERIFY(QDir().mkpath(wd));
        auto r = runner(host(wd), "run5");
        auto log = runToEnd(*r, solverJob());
        QVERIFY2(log->failed.isEmpty(), qPrintable(log->summary + ": " + log->details));
        QCOMPARE(log->finished.size(), 1);
        QVERIFY(QFileInfo::exists(log->result));
        // No local job file left behind.
        QVERIFY(QDir(QDir(work_).absoluteFilePath("run5")).entryList({"job-*.json"}, QDir::Files).isEmpty());
    }

    // -- quoting round-trips: a hostile workdir never runs its injected command --------
    void aWorkdirWithASpaceAndAMetacharacterRoundTrips() {
        const QString wd = QDir(work_).absoluteFilePath("remote wd; touch pwned");
        QVERIFY(QDir().mkpath(wd));
        auto r = runner(host(wd), "run6");
        auto log = runToEnd(*r, solverJob());
        QVERIFY2(log->failed.isEmpty(), qPrintable(log->summary + ": " + log->details));
        QVERIFY(!QFileInfo::exists(QDir(work_).absoluteFilePath("pwned")));
        QVERIFY(!QFileInfo::exists(QDir(wd).absoluteFilePath("pwned")));
    }

    // -- a bad host is named-failed, never a hang ---------------------------------------
    void aBadHostFailsNamed() {
        const QStringList bad_ssh = {env("TCAD_TEST_PYTHON"), env("TCAD_TEST_FAKE_SSH_FAIL")};
        const QString wd = QDir(work_).absoluteFilePath("remote-wd-bad");
        auto r = runner(host(wd), "run7", bad_ssh);
        auto log = runToEnd(*r, solverJob());
        QCOMPARE(log->finished.size(), 0);
        QCOMPARE(log->failed.size(), 1);
        QVERIFY2(log->summary.contains("mkdir"), qPrintable(log->summary));
    }

    // -- cancel mid-stage: killed at once, no leftover job file -------------------------
    void cancelMidRunKillsTheLiveProcess() {
        const QString wd = QDir(work_).absoluteFilePath("remote-wd-cancel");
        QVERIFY(QDir().mkpath(wd));
        auto r = runner(host(wd), "run8");
        Log log(*r);
        QString err;
        // A real solve is too fast to reliably still be in the "run" stage
        // by the time cancel() below fires (mkdir/push/run/pull could all
        // have finished on their own) -- a long, deterministic sleep in
        // place of the real solver_runner entry point removes that race:
        // remote_job_/remote_out_ are appended as harmless extra argv.
        const JobRequest sleepy{{"-c", "import time; time.sleep(30)"}, QByteArray()};
        QVERIFY(!r->start(sleepy, &err).isEmpty());
        QVERIFY(waitUntil([&] { return r->isRunning() && r->currentStage() == RemoteJobRunner::kRun; }, 10000));
        const qint64 pid = r->currentPid();
        QElapsedTimer clock;
        clock.start();
        r->cancel();
        // terminate() must end it well inside the 3 s grace-kill window --
        // a cancel() that relies on the grace timer alone (i.e. does
        // nothing itself) would still pass a 15 s wait, silently hiding a
        // "cancel doesn't actually kill anything" bug.
        QVERIFY(waitUntil([&] { return log.ended() > 0; }, 2500));
        QVERIFY2(clock.elapsed() < 2500, qPrintable(QString("cancel took %1 ms").arg(clock.elapsed())));
        QCOMPARE(log.canceled.size(), 1);
        QVERIFY(QDir(QDir(work_).absoluteFilePath("run8")).entryList({"job-*.json"}, QDir::Files).isEmpty());
        if (pid > 0) QVERIFY(waitUntil([&] { return !processAlive(pid); }, 5000));
    }

    // -- RunnerBase polymorphism: the same base pointer/signals as JobRunner -----------
    void isUsableThroughRunnerBase() {
        const QString wd = QDir(work_).absoluteFilePath("remote-wd-base");
        QVERIFY(QDir().mkpath(wd));
        std::unique_ptr<RunnerBase> r = runner(host(wd), "run9");
        auto log = runToEnd(*r, solverJob());
        QVERIFY2(log->failed.isEmpty(), qPrintable(log->summary + ": " + log->details));
        QCOMPARE(log->finished.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestRemoteJobRunner)
#include "test_remote_job_runner.moc"

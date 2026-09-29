// Unit tests of the Qt-free, Win32-free core of the native platform layer (N1): the JSON-RPC session against a
// scripted fake backend, backend/PATH configuration, settings, DPI/layout arithmetic, shortcuts. Portable: built
// by CMake (tcad_platform_core_tests) and by gui/tests/test_desktop_platform_core.py with any C++23 compiler.
#include "mini_test.hpp"

#include "platform/dpi.hpp"
#include "platform/env_path.hpp"
#include "platform/input.hpp"
#include "platform/rpc_session.hpp"
#include "platform/settings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using namespace tcad::platform;
using nlohmann::json;

namespace {

fs::path scratch(const std::string& name) {
    static int n = 0;
    fs::path p = fs::temp_directory_path() / ("tcad_ptest_" + name + "_" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())) + "_" + std::to_string(++n));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

}  // namespace

namespace {

// A scripted stand-in for the child process and the UI thread.
class FakeTransport : public RpcTransport {
public:
    RpcSession* session = nullptr;
    bool fail_start = false;
    std::string fail_start_error = "access denied";
    int starts = 0;
    bool running_ = false;
    bool kill_requested = false;
    bool stdin_closed = false;
    bool exit_on_wait = true;   // waitForExit(): the child exits promptly after system.shutdown
    bool detached = false;
    std::vector<std::vector<std::string>> writes;  // per started process
    std::vector<std::function<void()>> deferred;
    bool timer_armed = false;
    int timer_ms = 0;
    double now = 1000;

    bool start(const RpcConfig&, std::string* error) override {
        if (fail_start) {
            *error = fail_start_error;
            return false;
        }
        ++starts;
        running_ = true;
        kill_requested = stdin_closed = detached = false;
        writes.emplace_back();
        return true;
    }
    bool running() const override { return running_; }
    void write(const std::string& b) override { writes.back().push_back(b); }
    void closeStdin() override { stdin_closed = true; }
    void kill() override { kill_requested = true; }
    bool waitForExit(int) override {
        if (exit_on_wait) running_ = false;
        return exit_on_wait;
    }
    void detach() override { detached = true; }
    void armTimer(int ms) override { timer_armed = true; timer_ms = ms; }
    void cancelTimer() override { timer_armed = false; }
    void defer(std::function<void()> fn) override { deferred.push_back(std::move(fn)); }
    double nowMs() const override { return now; }

    void runDeferred() {
        auto d = std::move(deferred);
        deferred.clear();
        for (auto& f : d) f();
    }
    json lastRequest() const { return json::parse(writes.back().back()); }
    int requestCount() const { return static_cast<int>(writes.back().size()); }
    // The child's answer to the request last written.
    void answer(const json& result) {
        const json req = lastRequest();
        session->onStdout(json({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", result}}).dump() + "\n");
    }
    void answerError(int code, const std::string& msg, json data = nullptr) {
        const json req = lastRequest();
        json e = {{"code", code}, {"message", msg}};
        if (!data.is_null()) e["data"] = data;
        session->onStdout(json({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"error", e}}).dump() + "\n");
    }
    void handshake(const std::string& scratch_dir = "") {
        answer({{"pong", true}, {"protocol", 1}});
        answer({{"protocol", 1}, {"pid", 4242}, {"prefix", "/opt/py"}, {"python", "3.14"}, {"scratch_dir", scratch_dir}});
    }
    // The child dies; the session hears about it later, like a real transport.
    void die(int code = 1, bool crashed = false) {
        running_ = false;
        session->onExited(code, crashed);
    }
};

struct Rig {
    FakeTransport t;
    fs::path dir = scratch("rig");
    fs::path python;
    RpcConfig cfg;
    std::unique_ptr<RpcSession> s;
    Rig() {
        python = dir / "python.exe";
        std::ofstream(python) << "x";
        cfg.python = python.string();
        s = std::make_unique<RpcSession>(cfg, t);
        t.session = s.get();
    }
    ~Rig() {
        s.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

TEST(rpc_handshake_comes_first_then_the_callers_request) {
    Rig r;
    auto reply = r.s->call("examples.list", json::object({{"a", 1}}));
    CHECK(r.s->state() == RpcSession::State::Starting);
    CHECK_EQ(r.t.starts, 1);
    CHECK_EQ(r.t.requestCount(), 1);
    CHECK_EQ(r.t.lastRequest()["method"], "system.ping");   // nothing of the caller's before the handshake
    CHECK(r.t.timer_armed);
    r.t.answer({{"pong", true}, {"protocol", 1}});
    CHECK_EQ(r.t.lastRequest()["method"], "system.info");
    r.t.answer({{"protocol", 1}, {"pid", 4242}, {"prefix", "/opt/py"}, {"scratch_dir", "/tmp/x"}});
    CHECK(r.s->state() == RpcSession::State::Ready);
    CHECK_EQ(r.s->backendPid(), 4242);
    CHECK_EQ(r.s->backendPrefix(), "/opt/py");
    CHECK_EQ(r.t.lastRequest()["method"], "examples.list");
    CHECK_EQ(r.t.lastRequest()["params"]["a"], 1);
    CHECK(!reply->isFinished());
    r.t.answer(json::array({1, 2, 3}));
    CHECK(reply->ok());
    CHECK_EQ(reply->result().size(), 3u);
    CHECK(!r.t.timer_armed);
}

TEST(rpc_one_request_in_flight_fifo) {
    Rig r;
    auto a = r.s->call("m.a");
    auto b = r.s->call("m.b");
    auto c = r.s->call("m.c");
    r.t.handshake();
    CHECK_EQ(r.t.lastRequest()["method"], "m.a");
    CHECK_EQ(r.t.requestCount(), 3);                        // ping, info, a -- b and c wait in the queue
    r.t.answer(1);
    CHECK_EQ(r.t.lastRequest()["method"], "m.b");
    CHECK(a->ok() && !b->isFinished());
    r.t.answer(2);
    CHECK_EQ(r.t.lastRequest()["method"], "m.c");
    r.t.answer(3);
    CHECK(b->ok() && c->ok());
    CHECK_EQ(c->result().get<int>(), 3);
}

TEST(rpc_requests_are_ascii_escaped_and_cr_is_stripped) {
    Rig r;
    auto reply = r.s->call("spec.load", json::object({{"name", "caf\xC3\xA9 \xE2\x82\xAC"}}));
    r.t.handshake();
    const std::string line = r.t.writes.back().back();
    for (unsigned char ch : line) CHECK(ch < 0x80);
    CHECK(line.find("\\u00e9") != std::string::npos);
    CHECK_EQ(json::parse(line)["params"]["name"].get<std::string>(), "caf\xC3\xA9 \xE2\x82\xAC");
    const json req = r.t.lastRequest();
    r.s->onStdout(json({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", "ok"}}).dump() + "\r\n");  // Python text mode
    CHECK(reply->ok());
}

TEST(rpc_a_reply_split_across_reads_and_two_in_one_read) {
    Rig r;
    auto a = r.s->call("m.a");
    r.t.handshake();
    const json req = r.t.lastRequest();
    const std::string full = json({{"jsonrpc", "2.0"}, {"id", req["id"]}, {"result", "x"}}).dump() + "\n";
    r.s->onStdout(full.substr(0, 7));
    CHECK(!a->isFinished());
    r.s->onStdout(full.substr(7));
    CHECK(a->ok());
}

TEST(rpc_error_reply_carries_code_message_and_data) {
    Rig r;
    auto reply = r.s->call("spec.load");
    r.t.handshake();
    r.t.answerError(-32602, "bad params", {{"type", "ValueError"}, {"detail", "x"}});
    CHECK(reply->isFinished() && !reply->ok());
    CHECK_EQ(reply->errorCode(), -32602);
    CHECK_EQ(reply->errorMessage(), "ValueError: bad params");
    CHECK_EQ(reply->errorData()["detail"], "x");
    CHECK(r.s->state() == RpcSession::State::Ready);        // an error reply is not a broken service
}

TEST(rpc_timeout_fails_the_call_kills_and_restarts_for_the_queue) {
    Rig r;
    auto a = r.s->call("slow", nullptr, 500);
    auto b = r.s->call("next");
    r.t.handshake();
    CHECK_EQ(r.t.timer_ms, 500);
    r.s->onTimer();
    CHECK(a->isFinished() && a->errorCode() == Reply::kTimedOut);
    CHECK(r.t.kill_requested);
    CHECK_EQ(r.t.requestCount(), 3);                        // nothing more written into the dying process
    r.t.die(1);
    CHECK_EQ(r.t.starts, 2);                                // the queue goes on in a fresh process
    CHECK_EQ(r.t.lastRequest()["method"], "system.ping");
    CHECK_EQ(r.s->restarts(), 1);
    r.t.handshake();
    CHECK_EQ(r.t.lastRequest()["method"], "next");
    r.t.answer("ok");
    CHECK(b->ok());
}

TEST(rpc_exit_during_a_call_reports_code_and_stderr_tail) {
    Rig r;
    auto a = r.s->call("boom");
    r.t.handshake();
    r.s->onStderr("Traceback...\nValueError: nope\n");
    r.t.die(3);
    CHECK(a->isFinished());
    CHECK_EQ(a->errorCode(), Reply::kBackendExited);
    CHECK(a->errorMessage().find("exited with code 3") != std::string::npos);
    CHECK(a->errorMessage().find("ValueError: nope") != std::string::npos);
    CHECK(a->errorMessage().find("during boom") != std::string::npos);
}

TEST(rpc_three_unexpected_exits_in_30s_give_up_until_reset) {
    Rig r;
    for (int i = 0; i < 2; ++i) {           // a call in flight when the service dies fails, and a new call restarts it
        auto a = r.s->call("x");
        r.t.handshake();
        r.t.now += 1000;
        r.t.die(1);
        CHECK(a->isFinished() && a->errorCode() == Reply::kBackendExited);
        CHECK(r.s->state() == RpcSession::State::NotStarted);
    }
    auto x = r.s->call("x");
    auto y = r.s->call("y");                // queued behind x
    r.t.handshake();
    r.t.now += 1000;
    r.t.die(1, true);                       // the third exit within 30 s
    CHECK(r.s->state() == RpcSession::State::GaveUp);
    CHECK_EQ(x->errorCode(), Reply::kBackendExited);
    CHECK(y->isFinished());
    CHECK_EQ(y->errorCode(), Reply::kGaveUp);
    CHECK(y->errorMessage().find("keeps crashing") != std::string::npos);
    CHECK_EQ(r.t.starts, 3);                // no fourth process
    auto b = r.s->call("z");
    CHECK(!b->isFinished());                // never finishes inside call()
    r.t.runDeferred();
    CHECK(b->isFinished() && b->errorCode() == Reply::kGaveUp);
    r.s->resetBackoff();
    CHECK(r.s->state() == RpcSession::State::NotStarted);
    auto again = r.s->call("w");
    CHECK_EQ(r.t.starts, 4);                // allowed to start again
}

TEST(rpc_crashes_more_than_30s_apart_are_not_counted_together) {
    Rig r;
    for (int i = 0; i < 4; ++i) {
        auto a = r.s->call("x");
        r.t.handshake();
        r.t.now += 40000;
        r.t.die(1);
        CHECK(r.s->state() != RpcSession::State::GaveUp);
    }
}

TEST(rpc_protocol_version_mismatch_gives_up) {
    Rig r;
    auto a = r.s->call("x");
    r.t.answer({{"pong", true}, {"protocol", 2}});
    CHECK(r.s->state() == RpcSession::State::GaveUp);
    CHECK(a->isFinished());
    CHECK_EQ(a->errorCode(), Reply::kProtocolError);
    CHECK(a->errorMessage().find("protocol 2") != std::string::npos);
    CHECK(r.t.kill_requested);
}

TEST(rpc_malformed_line_and_id_mismatch_are_protocol_errors_that_restart_clean) {
    {
        Rig r;
        auto a = r.s->call("x");
        r.t.handshake();
        r.s->onStdout("this is not json\n");
        CHECK(a->isFinished() && a->errorCode() == Reply::kProtocolError);
        CHECK(r.t.kill_requested);
    }
    {
        Rig r;
        auto a = r.s->call("x");
        r.t.handshake();
        r.s->onStdout("{\"jsonrpc\":\"2.0\",\"id\":9999,\"result\":1}\n");
        CHECK(a->isFinished() && a->errorCode() == Reply::kProtocolError);
        CHECK(a->errorMessage().find("does not match") != std::string::npos);
        CHECK(r.t.kill_requested);
    }
}

TEST(rpc_unsolicited_reply_is_ignored_and_noted) {
    Rig r;
    auto a = r.s->call("x");
    r.t.handshake();
    r.t.answer(1);
    CHECK(a->ok());
    r.s->onStdout("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":1}\n");
    CHECK(!r.s->stderrTail().empty());
    CHECK(r.s->stderrTail().back().find("unsolicited") != std::string::npos);
    CHECK(!r.t.kill_requested);
}

TEST(rpc_cannot_start_variants_fail_asynchronously) {
    {
        RpcConfig cfg;  // no interpreter configured
        FakeTransport t;
        RpcSession s(cfg, t);
        t.session = &s;
        auto a = s.call("x");
        CHECK(!a->isFinished());
        CHECK(s.state() == RpcSession::State::GaveUp);
        t.runDeferred();
        CHECK(a->isFinished() && a->errorCode() == Reply::kCannotStart);
        CHECK(a->errorMessage().find("no backend interpreter") != std::string::npos);
    }
    {
        RpcConfig cfg;
        cfg.python = "/definitely/not/here/python.exe";
        FakeTransport t;
        RpcSession s(cfg, t);
        t.session = &s;
        auto a = s.call("x");
        t.runDeferred();
        CHECK(a->errorCode() == Reply::kCannotStart && a->errorMessage().find("does not exist") != std::string::npos);
    }
    {
        Rig r;
        r.t.fail_start = true;
        auto a = r.s->call("x");
        CHECK(!a->isFinished());
        r.t.runDeferred();
        CHECK(a->errorCode() == Reply::kCannotStart && a->errorMessage().find("access denied") != std::string::npos);
    }
}

TEST(rpc_shutdown_asks_politely_waits_and_removes_the_scratch_dir) {
    Rig r;
    const fs::path scratch_dir = fs::temp_directory_path() / ("tcad_backend_ptest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch_dir);
    auto a = r.s->call("x");
    r.t.handshake(scratch_dir.string());
    auto b = r.s->call("y");                               // queued behind x
    r.s->shutdown();
    CHECK_EQ(r.t.lastRequest()["method"], "system.shutdown");
    CHECK(r.t.stdin_closed);
    CHECK(!r.t.kill_requested);                            // it exited within the wait
    CHECK(r.t.detached);
    CHECK(a->isFinished() && a->errorCode() == Reply::kShutDown);
    CHECK(b->isFinished() && b->errorCode() == Reply::kShutDown);
    CHECK(!fs::exists(scratch_dir));
    CHECK(r.s->state() == RpcSession::State::NotStarted);
}

TEST(rpc_shutdown_kills_a_service_that_does_not_exit) {
    Rig r;
    r.t.exit_on_wait = false;
    auto a = r.s->call("x");
    r.t.handshake();
    r.s->shutdown();
    CHECK(r.t.kill_requested);
}

TEST(rpc_scratch_removal_refuses_a_directory_that_is_not_the_services_own) {
    Rig r;
    const fs::path other = scratch("keepme");               // not named tcad_backend_*
    auto a = r.s->call("x");
    r.t.handshake(other.string());
    r.s->shutdown();
    CHECK(fs::exists(other));
    fs::remove_all(other);
}

TEST(rpc_a_call_after_shutdown_starts_a_fresh_process) {
    Rig r;
    auto a = r.s->call("x");
    r.t.handshake();
    r.s->shutdown();
    auto b = r.s->call("y");
    CHECK_EQ(r.t.starts, 2);
    CHECK_EQ(r.t.lastRequest()["method"], "system.ping");
    r.t.handshake();
    r.t.answer(1);
    CHECK(b->ok());
}

TEST(rpc_then_runs_once_immediately_when_already_finished) {
    Rig r;
    auto a = r.s->call("x");
    r.t.handshake();
    r.t.answer(5);
    int n = 0;
    a->then([&](Reply& rep) { n += rep.result().get<int>(); });
    CHECK_EQ(n, 5);
}

// ---------------------------------------------------------------------------------------------
// PATH and configuration
// ---------------------------------------------------------------------------------------------

TEST(path_strip_and_normalisation) {
    CHECK_EQ(normalizedDir("C:\\Foo\\Bar\\"), "c:/foo/bar");
    CHECK_EQ(normalizedDir("/a/b/../c/"), "/a/c");
    const std::string p = pythonPathEnvironment("/x/one;/x/Two;/x/three", "", {"/X/TWO/"}, ';', false);
    CHECK_EQ(p, "/x/one;/x/three");
}

TEST(path_conda_prefix_directories_are_prepended_when_they_exist) {
    const fs::path prefix = scratch("conda");
    fs::create_directories(prefix / "Library" / "bin");
    fs::create_directories(prefix / "Scripts");
    std::ofstream(prefix / "python.exe") << "x";
    const std::string bin = (prefix / "Library" / "bin").generic_string();
    const std::string out = pythonPathEnvironment("/sys/a;" + bin + ";/sys/b", (prefix / "python.exe").string(), {}, ';', false);
    std::vector<std::string> parts;
    std::stringstream ss(out);
    for (std::string x; std::getline(ss, x, ';');) parts.push_back(x);
    CHECK_EQ(parts.front(), prefix.generic_string());       // <prefix> first, as `conda activate` puts it
    CHECK(std::count(parts.begin(), parts.end(), bin) == 1);  // and its Library/bin once, not twice
    CHECK(std::find(parts.begin(), parts.end(), (prefix / "Scripts").generic_string()) != parts.end());
    CHECK(std::find(parts.begin(), parts.end(), (prefix / "bin").generic_string()) == parts.end());  // absent dir: not added
    CHECK(parts.back() == "/sys/b");
    // not a conda-style prefix: PATH is only stripped
    const fs::path plain = scratch("plain");
    std::ofstream(plain / "python.exe") << "x";
    CHECK_EQ(pythonPathEnvironment("/a;/b", (plain / "python.exe").string(), {}, ';', false), "/a;/b");
    fs::remove_all(prefix);
    fs::remove_all(plain);
}

TEST(config_resolution_order_manifest_settings_environment) {
    auto env = [](std::map<std::string, std::string> m) {
        return [m](const char* k) -> std::optional<std::string> {
            auto it = m.find(k);
            if (it == m.end()) return std::nullopt;
            return it->second;
        };
    };
    const std::string manifest = R"({"runtime_bin":"runtime/bin","backend_python":"runtime/python.exe","backend_root":"backend"})";
    auto c = resolveBackendConfig("/app", manifest, "", env({}));
    CHECK_EQ(c.python, "/app/runtime/python.exe");           // relative: resolved against the app dir
    CHECK_EQ(c.working_dir, "/app/backend");
    CHECK_EQ(c.strip_from_path.size(), 1u);
    c = resolveBackendConfig("/app", manifest, "/settings/py.exe", env({}));
    CHECK_EQ(c.python, "/settings/py.exe");                   // settings beat the manifest
    c = resolveBackendConfig("/app", manifest, "/settings/py.exe", env({{"TCAD_BACKEND_PYTHON", "/env/py.exe"}, {"TCAD_BACKEND_ROOT", "/env/root"}}));
    CHECK_EQ(c.python, "/env/py.exe");                        // the environment beats both
    CHECK_EQ(c.working_dir, "/env/root");
    const std::string abs = R"({"backend_python":"C:\\envs\\dev\\python.exe"})";
    CHECK_EQ(resolveBackendConfig("/app", abs, "", env({})).python, "C:\\envs\\dev\\python.exe");   // absolute: unchanged
    c = resolveBackendConfig("/app", "{ not json", "", env({}));
    CHECK(c.python.empty());                                  // an unreadable manifest is ignored
    CHECK_EQ(resolveManifestPath("/app", ""), "");
}

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------

TEST(settings_recent_files_dedupe_case_insensitively_and_cap_at_ten) {
    auto s = Settings::ephemeral();
    for (int i = 0; i < 12; ++i) s.addRecent("/data/r" + std::to_string(i) + ".npz");
    CHECK_EQ(s.recentFiles().size(), Settings::kMaxRecent);
    CHECK(samePath(s.recentFiles().front(), "/data/r11.npz"));
    s.addRecent("/DATA/R5.NPZ");
    CHECK(samePath(s.recentFiles().front(), "/data/r5.npz"));
    const auto recent = s.recentFiles();
    CHECK_EQ(std::count_if(recent.begin(), recent.end(), [](const std::string& p) { return samePath(p, "/data/r5.npz"); }), 1);
    s.removeRecent("/data/R5.npz");
    CHECK(!samePath(s.recentFiles().front(), "/data/r5.npz"));
    s.addRecentProject("/p/a.tcad");
    CHECK_EQ(s.recentProjects().size(), 1u);
    CHECK(s.recentFiles().size() == 9);                       // the two lists are independent
    s.clearRecent();
    CHECK(s.recentFiles().empty());
    CHECK(!s.persistent());
    CHECK(s.sync());                                          // ephemeral: nothing to write, no failure
}

TEST(settings_round_trip_through_a_file_including_window_placement) {
    const fs::path d = scratch("settings");
    const fs::path f = d / "sub" / "settings.json";
    {
        auto s = Settings::atFile(f);
        s.addRecent("/data/a.npz");
        s.setValue("backend/python", "C:\\py\\python.exe");
        s.setWindowPlacement({100, 50, 1280, 800, true});
        CHECK(s.sync());
    }
    CHECK(fs::exists(f));
    CHECK(!fs::exists(fs::path(f.string() + ".tmp")));       // the temp file is gone: the write was atomic
    auto t = Settings::atFile(f);
    CHECK_EQ(t.recentFiles().size(), 1u);
    CHECK_EQ(t.value("backend/python"), "C:\\py\\python.exe");
    CHECK_EQ(t.value("missing", "fallback"), "fallback");
    const auto wp = t.windowPlacement();
    CHECK(wp.has_value() && *wp == (WindowPlacement{100, 50, 1280, 800, true}));
    CHECK(t.loadProblem().empty());
    fs::remove_all(d);
}

TEST(settings_a_corrupt_file_is_kept_aside_not_overwritten) {
    const fs::path d = scratch("corrupt");
    const fs::path f = d / "settings.json";
    std::ofstream(f) << "{ this is not json";
    auto s = Settings::atFile(f);
    CHECK(!s.loadProblem().empty());
    CHECK(s.recentFiles().empty());
    CHECK(!s.windowPlacement().has_value());
    s.addRecent("/x.npz");
    CHECK(s.sync());
    std::ifstream bad(fs::path(f.string() + ".corrupt"));
    std::stringstream b;
    b << bad.rdbuf();
    CHECK_EQ(b.str(), "{ this is not json");                  // the user's bytes are preserved
    CHECK(Settings::atFile(f).loadProblem().empty());         // the new file is valid
    fs::remove_all(d);
}

TEST(settings_another_version_is_ignored_and_kept_aside) {
    const fs::path d = scratch("ver");
    const fs::path f = d / "settings.json";
    std::ofstream(f) << R"({"version": 99, "recent": {"files": ["/x"]}})";
    auto s = Settings::atFile(f);
    CHECK(s.recentFiles().empty());
    CHECK(s.loadProblem().find("another version") != std::string::npos);
    fs::remove_all(d);
}

TEST(settings_placement_is_validated) {
    const fs::path d = scratch("placement");
    const fs::path f = d / "settings.json";
    std::ofstream(f) << R"({"version": 1, "window": {"x": 1, "y": 2, "w": 0, "h": 5}})";
    CHECK(!Settings::atFile(f).windowPlacement().has_value());   // zero width: not a usable placement
    std::ofstream(f) << R"({"version": 1, "window": {"x": "1"}})";
    CHECK(!Settings::atFile(f).windowPlacement().has_value());
    fs::remove_all(d);
}

// ---------------------------------------------------------------------------------------------
// DPI, layout, input
// ---------------------------------------------------------------------------------------------

TEST(dpi_conversion_rounds_half_away_from_zero) {
    CHECK_EQ(scaleFromDpi(96), 1.0);
    CHECK_EQ(scaleFromDpi(144), 1.5);
    CHECK_EQ(scaleFromDpi(0), 1.0);
    CHECK_EQ(toDevice(24, 1.25), 30);
    CHECK_EQ(toDevice(24, 1.5), 36);
    CHECK_EQ(toDevice(10, 1.25), 13);                        // 12.5 -> 13
    CHECK_EQ(toLogical(30, 1.25), 24.0);
}

TEST(layout_splits_the_client_into_content_and_a_status_strip) {
    const auto l = computeLayout(1000, 700, 1.5, 24);
    CHECK(l.client == (Rect{0, 0, 1000, 700}));
    CHECK(l.status == (Rect{0, 664, 1000, 36}));
    CHECK(l.content == (Rect{0, 0, 1000, 664}));
    const auto tiny = computeLayout(300, 10, 2.0, 24);       // a strip taller than the window: clamped
    CHECK(tiny.content.h == 0 && tiny.status.h == 10);
    const auto neg = computeLayout(-5, -5, 1.0, 24);
    CHECK(neg.client.empty() && neg.content.empty());
}

TEST(shortcuts_parse_and_dispatch) {
    const auto s = parseShortcut("Ctrl+Shift+F5");
    CHECK(s.has_value() && s->vk == 0x74 && s->mods == (Mod::Ctrl | Mod::Shift));
    CHECK(parseShortcut("ctrl+o")->vk == 'O');
    CHECK(parseShortcut("Esc")->vk == 0x1B && parseShortcut("Esc")->mods == Mod::None);
    CHECK(parseShortcut("Alt+Left")->vk == 0x25);
    CHECK(!parseShortcut("Ctrl+"));
    CHECK(!parseShortcut("Hyper+O"));
    CHECK(!parseShortcut("Ctrl+F25"));
    CHECK(!parseShortcut(""));
    int opened = 0, quit = 0;
    ShortcutMap m;
    CHECK(m.add("Ctrl+O", [&] { ++opened; }));
    CHECK(m.add("Ctrl+Q", [&] { ++quit; }));
    CHECK(!m.add("ctrl+o", [&] {}));                          // already taken
    CHECK(!m.add("Nope", [&] {}));
    CHECK(m.dispatch({'O', Mod::Ctrl, true, false}));
    CHECK(!m.dispatch({'O', Mod::None, true, false}));       // wrong modifiers
    CHECK(!m.dispatch({'O', Mod::Ctrl, true, true}));        // auto-repeat does not fire again
    CHECK(!m.dispatch({'O', Mod::Ctrl, false, false}));      // key-up does not fire
    CHECK(!m.dispatch({'O', Mod::Ctrl | Mod::Shift, true, false}));
    CHECK_EQ(opened, 1);
    CHECK_EQ(quit, 0);
}

TEST(vtk_keysyms_map_to_virtual_keys) {
    CHECK_EQ(vkFromVtkKeySym("f"), 'F');
    CHECK_EQ(vkFromVtkKeySym("F5"), 0x74);
    CHECK_EQ(vkFromVtkKeySym("Escape"), 0x1B);
    CHECK_EQ(vkFromVtkKeySym("Delete"), 0x2E);
    CHECK_EQ(vkFromVtkKeySym("3"), '3');
    CHECK_EQ(vkFromVtkKeySym("Left"), 0x25);
    CHECK_EQ(vkFromVtkKeySym("NoSuchKey"), 0);
}

int main(int argc, char** argv) { return minitest::runAll(argc, argv); }

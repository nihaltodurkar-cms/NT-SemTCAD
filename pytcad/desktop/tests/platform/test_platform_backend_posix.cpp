// RpcSession against the REAL Python backend (backend_service), over a POSIX pipe transport. The Win32 transport
// (native/backend_client.cpp, CreateProcess + pipe threads) cannot run off Windows; this drives the SAME session
// code -- the protocol, the handshake, the ordering, the error mapping, the shutdown -- through the real server
// instead of a scripted fake. Linux/macOS only; needs `python3` able to import backend_service (cwd = pytcad/).
//   test_platform_backend_posix <python3> <pytcad-dir>
#include "mini_test.hpp"

#include "platform/rpc_session.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;
using namespace tcad::platform;
using nlohmann::json;

namespace {

std::string g_python, g_root;

class PosixTransport : public RpcTransport {
public:
    RpcSession* session = nullptr;

    bool start(const RpcConfig& c, std::string* error) override {
        int in[2], out[2], err[2];
        if (pipe(in) || pipe(out) || pipe(err)) { *error = "pipe failed"; return false; }
        pid_ = fork();
        if (pid_ < 0) { *error = "fork failed"; return false; }
        if (pid_ == 0) {
            dup2(in[0], 0); dup2(out[1], 1); dup2(err[1], 2);
            for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) close(fd);
            if (!c.working_dir.empty() && chdir(c.working_dir.c_str()) != 0) _exit(126);
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(c.python.c_str()));
            for (const auto& a : c.args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            execv(c.python.c_str(), argv.data());
            _exit(127);
        }
        close(in[0]); close(out[1]); close(err[1]);
        in_ = in[1]; out_ = out[0]; err_ = err[0];
        exited_ = false;
        return true;
    }
    bool running() const override { return pid_ > 0 && !exited_; }
    void write(const std::string& b) override {
        if (in_ >= 0) { auto n = ::write(in_, b.data(), b.size()); (void)n; }
    }
    void closeStdin() override { if (in_ >= 0) { close(in_); in_ = -1; } }
    void kill() override { if (pid_ > 0) ::kill(pid_, SIGKILL); }
    bool waitForExit(int ms) override {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            drain(20);
            if (reapNoEvent()) return true;
            if (std::chrono::steady_clock::now() > end) return false;
        }
    }
    void detach() override {
        for (int* fd : {&in_, &out_, &err_}) if (*fd >= 0) { close(*fd); *fd = -1; }
        pid_ = 0; exited_ = true; pending_exit_ = false;
    }
    void armTimer(int ms) override { deadline_ = now() + ms; armed_ = true; }
    void cancelTimer() override { armed_ = false; }
    void defer(std::function<void()> fn) override { deferred_.push_back(std::move(fn)); }
    double nowMs() const override { return now(); }

    // One turn of the event loop: read what is readable, notice exit, fire the timer and the deferred calls.
    void pump(int ms) {
        drain(ms);
        if (reapNoEvent() || pending_exit_) {
            if (pending_exit_) { pending_exit_ = false; session->onExited(exit_code_, crashed_); }
        }
        if (armed_ && now() >= deadline_) { armed_ = false; session->onTimer(); }
        auto d = std::move(deferred_); deferred_.clear();
        for (auto& f : d) f();
    }

private:
    static double now() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void drain(int ms) {
        pollfd fds[2] = {{out_, POLLIN, 0}, {err_, POLLIN, 0}};
        int n = (out_ >= 0 ? 1 : 0) + (err_ >= 0 ? 1 : 0);
        if (!n) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); return; }
        if (poll(fds, 2, ms) <= 0) return;
        char buf[65536];
        if (out_ >= 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
            const ssize_t r = read(out_, buf, sizeof buf);
            if (r > 0) session->onStdout(std::string_view(buf, static_cast<std::size_t>(r)));
            else { close(out_); out_ = -1; }
        }
        if (err_ >= 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
            const ssize_t r = read(err_, buf, sizeof buf);
            if (r > 0) session->onStderr(std::string_view(buf, static_cast<std::size_t>(r)));
            else { close(err_); err_ = -1; }
        }
    }
    // Reap the child without telling the session (pump() does that once). True when it has exited.
    bool reapNoEvent() {
        if (pid_ <= 0) return true;
        if (exited_) return true;
        int st = 0;
        if (waitpid(pid_, &st, WNOHANG) == pid_) {
            exited_ = true;
            crashed_ = WIFSIGNALED(st);
            exit_code_ = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            pending_exit_ = true;
            return true;
        }
        return false;
    }

    pid_t pid_ = 0;
    int in_ = -1, out_ = -1, err_ = -1;
    bool exited_ = true, pending_exit_ = false, crashed_ = false;
    int exit_code_ = 0;
    double deadline_ = 0;
    bool armed_ = false;
    std::deque<std::function<void()>> dq_;
    std::vector<std::function<void()>> deferred_;
};

struct Live {
    PosixTransport t;
    std::unique_ptr<RpcSession> s;
    RpcConfig cfg;
    Live() {
        cfg.python = g_python;
        cfg.working_dir = g_root;
        cfg.default_timeout_ms = 60000;
        s = std::make_unique<RpcSession>(cfg, t);
        t.session = s.get();
    }
    bool waitFor(const ReplyPtr& r, int ms = 60000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!r->isFinished() && std::chrono::steady_clock::now() < end) t.pump(20);
        return r->isFinished();
    }
};

}  // namespace

TEST(real_backend_handshake_methods_and_error_mapping) {
    Live l;
    auto methods = l.s->call("system.methods");
    CHECK(l.waitFor(methods));
    CHECK(methods->ok());
    CHECK(l.s->state() == RpcSession::State::Ready);
    CHECK(l.s->backendPid() > 0);
    CHECK(!l.s->scratchDir().empty());
    CHECK(fs::path(l.s->scratchDir()).filename().string().starts_with("tcad_backend_"));
    const auto names = methods->result();
    CHECK(names.is_array() && names.size() > 20);
    CHECK(std::find(names.begin(), names.end(), "examples.list") != names.end());
    auto bad = l.s->call("nope.nothing");
    CHECK(l.waitFor(bad));
    CHECK(!bad->ok());
    CHECK_EQ(bad->errorCode(), -32601);                    // JSON-RPC "method not found", mapped through unchanged
    CHECK(l.s->state() == RpcSession::State::Ready);
}

TEST(real_backend_a_call_with_params_and_non_ascii_text_round_trips) {
    Live l;
    auto ex = l.s->call("examples.list");
    CHECK(l.waitFor(ex));
    CHECK(ex->ok() && ex->result().is_array() && !ex->result().empty());
    // catalog.describe with a bogus key: the server must have parsed our ASCII-escaped params and named the key
    auto d = l.s->call("catalog.describe", json::object({{"key", "caf\xC3\xA9-\xE2\x82\xAC"}}));
    CHECK(l.waitFor(d));
    CHECK(!d->ok());                                       // unknown model: an error, not a protocol failure
    CHECK(d->errorCode() != Reply::kProtocolError);
    CHECK(l.s->state() == RpcSession::State::Ready);
}

TEST(real_backend_shutdown_stops_the_process_and_removes_the_scratch_dir) {
    Live l;
    auto p = l.s->call("system.ping");
    CHECK(l.waitFor(p));
    const std::string scratch = l.s->scratchDir();
    CHECK(fs::exists(scratch));
    l.s->shutdown();
    CHECK(!l.t.running());
    CHECK(!fs::exists(scratch));
    CHECK(l.s->state() == RpcSession::State::NotStarted);
    // and it starts again on demand, with a fresh scratch directory
    auto again = l.s->call("system.ping");
    CHECK(l.waitFor(again));
    CHECK(again->ok());
    CHECK(l.s->scratchDir() != scratch);
    l.s->shutdown();
}

TEST(real_backend_a_timeout_replaces_the_service_and_the_queue_goes_on) {
    Live l;
    l.cfg.default_timeout_ms = 60000;
    auto warm = l.s->call("system.ping");
    CHECK(l.waitFor(warm));
    const auto pid1 = l.s->backendPid();
    auto stuck = l.s->call("system.warmup", nullptr, 1);    // 1 ms: imports take far longer -> times out
    auto next = l.s->call("system.ping");
    CHECK(l.waitFor(stuck));
    CHECK_EQ(stuck->errorCode(), Reply::kTimedOut);
    CHECK(l.waitFor(next));
    CHECK(next->ok());                                     // served by a fresh process
    CHECK(l.s->backendPid() != pid1);
    CHECK_EQ(l.s->restarts(), 1);
    l.s->shutdown();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <python3> <pytcad-dir>\n", argv[0]);
        return 2;
    }
    g_python = argv[1];
    g_root = argv[2];
    return minitest::runAll(argc > 3 ? argc - 2 : 1, argv + 2);
}

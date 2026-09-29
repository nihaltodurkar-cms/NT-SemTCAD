// The client side of the Python backend's JSON-RPC 2.0 stdio protocol (backend_service/, NATIVE-DESKTOP-PLAN.md
// 15.15) -- the Qt-free port of backend_client.cpp (N1). ALL protocol behaviour lives here, behind an abstract
// RpcTransport (a child process, a one-shot timer, a deferral to the UI thread), so it is unit-tested on any
// platform against a scripted fake backend. The Win32 transport is native/backend_client.*.
//
// Contract (each part exists for a recorded reason, see backend_client.hpp's header):
//   - Lazy start; a handshake (system.ping, then system.info) checks the protocol version and learns the pid and
//     scratch directory before any caller's request is written.
//   - ONE request in flight, FIFO: the service answers strictly in order; each call's timer starts when written.
//   - Requests are ASCII-escaped JSON; a trailing '\r' is stripped from every reply line.
//   - A timeout fails that call, kills the service and goes on in a fresh process; an unexpected exit fails the
//     in-flight call with the exit description and stderr tail; three unexpected exits within 30 s: "keeps
//     crashing" until resetBackoff().
//   - Replies never finish inside call(): they finish later, through the transport's defer().
//   - shutdown(): system.shutdown, a bounded wait, then kill; a scratch directory the service left is removed.
//
// Threading: single-threaded. Every method, and every transport -> session event, runs on the UI thread. A
// transport must not call onExited() re-entrantly from kill()/write().
#pragma once

#include "platform/env_path.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tcad::platform {

class RpcSession;

class Reply {
public:
    const std::string& method() const { return method_; }
    bool isFinished() const { return finished_; }
    bool ok() const { return finished_ && !error_code_; }
    const nlohmann::json& result() const { return result_; }
    int errorCode() const { return error_code_; }  // JSON-RPC code, or one of the client codes below
    const std::string& errorMessage() const { return error_message_; }
    // A JSON-RPC error's "data" object (e.g. {"type","title","detail"}); null for client-side failures.
    const nlohmann::json& errorData() const { return error_data_; }
    double elapsedMs() const { return elapsed_ms_; }  // enqueue -> answer

    // Run `fn` when the reply finishes (exactly once); immediately if it already has.
    void then(std::function<void(Reply&)> fn);

    static constexpr int kTimedOut = -33001;
    static constexpr int kBackendExited = -33002;
    static constexpr int kProtocolError = -33003;
    static constexpr int kCannotStart = -33004;
    static constexpr int kGaveUp = -33005;
    static constexpr int kShutDown = -33006;

private:
    friend class RpcSession;
    Reply(std::string method, double t0_ms) : method_(std::move(method)), t0_ms_(t0_ms) {}
    void succeed(nlohmann::json result, double now_ms);
    void fail(int code, std::string message, double now_ms);
    void fire();

    std::string method_;
    bool finished_ = false;
    nlohmann::json result_;
    int error_code_ = 0;
    std::string error_message_;
    nlohmann::json error_data_;
    double elapsed_ms_ = 0;
    double t0_ms_ = 0;
    std::function<void(Reply&)> then_;
};
using ReplyPtr = std::shared_ptr<Reply>;

// What the session needs from the outside world.
class RpcTransport {
public:
    virtual ~RpcTransport() = default;
    // Spawn the child with pipes. False + `error`: it could not start. Later events go to the session:
    // onStdout / onStderr / onExited.
    virtual bool start(const RpcConfig& config, std::string* error) = 0;
    virtual bool running() const = 0;
    virtual void write(const std::string& bytes) = 0;
    virtual void closeStdin() = 0;
    virtual void kill() = 0;                   // asynchronous: onExited follows
    virtual bool waitForExit(int timeout_ms) = 0;  // blocking; true if the child has exited
    virtual void detach() = 0;                 // drop this child's pending events (used by shutdown())
    virtual void armTimer(int ms) = 0;         // single shot; a new arm replaces the old; fires session.onTimer()
    virtual void cancelTimer() = 0;
    virtual void defer(std::function<void()> fn) = 0;  // run later on the UI thread, in order
    virtual double nowMs() const = 0;          // monotonic
};

class RpcSession {
public:
    enum class State { NotStarted, Starting, Ready, GaveUp };

    RpcSession(RpcConfig config, RpcTransport& transport);
    ~RpcSession();
    RpcSession(const RpcSession&) = delete;
    RpcSession& operator=(const RpcSession&) = delete;

    // Queue a call. timeout_ms <= 0 uses the default.
    ReplyPtr call(std::string method, nlohmann::json params = nullptr, int timeout_ms = 0);

    State state() const { return state_; }
    std::int64_t backendPid() const { return backend_pid_; }
    const std::string& scratchDir() const { return scratch_dir_; }
    const std::string& backendPrefix() const { return backend_prefix_; }
    const std::vector<std::string>& stderrTail() const { return stderr_tail_; }
    int restarts() const { return restarts_; }
    void resetBackoff();  // after "keeps crashing": allow starting again
    void shutdown();      // system.shutdown, wait, kill; remove a scratch directory left behind

    std::function<void(State)> on_state_changed;

    // Transport -> session events.
    void onStdout(std::string_view bytes);
    void onStderr(std::string_view bytes);
    void onExited(int exit_code, bool crashed);
    void onTimer();

private:
    struct Pending {
        int id = 0;
        std::string method;
        nlohmann::json params;
        int timeout_ms = 0;
        ReplyPtr reply;  // null for the internal handshake
        bool handshake = false;
    };

    void ensureStarted();
    void startProcess();
    void pump();
    void handleLine(const std::string& line);
    void completeInflight(const nlohmann::json& response);
    void failInflight(int code, const std::string& message);
    void failAll(int code, const std::string& message);
    void killProcess();
    void setState(State s);
    void removeScratch();
    void deferSafe(std::function<void()> fn);
    std::string exitDescription(int exit_code, bool crashed) const;

    RpcConfig config_;
    RpcTransport& transport_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    State state_ = State::NotStarted;
    bool process_active_ = false;
    std::deque<Pending> queue_;
    std::optional<Pending> inflight_;
    std::string buffer_;
    std::vector<std::string> stderr_tail_;
    int next_id_ = 1;
    std::int64_t backend_pid_ = 0;
    std::string scratch_dir_, backend_prefix_;
    bool handshake_done_ = false;
    bool killing_ = false;
    bool shutting_down_ = false;
    std::vector<std::string> scratch_dirs_;  // every one a service reported: kept until shutdown()
    std::string last_failure_;
    std::vector<double> crash_times_ms_;
    int restarts_ = 0;
};

}  // namespace tcad::platform

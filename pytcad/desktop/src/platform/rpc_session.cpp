#include "platform/rpc_session.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace tcad::platform {
namespace {

constexpr int kProtocolVersion = 1;                       // backend_service.server.PROTOCOL_VERSION
constexpr std::size_t kMaxLineBytes = 256ull * 1024 * 1024;  // a longer reply line is a protocol error
constexpr std::size_t kStderrLines = 200;
constexpr int kCrashLimit = 3;
constexpr double kCrashWindowMs = 30000;

std::string left(const std::string& s, std::size_t n) { return s.substr(0, n); }

}  // namespace

// -- Reply --------------------------------------------------------------------

void Reply::then(std::function<void(Reply&)> fn) {
    then_ = std::move(fn);
    if (finished_) fire();
}

void Reply::fire() {
    if (auto f = std::move(then_)) {
        then_ = nullptr;
        f(*this);
    }
}

void Reply::succeed(nlohmann::json result, double now_ms) {
    if (finished_) return;
    finished_ = true;
    result_ = std::move(result);
    elapsed_ms_ = now_ms - t0_ms_;
    fire();
}

void Reply::fail(int code, std::string message, double now_ms) {
    if (finished_) return;
    finished_ = true;
    error_code_ = code ? code : kProtocolError;
    error_message_ = std::move(message);
    elapsed_ms_ = now_ms - t0_ms_;
    fire();
}

// -- RpcSession ---------------------------------------------------------------

RpcSession::RpcSession(RpcConfig config, RpcTransport& transport) : config_(std::move(config)), transport_(transport) {}

RpcSession::~RpcSession() {
    shutdown();
    *alive_ = false;
}

void RpcSession::deferSafe(std::function<void()> fn) {
    std::weak_ptr<bool> alive = alive_;
    transport_.defer([alive, fn = std::move(fn)] {
        if (auto a = alive.lock(); a && *a) fn();
    });
}

void RpcSession::setState(State s) {
    if (s == state_) return;
    state_ = s;
    if (on_state_changed) on_state_changed(s);
}

ReplyPtr RpcSession::call(std::string method, nlohmann::json params, int timeout_ms) {
    auto reply = std::shared_ptr<Reply>(new Reply(method, transport_.nowMs()));
    if (state_ == State::GaveUp) {
        // Asynchronous, like every other outcome: the caller attaches then() after call() returns.
        const std::string why = last_failure_.empty() ? "it kept crashing or could not start" : last_failure_;
        deferSafe([this, reply, why] { reply->fail(Reply::kGaveUp, "the backend is not running: " + why, transport_.nowMs()); });
        return reply;
    }
    Pending p;
    p.id = next_id_++;
    p.method = std::move(method);
    p.params = std::move(params);
    p.timeout_ms = timeout_ms > 0 ? timeout_ms : config_.default_timeout_ms;
    p.reply = reply;
    queue_.push_back(std::move(p));
    ensureStarted();
    pump();
    return reply;
}

void RpcSession::ensureStarted() {
    if (state_ == State::NotStarted && !process_active_) startProcess();
}

void RpcSession::startProcess() {
    auto cannot_start = [this](const std::string& why) {
        last_failure_ = why;  // a call() before the deferred failAll must see it
        setState(State::GaveUp);
        deferSafe([this, why] { failAll(Reply::kCannotStart, why); });
    };
    if (config_.python.empty()) {
        cannot_start("no backend interpreter configured (TCAD_BACKEND_PYTHON, the settings key backend/python, or "
                     "backend_python in desktop_runtime.json)");
        return;
    }
    std::error_code ec;
    if (!fs::exists(config_.python, ec)) {
        cannot_start("the backend interpreter does not exist: " + config_.python);
        return;
    }
    // The handshake goes first: nothing of the caller's is written before it.
    queue_.push_front(Pending{next_id_++, "system.info", nullptr, config_.default_timeout_ms, nullptr, true});
    queue_.push_front(Pending{next_id_++, "system.ping", nullptr, config_.default_timeout_ms, nullptr, true});
    handshake_done_ = false;
    buffer_.clear();
    setState(State::Starting);
    std::string error;
    if (!transport_.start(config_, &error)) {
        process_active_ = false;
        const std::string why = "could not start the backend '" + config_.python + "': " + error;
        last_failure_ = why;
        setState(State::GaveUp);
        deferSafe([this, why] { failAll(Reply::kCannotStart, why); });
        return;
    }
    process_active_ = true;
    pump();
}

void RpcSession::pump() {
    // killing_: kill() is asynchronous and the dying process still reads as running -- a request written now would
    // die with it. The queue resumes in the replacement process.
    if (inflight_ || queue_.empty() || !process_active_ || killing_ || !transport_.running()) return;
    if (!handshake_done_ && !queue_.front().handshake) return;
    inflight_ = std::move(queue_.front());
    queue_.pop_front();
    nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", inflight_->id}, {"method", inflight_->method}};
    if (!inflight_->params.is_null()) req["params"] = inflight_->params;
    // ensure_ascii: every non-ASCII character travels as a \u escape (a piped stdin on Windows is not UTF-8)
    transport_.write(req.dump(-1, ' ', true) + "\n");
    transport_.armTimer(inflight_->timeout_ms);
}

void RpcSession::onStdout(std::string_view bytes) {
    if (!process_active_) return;
    buffer_.append(bytes);
    std::size_t nl;
    while ((nl = buffer_.find('\n')) != std::string::npos) {
        std::string line = buffer_.substr(0, nl);
        buffer_.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();  // Python's text-mode stdout on Windows
        if (line.find_first_not_of(" \t") != std::string::npos) handleLine(line);
        if (!process_active_) return;  // handleLine ended the process
    }
    if (buffer_.size() > kMaxLineBytes) {
        failInflight(Reply::kProtocolError, "a reply line from the backend exceeded 256 MB");
        killProcess();
    }
}

void RpcSession::handleLine(const std::string& line) {
    nlohmann::json j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        failInflight(Reply::kProtocolError, "malformed reply from the backend: " + left(line, 200));
        killProcess();  // the stream is out of step: start clean
        return;
    }
    if (!inflight_) {
        stderr_tail_.push_back("[client] unsolicited reply ignored: " + left(line, 200));
        return;
    }
    if (!j.is_object() || !j.contains("id") || j["id"] != inflight_->id) {
        failInflight(Reply::kProtocolError,
                     "reply id does not match request " + std::to_string(inflight_->id) + ": " + left(line, 200));
        killProcess();
        return;
    }
    completeInflight(j);
}

void RpcSession::completeInflight(const nlohmann::json& response) {
    transport_.cancelTimer();
    Pending p = std::move(*inflight_);
    inflight_.reset();
    const bool is_error = response.contains("error");
    const double now = transport_.nowMs();
    if (p.handshake) {
        if (is_error) {
            failAll(Reply::kProtocolError, "backend handshake (" + p.method + ") failed: " + response["error"].dump());
            setState(State::GaveUp);
            killProcess();
            return;
        }
        const auto& r = response["result"];
        if (p.method == "system.ping") {
            const int proto = r.is_object() && r.contains("protocol") && r["protocol"].is_number_integer() ? r["protocol"].get<int>() : -1;
            if (proto != kProtocolVersion) {
                failAll(Reply::kProtocolError, "the backend speaks protocol " + std::to_string(proto) + "; this app speaks " + std::to_string(kProtocolVersion));
                setState(State::GaveUp);
                killProcess();
                return;
            }
        } else {  // system.info
            backend_pid_ = r.value("pid", std::int64_t{0});
            scratch_dir_ = r.value("scratch_dir", std::string());
            backend_prefix_ = r.value("prefix", std::string());
            if (!scratch_dir_.empty() && std::find(scratch_dirs_.begin(), scratch_dirs_.end(), scratch_dir_) == scratch_dirs_.end())
                scratch_dirs_.push_back(scratch_dir_);
            handshake_done_ = true;
            setState(State::Ready);
        }
    } else if (p.reply) {
        if (is_error) {
            const auto& e = response["error"];
            std::string msg = e.value("message", std::string("backend error"));
            if (e.contains("data") && e["data"].is_object()) {
                p.reply->error_data_ = e["data"];
                if (e["data"].contains("type") && e["data"]["type"].is_string()) msg = e["data"]["type"].get<std::string>() + ": " + msg;
            }
            p.reply->fail(e.value("code", Reply::kProtocolError), msg, now);
        } else {
            p.reply->succeed(response.value("result", nlohmann::json()), now);
        }
    }
    pump();
}

void RpcSession::onTimer() {
    if (!inflight_) return;
    const std::string what = inflight_->method + " timed out after " + std::to_string(inflight_->timeout_ms) + " ms";
    if (inflight_->handshake) {
        failAll(Reply::kTimedOut, "backend handshake: " + what);
        setState(State::GaveUp);
    } else {
        failInflight(Reply::kTimedOut, what);
    }
    killProcess();  // the service is sequential and stuck: replace it
}

void RpcSession::onStderr(std::string_view bytes) {
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) stderr_tail_.push_back(cur);
        cur.clear();
    };
    for (char c : bytes) {
        if (c == '\n') flush();
        else if (c != '\r') cur.push_back(c);
    }
    flush();
    while (stderr_tail_.size() > kStderrLines) stderr_tail_.erase(stderr_tail_.begin());
}

std::string RpcSession::exitDescription(int exit_code, bool crashed) const {
    std::string why = crashed ? "crashed" : "exited with code " + std::to_string(exit_code);
    if (!stderr_tail_.empty()) {
        why += "; stderr: ";
        const std::size_t from = stderr_tail_.size() > 5 ? stderr_tail_.size() - 5 : 0;
        for (std::size_t i = from; i < stderr_tail_.size(); ++i) why += (i > from ? " | " : "") + stderr_tail_[i];
    }
    return why;
}

void RpcSession::onExited(int exit_code, bool crashed) {
    if (!process_active_) return;
    const bool expected = killing_ || shutting_down_;
    killing_ = false;
    const std::string why = exitDescription(exit_code, crashed);
    if (inflight_) {
        if (shutting_down_) failInflight(Reply::kShutDown, "the backend was shut down");
        else failInflight(Reply::kBackendExited, "the backend " + why + " during " + inflight_->method);
    }
    // handshake entries belong to the dead process
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Pending& p) { return p.handshake; }), queue_.end());
    process_active_ = false;
    handshake_done_ = false;
    buffer_.clear();
    if (shutting_down_ || state_ == State::GaveUp) return;
    if (!expected) {
        const double now = transport_.nowMs();
        crash_times_ms_.push_back(now);
        std::erase_if(crash_times_ms_, [now](double t) { return now - t > kCrashWindowMs; });
        if (static_cast<int>(crash_times_ms_.size()) >= kCrashLimit) {
            setState(State::GaveUp);
            failAll(Reply::kGaveUp, "the backend keeps crashing (" + std::to_string(kCrashLimit) + " exits in " +
                                        std::to_string(static_cast<int>(kCrashWindowMs / 1000)) + " s); last: " + why);
            return;
        }
    }
    setState(State::NotStarted);
    if (!queue_.empty()) {
        ++restarts_;
        startProcess();
    }
}

void RpcSession::failInflight(int code, const std::string& message) {
    transport_.cancelTimer();
    if (!inflight_) return;
    Pending p = std::move(*inflight_);
    inflight_.reset();
    if (p.reply) p.reply->fail(code, message, transport_.nowMs());
}

void RpcSession::failAll(int code, const std::string& message) {
    last_failure_ = message;  // what a later call() in the gave-up state reports
    failInflight(code, message);
    std::deque<Pending> queued;
    queued.swap(queue_);
    for (Pending& p : queued)
        if (p.reply) p.reply->fail(code, message, transport_.nowMs());
}

void RpcSession::killProcess() {
    if (process_active_ && transport_.running()) {
        killing_ = true;
        transport_.kill();
    }
}

void RpcSession::resetBackoff() {
    crash_times_ms_.clear();
    if (state_ == State::GaveUp && !process_active_) setState(State::NotStarted);
}

void RpcSession::removeScratch() {
    // Only directories the service itself reported, and only if they look like its own (tcad_backend_* directly
    // under the temp directory).
    std::error_code ec;
    const std::string temp = normalizedDir(fs::temp_directory_path(ec).string());
    for (const std::string& d : scratch_dirs_) {
        const fs::path p(d);
        if (p.filename().string().starts_with("tcad_backend_") && normalizedDir(p.parent_path().string()) == temp && fs::is_directory(p, ec))
            fs::remove_all(p, ec);
    }
    scratch_dirs_.clear();
}

void RpcSession::shutdown() {
    shutting_down_ = true;
    transport_.cancelTimer();
    if (process_active_) {
        if (transport_.running()) {
            transport_.write(nlohmann::json({{"jsonrpc", "2.0"}, {"id", next_id_++}, {"method", "system.shutdown"}}).dump() + "\n");
            transport_.closeStdin();
            if (!transport_.waitForExit(config_.shutdown_wait_ms)) {
                transport_.kill();
                transport_.waitForExit(1000);
            }
        }
        transport_.detach();  // no late event of this child may reach a later one
        process_active_ = false;
        killing_ = false;
    }
    failAll(Reply::kShutDown, "the backend was shut down");
    removeScratch();
    handshake_done_ = false;
    buffer_.clear();
    shutting_down_ = false;
    if (state_ != State::GaveUp) setState(State::NotStarted);
}

}  // namespace tcad::platform

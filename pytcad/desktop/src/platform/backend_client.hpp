// The Win32 backend client (N1): RpcSession (platform-neutral protocol) + a transport made of Process
// (CreateProcess + pipe threads), Application timers and Application::post. The Qt-free replacement for
// backend/backend_client.{hpp,cpp}; the public shape is the same (lazy start, handshake, FIFO, timeouts,
// restart/backoff, shutdown) and its behaviour is the RpcSession's, which is tested off Windows against a fake
// and against the real server.
#pragma once

#include "platform/rpc_session.hpp"
#include "platform/settings.hpp"

#include <memory>

namespace tcad::platform {

class BackendClient {
public:
    explicit BackendClient(RpcConfig config);
    ~BackendClient();
    BackendClient(const BackendClient&) = delete;
    BackendClient& operator=(const BackendClient&) = delete;

    // Where the backend lives: desktop_runtime.json next to the executable, then the settings key backend/python
    // (or `python_override` when non-empty, e.g. --python), then TCAD_BACKEND_PYTHON / TCAD_BACKEND_ROOT.
    static RpcConfig configFromEnvironment(const Settings* settings, const std::string& python_override = {});

    ReplyPtr call(std::string method, nlohmann::json params = nullptr, int timeout_ms = 0);
    RpcSession& session() { return *session_; }
    void shutdown() { session_->shutdown(); }

private:
    class Transport;
    std::unique_ptr<Transport> transport_;  // declared first: destroyed after the session that uses it
    std::unique_ptr<RpcSession> session_;
};

}  // namespace tcad::platform

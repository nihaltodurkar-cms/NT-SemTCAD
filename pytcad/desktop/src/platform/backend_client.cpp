#include "platform/backend_client.hpp"

#include "platform/app.hpp"
#include "platform/process.hpp"
#include "platform/win32_util.hpp"

#include <fstream>
#include <sstream>

namespace tcad::platform {

class BackendClient::Transport final : public RpcTransport {
public:
    RpcSession* session = nullptr;

    ~Transport() override {
        if (timer_) Application::instance().stopTimer(timer_);
        if (process_) process_->detach();
    }

    bool start(const RpcConfig& c, std::string* error) override {
        retire();  // a previous child (already exited or killed): parked, never destroyed inside its own callback
        auto p = std::make_unique<Process>();
        ProcessOptions o;
        o.exe = widen(c.python);
        for (const auto& a : c.args) o.args.push_back(widen(a));
        o.cwd = widen(c.working_dir);
        const std::string path = pythonPathEnvironment(getEnv("PATH").value_or(""), c.python, c.strip_from_path, ';', true);
        o.env.emplace_back(L"PATH", widen(path));
        if (c.debug) o.env.emplace_back(L"TCAD_BACKEND_DEBUG", std::wstring(L"1"));
        else o.env.emplace_back(L"TCAD_BACKEND_DEBUG", std::nullopt);
        p->on_stdout = [this](std::string_view b) { if (session) session->onStdout(b); };
        p->on_stderr = [this](std::string_view b) { if (session) session->onStderr(b); };
        p->on_exit = [this](int code, bool crashed) { if (session) session->onExited(code, crashed); };
        auto r = p->start(o);
        if (!r) {
            *error = r.error();
            return false;
        }
        process_ = std::move(p);
        return true;
    }
    bool running() const override { return process_ && process_->running(); }
    void write(const std::string& b) override { if (process_) process_->write(b); }
    void closeStdin() override { if (process_) process_->closeStdin(); }
    void kill() override { if (process_) process_->kill(); }
    bool waitForExit(int ms) override { return !process_ || process_->waitForExit(ms); }
    void detach() override { if (process_) { process_->detach(); retire(); } }
    void armTimer(int ms) override {
        cancelTimer();
        timer_ = Application::instance().startTimer(ms, false, [this] {
            timer_ = 0;
            if (session) session->onTimer();
        });
    }
    void cancelTimer() override {
        if (timer_) Application::instance().stopTimer(timer_);
        timer_ = 0;
    }
    void defer(std::function<void()> fn) override { Application::instance().post(std::move(fn)); }
    double nowMs() const override { return Application::instance().nowMs(); }

private:
    // Keep the Process object alive a little longer: this may run inside its own on_exit callback.
    void retire() {
        if (!process_) return;
        auto keep = std::make_shared<std::unique_ptr<Process>>(std::move(process_));
        (*keep)->detach();
        Application::instance().post([keep] {});  // destroyed on a later turn of the loop
    }
    std::unique_ptr<Process> process_;
    Application::TimerId timer_ = 0;
};

BackendClient::BackendClient(RpcConfig config) : transport_(std::make_unique<Transport>()) {
    session_ = std::make_unique<RpcSession>(std::move(config), *transport_);
    transport_->session = session_.get();
}

BackendClient::~BackendClient() {
    session_->shutdown();
    transport_->session = nullptr;
    session_.reset();
}

ReplyPtr BackendClient::call(std::string method, nlohmann::json params, int timeout_ms) {
    return session_->call(std::move(method), std::move(params), timeout_ms);
}

RpcConfig BackendClient::configFromEnvironment(const Settings* settings, const std::string& python_override) {
    const auto app_dir = executableDir();
    std::string manifest;
    {
        std::ifstream in(app_dir / L"desktop_runtime.json", std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            manifest = ss.str();
        }
    }
    std::string settings_python = settings ? settings->value("backend/python") : std::string();
    if (!python_override.empty()) settings_python = python_override;
    return resolveBackendConfig(app_dir.string(), manifest, settings_python, [](const char* k) { return getEnv(k); });
}

}  // namespace tcad::platform

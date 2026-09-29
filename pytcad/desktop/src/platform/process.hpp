// A child process with three pipes (N1; replaces QProcess for the backend and, later, solver jobs).
//   - CreateProcessW with an explicit inheritable-handle list (only the three pipe ends are inherited),
//     CREATE_NO_WINDOW, a modified copy of the environment, and a job object so the child dies with the app;
//   - one reader thread per output pipe, one writer thread, one waiter thread; each hands its data to the UI
//     thread through Application::post, so every callback runs on the UI thread, in order;
//   - on_exit is delivered after the output pipes drained (bounded wait: a grandchild that inherited a pipe
//     must not hold the exit back).
// No Qt: Win32 + std::thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace tcad::platform {

struct ProcessOptions {
    std::wstring exe;                 // full path
    std::vector<std::wstring> args;   // argv[1...]; quoted for CommandLineToArgvW
    std::wstring cwd;                 // empty: inherit
    // Applied over the current environment. nullopt value: remove the variable.
    std::vector<std::pair<std::wstring, std::optional<std::wstring>>> env;
};

class Process {
public:
    Process();
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    std::expected<void, std::string> start(const ProcessOptions& options);
    bool running() const;
    DWORD pid() const { return pid_; }
    void write(const std::string& bytes);  // queued; a writer thread sends it (never blocks the UI thread)
    void closeStdin();                     // after everything queued was sent
    void kill();                           // TerminateProcess; on_exit follows
    bool waitForExit(int timeout_ms);      // blocking
    // Drop this child's pending and future callbacks (it may still run; kill() it first if it should not).
    void detach();

    std::function<void(std::string_view)> on_stdout, on_stderr;
    std::function<void(int exit_code, bool crashed)> on_exit;

private:
    struct Shared;  // state shared with the threads
    std::shared_ptr<Shared> shared_;
    HANDLE process_ = nullptr, job_ = nullptr;
    DWORD pid_ = 0;
    std::vector<std::thread> threads_;
    bool started_ = false;
};

// One argument quoted for a Windows command line (the inverse of CommandLineToArgvW's rules).
std::wstring quoteArgument(const std::wstring& arg);

}  // namespace tcad::platform

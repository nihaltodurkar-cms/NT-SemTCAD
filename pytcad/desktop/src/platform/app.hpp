// The application object and message loop of the native platform layer (NATIVE-DESKTOP-PLAN.md 27.5, N1).
// One per process, on the UI thread. It owns a message-only dispatcher window, through which:
//   - post(fn): any thread queues a function to run on the UI thread (child-process pipe readers, tests);
//   - timers: SetTimer-based, single-shot or repeating, callbacks on the UI thread;
//   - run()/pump(): the loop itself (windows created on this thread are served by it).
// It also initialises COM (single-threaded apartment), which the common file dialogs require.
// No Qt: Win32 only.
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>

namespace tcad::platform {

class Application {
public:
    using TimerId = std::uint64_t;

    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    static Application& instance();  // throws std::logic_error before construction

    // Run the message loop until quit(); returns its exit code.
    int run();
    // Handle every queued message; false once WM_QUIT was seen (then *exit_code is set).
    bool pump(int* exit_code = nullptr);
    // Ask the loop to end. Safe from any thread.
    void quit(int exit_code = 0);

    // Run `fn` on the UI thread, later, in the order posted. Safe from any thread.
    void post(std::function<void()> fn);
    bool onUiThread() const { return GetCurrentThreadId() == ui_thread_; }

    // UI thread only. The callback runs on the UI thread; a single-shot timer is gone before its callback runs.
    TimerId startTimer(int interval_ms, bool repeat, std::function<void()> fn);
    void stopTimer(TimerId id);

    double nowMs() const;  // monotonic, ms
    HWND dispatcherWindow() const { return hwnd_; }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void drainPosted();
    void onTimer(TimerId id);

    struct TimerEntry {
        bool repeat;
        std::function<void()> fn;
    };
    HWND hwnd_ = nullptr;
    DWORD ui_thread_ = 0;
    bool com_initialised_ = false;
    std::mutex mutex_;
    std::deque<std::function<void()>> posted_;
    bool wake_pending_ = false;
    std::map<TimerId, TimerEntry> timers_;
    TimerId next_timer_ = 1;
};

}  // namespace tcad::platform

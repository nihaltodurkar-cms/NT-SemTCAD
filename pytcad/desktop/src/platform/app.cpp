#include "platform/app.hpp"

#include <objbase.h>

#include <chrono>
#include <stdexcept>
#include <vector>

namespace tcad::platform {
namespace {

constexpr UINT kWmPosted = WM_APP + 1;
constexpr const wchar_t* kDispatcherClass = L"TcadDispatcher";
Application* g_instance = nullptr;

}  // namespace

Application& Application::instance() {
    if (!g_instance) throw std::logic_error("no tcad::platform::Application exists");
    return *g_instance;
}

Application::Application() {
    if (g_instance) throw std::logic_error("only one tcad::platform::Application per process");
    ui_thread_ = GetCurrentThreadId();
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    com_initialised_ = SUCCEEDED(hr);  // S_FALSE: already initialised on this thread, still balanced by CoUninitialize
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Application::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kDispatcherClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(0, kDispatcherClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (!hwnd_) throw std::runtime_error("cannot create the dispatcher window (Win32 error " + std::to_string(GetLastError()) + ")");
    g_instance = this;
}

Application::~Application() {
    g_instance = nullptr;
    if (hwnd_) DestroyWindow(hwnd_);
    if (com_initialised_) CoUninitialize();
}

LRESULT CALLBACK Application::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(h, msg, wp, lp);
    }
    auto* self = reinterpret_cast<Application*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) {
        if (msg == kWmPosted) {
            self->drainPosted();
            return 0;
        }
        if (msg == WM_TIMER) {
            self->onTimer(static_cast<TimerId>(wp));
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Application::post(std::function<void()> fn) {
    bool wake = false;
    {
        std::lock_guard lock(mutex_);
        posted_.push_back(std::move(fn));
        wake = !wake_pending_;
        wake_pending_ = true;
    }
    if (wake) PostMessageW(hwnd_, kWmPosted, 0, 0);
}

void Application::drainPosted() {
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard lock(mutex_);
        batch.swap(posted_);
        wake_pending_ = false;
    }
    for (auto& f : batch) f();
}

Application::TimerId Application::startTimer(int interval_ms, bool repeat, std::function<void()> fn) {
    const TimerId id = next_timer_++;
    timers_[id] = TimerEntry{repeat, std::move(fn)};
    SetTimer(hwnd_, static_cast<UINT_PTR>(id), static_cast<UINT>(interval_ms < USER_TIMER_MINIMUM ? USER_TIMER_MINIMUM : interval_ms), nullptr);
    return id;
}

void Application::stopTimer(TimerId id) {
    if (timers_.erase(id)) KillTimer(hwnd_, static_cast<UINT_PTR>(id));
}

void Application::onTimer(TimerId id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    std::function<void()> fn = it->second.fn;
    if (!it->second.repeat) {
        KillTimer(hwnd_, static_cast<UINT_PTR>(id));
        timers_.erase(it);
    }
    if (fn) fn();
}

void Application::quit(int exit_code) {
    if (onUiThread()) PostQuitMessage(exit_code);
    else post([exit_code] { PostQuitMessage(exit_code); });
}

bool Application::pump(int* exit_code) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            if (exit_code) *exit_code = static_cast<int>(msg.wParam);
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

int Application::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

double Application::nowMs() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace tcad::platform

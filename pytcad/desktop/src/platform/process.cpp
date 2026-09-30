#include "platform/process.hpp"

#include "platform/app.hpp"
#include "platform/win32_util.hpp"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace tcad::platform {

std::wstring quoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');  // trailing backslashes precede the closing quote: double them
            break;
        }
        if (arg[i] == L'"') out.append(backslashes * 2 + 1, L'\\');
        else out.append(backslashes, L'\\');
        out.push_back(arg[i]);
    }
    out.push_back(L'"');
    return out;
}

struct Process::Shared {
    std::atomic<bool> live{true};  // false after detach(): posted callbacks are dropped
    Process* owner = nullptr;
    // writer
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> out_queue;
    bool close_stdin = false, stop_writer = false;
    HANDLE stdin_w = nullptr, stdout_r = nullptr, stderr_r = nullptr;
};

namespace {

std::wstring lowerKey(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

// The current environment with `overrides` applied, as a CreateProcess block (sorted, double-NUL terminated).
std::wstring buildEnvironmentBlock(const std::vector<std::pair<std::wstring, std::optional<std::wstring>>>& overrides) {
    std::vector<std::pair<std::wstring, std::wstring>> vars;  // name, "name=value"
    LPWCH env = GetEnvironmentStringsW();
    for (const wchar_t* p = env; p && *p; p += wcslen(p) + 1) {
        std::wstring entry(p);
        const auto eq = entry.find(L'=', entry.empty() || entry[0] == L'=' ? 1 : 0);  // "=C:=C:\" style entries start with '='
        if (eq == std::wstring::npos) continue;
        vars.emplace_back(lowerKey(entry.substr(0, eq)), entry);
    }
    if (env) FreeEnvironmentStringsW(env);
    for (const auto& [name, value] : overrides) {
        const std::wstring key = lowerKey(name);
        std::erase_if(vars, [&](const auto& v) { return v.first == key; });
        if (value) vars.emplace_back(key, name + L"=" + *value);
    }
    std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::wstring block;
    for (const auto& v : vars) {
        block += v.second;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

}  // namespace

Process::Process() : shared_(std::make_shared<Shared>()) { shared_->owner = this; }

Process::~Process() {
    detach();
    if (started_ && running()) TerminateProcess(process_, 1);
    // Blocked ReadFile / WriteFile calls end when the child dies and its pipe ends close; a grandchild holding a
    // pipe must not hang the destructor, so cancel any synchronous I/O still pending first.
    {
        std::lock_guard lock(shared_->mu);
        shared_->stop_writer = true;
    }
    shared_->cv.notify_all();
    for (auto& t : threads_) CancelSynchronousIo(reinterpret_cast<HANDLE>(t.native_handle()));
    for (auto& t : threads_) if (t.joinable()) t.join();
    for (HANDLE* h : {&shared_->stdin_w, &shared_->stdout_r, &shared_->stderr_r, &process_, &job_})
        if (*h) { CloseHandle(*h); *h = nullptr; }
}

std::expected<void, std::string> Process::start(const ProcessOptions& o) {
    if (started_) return std::unexpected("process already started");
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    auto closeAll = [&] {
        for (HANDLE h : {in_r, in_w, out_r, out_w, err_r, err_w}) if (h) CloseHandle(h);
    };
    if (!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
        closeAll();
        return std::unexpected("CreatePipe failed (Win32 error " + std::to_string(GetLastError()) + ")");
    }
    // our ends must not be inherited
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<char> attr_buf(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size);
    HANDLE inherit[3] = {in_r, out_w, err_w};
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in_r;
    si.StartupInfo.hStdOutput = out_w;
    si.StartupInfo.hStdError = err_w;
    si.lpAttributeList = attrs;

    std::wstring cmd = quoteArgument(o.exe);
    for (const auto& a : o.args) cmd += L" " + quoteArgument(a);
    std::wstring env_block = buildEnvironmentBlock(o.env);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(o.exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                   EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                   env_block.data(), o.cwd.empty() ? nullptr : o.cwd.c_str(), &si.StartupInfo, &pi);
    const DWORD create_error = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(in_r);  // the child has its copies
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (!ok) {
        CloseHandle(in_w);
        CloseHandle(out_r);
        CloseHandle(err_r);
        return std::unexpected("CreateProcess failed (Win32 error " + std::to_string(create_error) + ")");
    }

    // A job object that kills the child when this process ends, however it ends.
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info));
        AssignProcessToJobObject(job_, pi.hProcess);  // failure (an outer job forbids nesting) is not fatal
    }
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    started_ = true;

    auto sh = shared_;
    sh->stdin_w = in_w;
    sh->stdout_r = out_r;
    sh->stderr_r = err_r;
    auto reader = [sh](HANDLE h, bool is_err, std::promise<void> done) {
        std::string buf(64 * 1024, '\0');
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) || n == 0) break;
            std::string chunk(buf.data(), n);
            Application::instance().post([sh, is_err, chunk = std::move(chunk)] {
                if (!sh->live) return;
                auto& cb = is_err ? sh->owner->on_stderr : sh->owner->on_stdout;
                if (cb) cb(chunk);
            });
        }
        done.set_value();
    };
    std::promise<void> out_done, err_done;
    std::future<void> out_future = out_done.get_future(), err_future = err_done.get_future();
    threads_.emplace_back(reader, out_r, false, std::move(out_done));
    threads_.emplace_back(reader, err_r, true, std::move(err_done));
    threads_.emplace_back([sh] {  // writer
        for (;;) {
            std::string data;
            bool close_now = false;
            {
                std::unique_lock lock(sh->mu);
                sh->cv.wait(lock, [&] { return !sh->out_queue.empty() || sh->close_stdin || sh->stop_writer; });
                if (!sh->out_queue.empty()) {
                    data = std::move(sh->out_queue.front());
                    sh->out_queue.pop_front();
                } else if (sh->stop_writer) {
                    break;
                } else {
                    close_now = true;
                }
            }
            if (close_now) break;
            for (std::size_t off = 0; off < data.size();) {
                DWORD n = 0;
                if (!WriteFile(sh->stdin_w, data.data() + off, static_cast<DWORD>(data.size() - off), &n, nullptr)) return;  // the child is gone
                off += n;
            }
        }
        if (sh->stdin_w) { CloseHandle(sh->stdin_w); sh->stdin_w = nullptr; }
    });
    HANDLE hproc = process_;
    threads_.emplace_back([sh, hproc, of = std::move(out_future), ef = std::move(err_future)]() mutable {  // waiter
        WaitForSingleObject(hproc, INFINITE);
        // the output the child wrote before it died is still in the pipes: let the readers finish, but not forever
        of.wait_for(std::chrono::milliseconds(1000));
        ef.wait_for(std::chrono::milliseconds(1000));
        DWORD code = 0;
        GetExitCodeProcess(hproc, &code);
        const bool crashed = (code & 0xF0000000u) == 0xC0000000u;  // an NTSTATUS error: the process crashed
        Application::instance().post([sh, code, crashed] {
            if (!sh->live) return;
            if (sh->owner->on_exit) sh->owner->on_exit(static_cast<int>(code), crashed);
        });
    });
    return {};
}

bool Process::running() const {
    return started_ && process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

void Process::write(const std::string& bytes) {
    {
        std::lock_guard lock(shared_->mu);
        shared_->out_queue.push_back(bytes);
    }
    shared_->cv.notify_all();
}

void Process::closeStdin() {
    {
        std::lock_guard lock(shared_->mu);
        shared_->close_stdin = true;
    }
    shared_->cv.notify_all();
}

void Process::kill() {
    if (started_ && process_) TerminateProcess(process_, 1);
}

bool Process::waitForExit(int timeout_ms) {
    return !started_ || !process_ || WaitForSingleObject(process_, static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
}

void Process::detach() { shared_->live = false; }

}  // namespace tcad::platform

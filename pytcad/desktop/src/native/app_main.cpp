// tcad_native -- the N1 native application (NATIVE-DESKTOP-PLAN.md 27.5): ONE minimal native window that hosts the
// existing FieldScene and talks to the existing Python backend. No Qt anywhere; the window, message loop, input,
// DPI, timers, file dialogs, child-process JSON-RPC connection and settings are all src/platform/.
//
//   tcad_native [result.npz] [--settings <file.json>] [--ephemeral] [--python <interpreter>]
//   tcad_native <result.npz> --selftest <report.json> [--screenshot <out.png>]     (exit 0 only if every check passed)
//
// Keys (the shortcuts registered with the workspace):
//   Ctrl+O open a result   Ctrl+B check the backend   F next field   L log   C contours   M mesh lines
//   R reset view   1..7 iso/+x/-x/+y/-y/+z/-z (3D)   Esc or Ctrl+Q quit
#include "data/npz.hpp"
#include "data/result_model.hpp"
#include "native/app_support.hpp"
#include "native/vtk_win32_host.hpp"
#include "platform/app.hpp"
#include "platform/backend_client.hpp"
#include "platform/dpi.hpp"
#include "platform/file_dialog.hpp"
#include "platform/settings.hpp"
#include "platform/win32_util.hpp"
#include "platform/window.hpp"
#include "platform/workspace.hpp"
#include "views/field/field_scene.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace tcad::platform;
using tcad::desktop::FieldScene;
using tcad::desktop::NpzFile;
using tcad::desktop::ResultModel;
using tcad::desktop::ViewPreset;
namespace fs = std::filesystem;
using json = nlohmann::json;

// Adapts the VTK child window to the workspace's ChildHost.
class VtkChildHost final : public ChildHost {
public:
    explicit VtkChildHost(tcad::native::VtkWin32Host& host) : host_(host) {}
    HWND childWindow() const override { return host_.childWindow(); }
    void resizeTo(const Rect& r) override { host_.resize(r.w, r.h); }

private:
    tcad::native::VtkWin32Host& host_;
};

struct Args {
    std::string result, settings, python, selftest, screenshot;
    bool ephemeral = false;
};

bool parseArgs(int argc, char** argv, Args* a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (s == "--settings") a->settings = val();
        else if (s == "--python") a->python = val();
        else if (s == "--selftest") a->selftest = val();
        else if (s == "--screenshot") a->screenshot = val();
        else if (s == "--ephemeral") a->ephemeral = true;
        else if (!s.starts_with("--") && a->result.empty()) a->result = s;
        else return false;
    }
    if (!a->selftest.empty() && a->result.empty()) return false;  // the self-test needs a result to show
    return true;
}

class NativeApp {
public:
    NativeApp(Application& app, const Args& args) : app_(app), args_(args) {
        // ---- settings
        if (!args.settings.empty()) settings_ = std::make_unique<Settings>(Settings::atFile(args.settings));
        else if (args.ephemeral || !args.selftest.empty()) settings_ = std::make_unique<Settings>(Settings::ephemeral());
        else settings_ = std::make_unique<Settings>(Settings::userDefault(appDataDir()));
        // ---- window, workspace
        WindowOptions wo;
        wo.title = L"PyTCAD";
        auto w = Window::create(wo);
        if (!w) throw std::runtime_error("window: " + w.error());
        window_ = std::move(*w);
        workspace_ = std::make_unique<Workspace>(*window_, settings_.get());
        // ---- the VTK view in the content region
        const WorkspaceLayout l = workspace_->layout();
        host_ = std::make_unique<tcad::native::VtkWin32Host>(window_->hwnd(), std::max(1, l.content.w), std::max(1, l.content.h));
        scene_ = std::make_unique<FieldScene>();
        scene_->attach(host_->renderWindow(), host_.get());
        host_->bind(scene_.get());
        child_host_ = std::make_unique<VtkChildHost>(*host_);
        workspace_->setContent(child_host_.get());
        scene_->on_readout_changed = [this](const std::string& text) { workspace_->setStatus(text.empty() ? base_status_ : text); };
        host_->on_key_mods = [this](const std::string& sym, bool ctrl, bool shift, bool alt) {
            const int vk = vkFromVtkKeySym(sym);
            if (!vk) return;
            Mod m = Mod::None;
            if (ctrl) m = m | Mod::Ctrl;
            if (alt) m = m | Mod::Alt;
            const bool letter_or_digit = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
            if (shift && !letter_or_digit) m = m | Mod::Shift;  // Shift+letter is just the upper-case letter
            workspace_->routeKey(KeyEvent{vk, m, true, false});
        };
        // ---- backend (starts lazily, on the first call)
        backend_ = std::make_unique<BackendClient>(BackendClient::configFromEnvironment(settings_.get(), args.python));
        workspace_->on_close_requested = [this] {
            settings_->sync();
            return true;
        };
        registerShortcuts();
    }

    ~NativeApp() {
        backend_->shutdown();
        scene_.reset();
    }

    int run() {
        if (!args_.result.empty() && !openResult(args_.result, /*remember=*/args_.selftest.empty())) {
            if (!args_.selftest.empty()) return finishSelfTest(false);
        }
        window_->show();
        if (!args_.selftest.empty()) {
            app_.post([this] { runSelfTest(); });
            return app_.run();
        }
        app_.startTimer(100, false, [this] { checkBackend(); });  // after the first frame
        const int code = app_.run();
        settings_->sync();
        return code;
    }

private:
    // ------------------------------------------------------------------ the application
    void registerShortcuts() {
        auto& s = workspace_->shortcuts();
        s.add("Ctrl+O", [this] { openDialog(); });
        s.add("Ctrl+B", [this] { checkBackend(); });
        s.add("Ctrl+Q", [this] { window_->requestClose(); });
        s.add("Esc", [this] { window_->requestClose(); });
        s.add("F", [this] {
            if (!model_) return;
            const auto& names = model_->scalar_names();
            const auto it = std::find(names.begin(), names.end(), scene_->field());
            scene_->setField(names[(static_cast<std::size_t>(it - names.begin()) + 1) % names.size()]);
            updateBaseStatus();
        });
        s.add("L", [this] { scene_->setLogScale(!scene_->logScale()); });
        s.add("C", [this] { if (model_ && !scene_->is3D()) scene_->setContours(!scene_->contours()); });
        s.add("M", [this] { if (model_ && !scene_->is3D()) scene_->setMeshLines(!scene_->meshLines()); });
        s.add("R", [this] { scene_->resetView(); });
        static const ViewPreset presets[] = {ViewPreset::Iso,    ViewPreset::PlusX,  ViewPreset::MinusX, ViewPreset::PlusY,
                                             ViewPreset::MinusY, ViewPreset::PlusZ, ViewPreset::MinusZ};
        for (int k = 1; k <= 7; ++k)
            s.add(std::string(1, static_cast<char>('0' + k)), [this, k] { if (scene_->is3D()) scene_->setViewPreset(presets[k - 1]); });
    }

    void updateBaseStatus() {
        if (!model_) {
            base_status_ = "no result open - Ctrl+O";
        } else {
            base_status_ = fs::path(model_path_).filename().string() + "   " + std::to_string(model_->dimensionality()) + "D   field: " +
                           scene_->field() + "   " + backend_status_;
        }
        workspace_->setStatus(base_status_);
    }

    bool openResult(const std::string& path, bool remember) {
        std::unique_ptr<NpzFile> npz;
        std::unique_ptr<ResultModel> model;
        try {
            npz = std::make_unique<NpzFile>(NpzFile::open(widen(path)));
            model = std::make_unique<ResultModel>(ResultModel::from_npz(*npz, path));
        } catch (const std::exception& e) {
            const std::string why = "cannot open " + path + ": " + e.what();
            workspace_->setStatus(why, StatusKind::Error);
            if (!args_.selftest.empty()) rec("open_result", false, why);  // the report says WHY, not just "failed"
            return false;
        }
        if (model->dimensionality() < 2 || model->scalar_names().empty()) {
            const std::string why = fs::path(path).filename().string() + " is a " +
                                    std::to_string(model->dimensionality()) +
                                    "D result: the field view shows 2D and 3D results (curves come with the plot panel)";
            workspace_->setStatus(why, StatusKind::Error);
            if (!args_.selftest.empty()) rec("open_result", false, why);
            return false;
        }
        scene_->setResult(model.get());  // the scene now points at the new model: the old one may go
        model_ = std::move(model);
        npz_ = std::move(npz);
        model_path_ = path;
        workspace_->setTitle(fs::path(path).filename().string());
        if (remember && settings_) {
            settings_->addRecent(path);
            settings_->sync();
        }
        updateBaseStatus();
        return true;
    }

    void openDialog() {
        FileDialogOptions o;
        o.title = L"Open a result";
        o.filters = {{L"Result files (*.npz)", L"*.npz"}, {L"All files", L"*.*"}};
        if (settings_ && !settings_->recentFiles().empty()) o.initial_directory = fs::path(settings_->recentFiles().front()).parent_path();
        if (const auto p = openFile(window_->hwnd(), o)) openResult(p->string(), true);
    }

    void checkBackend() {
        workspace_->setStatus(base_status_ + "   (backend: connecting...)");
        backend_->call("system.methods")->then([this](Reply& r) {
            if (r.ok()) {
                const auto& s = backend_->session();
                backend_status_ = "backend: pid " + std::to_string(s.backendPid()) + ", " + std::to_string(r.result().size()) + " methods";
                updateBaseStatus();
            } else {
                backend_status_ = "backend: " + r.errorMessage();
                workspace_->setStatus(base_status_ + "   " + backend_status_, StatusKind::Error);
            }
        });
    }

    // ------------------------------------------------------------------ the self-test
    struct Check {
        std::string name;
        bool ok;
        std::string detail;
    };
    void rec(const std::string& name, bool ok, const std::string& detail) {
        checks_.push_back({name, ok, detail});
        std::printf("%-4s %-16s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
        std::fflush(stdout);
    }

    void runSelfTest() {
        watchdog_ = app_.startTimer(120000, false, [this] {
            rec("watchdog", false, "the self-test did not finish within 120 s");
            finishSelfTest(true);
        });
        steps_ = {[this](auto done) { stepLayout(done); },     [this](auto done) { stepInput(done); },
                  [this](auto done) { stepShortcut(done); },   [this](auto done) { stepSettings(done); },
                  [this](auto done) { stepDialogs(done); },    [this](auto done) { stepTimers(done); },
                  [this](auto done) { stepPost(done); },       [this](auto done) { stepBackend(done); },
                  [this](auto done) { stepScene(done); },      [this](auto done) { stepNoQt(done); },
                  [this](auto done) { stepShutdown(done); }};
        nextStep();
    }
    void nextStep() {
        if (step_ >= steps_.size()) {
            finishSelfTest(true);
            return;
        }
        // one turn of the loop between steps, so a step never runs inside the previous step's callback
        auto run = steps_[step_++];
        app_.post([this, run] { run([this] { nextStep(); }); });
    }

    void stepLayout(const std::function<void()>& done) {
        const WorkspaceLayout l = workspace_->layout();
        RECT rc{};
        GetClientRect(host_->childWindow(), &rc);
        const double scale = window_->dpiScale();
        const auto [cw, ch] = window_->clientSize();
        const bool ok = scale > 0.5 && l.client.w == cw && l.client.h == ch && l.content.h + l.status.h == l.client.h &&
                        (rc.right - rc.left) == l.content.w && (rc.bottom - rc.top) == l.content.h && l.status.h == toDevice(24, scale);
        rec("dpi_layout", ok,
            "scale " + std::to_string(scale) + ", client " + std::to_string(cw) + "x" + std::to_string(ch) + ", content " +
                std::to_string(l.content.w) + "x" + std::to_string(l.content.h) + ", child " + std::to_string(rc.right - rc.left) + "x" +
                std::to_string(rc.bottom - rc.top));
        done();
    }

    void stepInput(const std::function<void()>& done) {
        std::vector<MouseEvent> mouse;
        std::vector<char32_t> chars;
        window_->handlers.on_mouse = [&](const MouseEvent& e) { mouse.push_back(e); };
        window_->handlers.on_char = [&](char32_t c) { chars.push_back(c); };
        const HWND h = window_->hwnd();
        const double s = window_->dpiScale();
        const int px = 100, py = 60;
        SendMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(px, py));
        SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(px, py));
        SendMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(px, py));
        POINT sp{px, py};
        ClientToScreen(h, &sp);
        SendMessageW(h, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
        SendMessageW(h, WM_CHAR, 'a', 0);
        SendMessageW(h, WM_CHAR, 0xD83D, 0);  // a surrogate pair: U+1F600
        SendMessageW(h, WM_CHAR, 0xDE00, 0);
        const bool move_ok = mouse.size() >= 1 && mouse[0].type == MouseType::Move && std::abs(mouse[0].x - px / s) < 0.01 && std::abs(mouse[0].y - py / s) < 0.01;
        const bool click_ok = mouse.size() >= 3 && mouse[1].type == MouseType::Down && mouse[1].button == MouseButton::Left &&
                              (mouse[1].buttons_down & (1u << static_cast<int>(MouseButton::Left))) && mouse[2].type == MouseType::Up && mouse[2].buttons_down == 0;
        const bool wheel_ok = mouse.size() >= 4 && mouse[3].type == MouseType::Wheel && std::abs(mouse[3].wheel_steps - 1.0) < 1e-9 && std::abs(mouse[3].x - px / s) < 0.01;
        const bool char_ok = chars.size() == 2 && chars[0] == U'a' && chars[1] == 0x1F600;
        rec("input", move_ok && click_ok && wheel_ok && char_ok,
            std::string("move ") + (move_ok ? "ok" : "BAD") + ", click " + (click_ok ? "ok" : "BAD") + ", wheel " + (wheel_ok ? "ok" : "BAD") +
                ", chars " + (char_ok ? "ok (incl. a surrogate pair)" : "BAD") + "; coordinates are logical (scale " + std::to_string(s) + ")");
        window_->handlers.on_mouse = nullptr;
        window_->handlers.on_char = nullptr;
        done();
    }

    void stepShortcut(const std::function<void()>& done) {
        int f12 = 0, ctrl_j = 0;
        workspace_->shortcuts().add("F12", [&] { ++f12; });
        workspace_->shortcuts().add("Ctrl+J", [&] { ++ctrl_j; });
        // The window reads modifiers with GetKeyState -- the REAL keyboard -- so a Ctrl/Shift/Alt the user happens to
        // hold made this synthetic F12 arrive as Ctrl+F12 etc. and the check failed intermittently (seen 2026-09-30).
        // Pin this thread's keyboard state to "no modifier" for the synthetic keys, and restore it after.
        BYTE keys[256]{};
        GetKeyboardState(keys);
        BYTE clean[256];
        std::copy(std::begin(keys), std::end(keys), clean);
        for (int vk : {VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_MENU, VK_LMENU, VK_RMENU})
            clean[vk] &= static_cast<BYTE>(~0x80);
        SetKeyboardState(clean);
        SendMessageW(window_->hwnd(), WM_KEYDOWN, VK_F12, 0x00000001);           // a real key message through the window
        SendMessageW(window_->hwnd(), WM_KEYDOWN, VK_F12, 0x40000001);           // auto-repeat: must not fire again
        SendMessageW(window_->hwnd(), WM_KEYUP, VK_F12, 0xC0000001);
        SetKeyboardState(keys);
        const bool routed = workspace_->routeKey(KeyEvent{'J', Mod::Ctrl, true, false});  // as from the VTK child window
        rec("shortcuts", f12 == 1 && ctrl_j == 1 && routed, "F12 via WM_KEYDOWN fired " + std::to_string(f12) + "x (repeat and key-up ignored), Ctrl+J routed from a child: " + std::to_string(ctrl_j));
        done();
    }

    void stepSettings(const std::function<void()>& done) {
        const fs::path dir = fs::temp_directory_path() / ("tcad_native_selftest_" + std::to_string(GetCurrentProcessId()));
        const fs::path f = dir / "settings.json";
        bool ok = false;
        std::string detail;
        {
            auto s = Settings::atFile(f);
            s.addRecent("C:\\data\\a.npz");
            s.setValue("backend/python", "C:\\py\\python.exe");
            s.setWindowPlacement(window_->placement());
            ok = s.sync();
        }
        if (ok) {
            auto t = Settings::atFile(f);
            const auto wp = t.windowPlacement();
            ok = t.recentFiles().size() == 1 && t.value("backend/python") == "C:\\py\\python.exe" && wp && *wp == window_->placement();
            detail = ok ? "written atomically to " + f.string() + " and read back (recent files, a value, the window placement)" : "the read-back differs";
        } else {
            detail = "cannot write " + f.string();
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
        rec("settings", ok, detail);
        done();
    }

    void stepDialogs(const std::function<void()>& done) {
        rec("file_dialogs", fileDialogsAvailable(), "IFileOpenDialog and IFileSaveDialog can be created and configured (not shown: a dialog needs a person)");
        done();
    }

    void stepTimers(const std::function<void()>& done) {
        auto repeat = std::make_shared<int>(0);
        auto once = std::make_shared<int>(0);
        auto rid = app_.startTimer(20, true, [repeat] { ++*repeat; });
        app_.startTimer(35, false, [once] { ++*once; });
        const double t0 = app_.nowMs();
        app_.startTimer(400, false, [this, repeat, once, rid, t0, done] {
            app_.stopTimer(rid);
            const int after = *repeat;
            const double dt = app_.nowMs() - t0;
            rec("timers", *repeat >= 5 && *once == 1, "a 20 ms repeating timer fired " + std::to_string(after) + "x in " + std::to_string(static_cast<int>(dt)) +
                                                          " ms; a 35 ms single-shot fired " + std::to_string(*once) + "x");
            done();
        });
    }

    void stepPost(const std::function<void()>& done) {
        auto result = std::make_shared<std::pair<bool, bool>>(false, false);  // ran, on_ui_thread
        std::thread([this, result] { Application::instance().post([this, result] { *result = {true, app_.onUiThread()}; }); }).join();
        const double t0 = app_.nowMs();
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [this, result, t0, done, poll] {
            if (result->first || app_.nowMs() - t0 > 2000) {
                rec("post", result->first && result->second, result->first ? "a function posted from another thread ran on the UI thread" : "the posted function never ran");
                done();
            } else {
                app_.startTimer(10, false, *poll);
            }
        };
        (*poll)();
    }

    void stepBackend(const std::function<void()>& done) {
        auto order = std::make_shared<std::vector<int>>();
        auto replies = std::make_shared<std::vector<ReplyPtr>>();
        auto finished = std::make_shared<int>(0);
        const std::vector<std::string> methods = {"system.methods", "examples.list", "nope.nothing", "system.ping"};
        auto evaluate = [this, order, replies, done] {
            const auto& r = *replies;
            bool fifo = order->size() == 4;
            for (std::size_t i = 0; i < order->size(); ++i) fifo = fifo && (*order)[i] == static_cast<int>(i);
            const bool methods_ok = r[0]->ok() && r[0]->result().is_array() && r[0]->result().size() > 20;
            const bool list_ok = r[1]->ok() && r[1]->result().is_array() && !r[1]->result().empty();
            const bool error_ok = !r[2]->ok() && r[2]->errorCode() == -32601;
            const bool ping_ok = r[3]->ok() && r[3]->result().value("pong", false);
            const auto& s = backend_->session();
            std::error_code ec;
            const bool session_ok = s.state() == RpcSession::State::Ready && s.backendPid() > 0 && !s.scratchDir().empty() && fs::exists(s.scratchDir(), ec) && !s.backendPrefix().empty();
            backend_pid_ = s.backendPid();
            backend_scratch_ = s.scratchDir();
            backend_prefix_ = s.backendPrefix();
            backend_methods_ = methods_ok ? static_cast<int>(r[0]->result().size()) : 0;
            std::string why;
            for (std::size_t i = 0; i < r.size(); ++i)
                if (!r[i]->ok() && i != 2) why += " [" + r[i]->method() + ": " + r[i]->errorMessage() + "]";
            rec("backend", fifo && methods_ok && list_ok && error_ok && ping_ok && session_ok,
                std::string("system.methods ") + (methods_ok ? std::to_string(backend_methods_) + " methods" : "BAD") + ", examples.list " + (list_ok ? "ok" : "BAD") +
                    ", unknown method -> JSON-RPC " + (error_ok ? "-32601" : "BAD") + ", ping " + (ping_ok ? "ok" : "BAD") + ", replies in call order " + (fifo ? "yes" : "NO") +
                    ", pid " + std::to_string(s.backendPid()) + ", handshake state " + (session_ok ? "Ready" : "NOT READY") + why);
            done();
        };
        for (std::size_t i = 0; i < methods.size(); ++i) {
            auto reply = backend_->call(methods[i]);
            replies->push_back(reply);
            reply->then([order, finished, i, evaluate, n = methods.size()](Reply&) {
                order->push_back(static_cast<int>(i));
                if (++*finished == static_cast<int>(n)) evaluate();
            });
        }
    }

    void stepScene(const std::function<void()>& done) {
        scene_->renderNow();
        scene_->renderNow();
        tcad::native::ImageStats st;
        const std::string png = args_.screenshot.empty() ? (fs::path(args_.selftest).replace_extension(".png")).string() : args_.screenshot;
        const bool captured = tcad::native::captureAndMeasure(host_->renderWindow(), png, &st);
        const auto [cw, ch] = window_->clientSize();
        const double s = window_->dpiScale();
        // hover: probe a grid over the map area until one point lies on the device (a map need not fill the view)
        std::string hover;
        bool hit = false;
        const double map_w = (cw - 130.0 * s) / s, map_h = (ch - toDevice(24, s)) / s;  // the bar column is on the right
        for (int iy = 1; iy <= 7 && !hit; ++iy)
            for (int ix = 1; ix <= 9 && !hit; ++ix) hit = scene_->readoutAtStr(map_w * ix / 10.0, map_h * iy / 8.0, &hover);
        const bool displayed = captured && st.distinct_colours >= 8 && st.non_background_fraction > 0.05;
        image_ = json{{"path", png}, {"width", st.width}, {"height", st.height}, {"distinct_colours", st.distinct_colours}, {"non_background_fraction", st.non_background_fraction}};
        hover_ = json{{"hit", hit}, {"text", hover}};
        rec("field_scene", displayed && hit,
            std::string("the existing FieldScene displays ") + fs::path(model_path_).filename().string() + " (" + std::to_string(model_->dimensionality()) + "D, field " + scene_->field() +
                ") in the native window: " + std::to_string(st.distinct_colours) + " colours, " + std::to_string(static_cast<int>(st.non_background_fraction * 100)) + "% non-background; hover " +
                (hit ? "hit: " + hover : "MISSED"));
        done();
    }

    void stepNoQt(const std::function<void()>& done) {
        const auto modules = tcad::native::loadedModuleNames();
        qt_modules_.clear();
        for (const auto& m : modules)
            if (tcad::native::isQtModule(m)) qt_modules_.push_back(m);
        module_count_ = modules.size();
        rec("no_qt_loaded", qt_modules_.empty(), std::to_string(modules.size()) + " modules loaded in this process; Qt modules: " + std::to_string(qt_modules_.size()));
        done();
    }

    void stepShutdown(const std::function<void()>& done) {
        auto& s = backend_->session();
        const std::int64_t pid = s.backendPid();
        const std::string scratch = s.scratchDir();
        backend_->shutdown();
        std::error_code ec;
        bool gone = true;
        if (pid > 0) {
            if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid))) {
                gone = WaitForSingleObject(h, 3000) == WAIT_OBJECT_0;
                CloseHandle(h);
            }
        }
        const bool ok = s.state() == RpcSession::State::NotStarted && gone && !scratch.empty() && !fs::exists(scratch, ec);
        rec("backend_shutdown", ok, "system.shutdown: pid " + std::to_string(pid) + (gone ? " exited" : " STILL RUNNING") + ", scratch directory " + (fs::exists(scratch, ec) ? "STILL THERE" : "removed"));
        // and a call afterwards starts a fresh service
        backend_->call("system.ping")->then([this, pid, done](Reply& r) {
            const std::int64_t pid2 = backend_->session().backendPid();
            rec("backend_restart", r.ok() && pid2 > 0 && pid2 != pid, "a call after shutdown started a new service: pid " + std::to_string(pid2));
            done();
        });
    }

    int finishSelfTest(bool from_loop) {
        if (watchdog_) app_.stopTimer(watchdog_);
        watchdog_ = 0;
        bool ok = !checks_.empty();
        json arr = json::array();
        for (const auto& c : checks_) {
            ok = ok && c.ok;
            arr.push_back({{"name", c.name}, {"ok", c.ok}, {"detail", c.detail}});
        }
        json j = {{"ok", ok},
                  {"result", model_path_},
                  {"checks", arr},
                  {"backend", {{"pid", backend_pid_}, {"prefix", backend_prefix_}, {"scratch_dir", backend_scratch_}, {"methods", backend_methods_}}},
                  {"image", image_},
                  {"hover", hover_},
                  {"loaded_module_count", module_count_},
                  {"qt_modules_loaded", qt_modules_}};
        std::ofstream(args_.selftest, std::ios::binary) << j.dump(2) << "\n";
        std::printf("%s\n", ok ? "NATIVE SELFTEST: ALL CHECKS PASSED" : "NATIVE SELFTEST: FAILED");
        exit_code_ = ok ? 0 : 1;
        if (from_loop) app_.quit(exit_code_);
        return exit_code_;
    }

    Application& app_;
    Args args_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Workspace> workspace_;
    std::unique_ptr<BackendClient> backend_;
    std::unique_ptr<tcad::native::VtkWin32Host> host_;
    std::unique_ptr<VtkChildHost> child_host_;
    std::unique_ptr<NpzFile> npz_;
    std::unique_ptr<ResultModel> model_;
    std::unique_ptr<FieldScene> scene_;
    std::string model_path_, base_status_ = "no result open - Ctrl+O", backend_status_;
    // self-test
    std::vector<Check> checks_;
    std::vector<std::function<void(std::function<void()>)>> steps_;
    std::size_t step_ = 0;
    Application::TimerId watchdog_ = 0;
    int exit_code_ = 1;
    std::int64_t backend_pid_ = 0;
    std::string backend_scratch_, backend_prefix_;
    int backend_methods_ = 0;
    json image_ = json::object(), hover_ = json::object();
    std::vector<std::string> qt_modules_;
    std::size_t module_count_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, &args)) {
        std::fputs("usage: tcad_native [result.npz] [--settings <file.json>] [--ephemeral] [--python <interpreter>]\n"
                   "       tcad_native <result.npz> --selftest <report.json> [--screenshot <out.png>]\n", stderr);
        return 2;
    }
    try {
        Application app;
        NativeApp native(app, args);
        return native.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tcad_native: %s\n", e.what());
        return 4;
    }
}

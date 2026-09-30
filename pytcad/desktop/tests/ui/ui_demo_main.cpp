// tcad_ui_demo (N2a): the render core in a real, visible window -- the sample scene at the window's DPI, redrawn
// on resize and on DPI change (drag it between monitors). F5 simulates a device loss (the scene must come back);
// Esc closes. For looking at, not for gating: the gate is tcad_ui_render_tests.
//
//   tcad_ui_demo [--warp] [--screenshot <png> (render one frame, save it, exit)]
//   tcad_ui_demo --gallery [--warp] [--screenshot <png>]   the N2/N3a gallery (ui_gallery.hpp)
//   tcad_ui_demo --edits   two N2e line edits in a real window: the manual IME / dictation / touch-keyboard checklist
//                          of NATIVE-DESKTOP-PLAN.md 27.7.5 is run here
#include "render_scene.hpp"
#include "ui_gallery.hpp"

#include "platform/app.hpp"
#include "platform/window.hpp"
#include "ui/core/layout.hpp"
#include "ui/render/window_surface.hpp"
#include "ui/win32/line_edit.hpp"
#include "ui/win32/ui_window.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace tcad;

int main(int argc, char** argv) {
    bool warp = false, edits = false, gallery_mode = false;
    std::string shot;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--warp") warp = true;
        else if (a == "--screenshot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--edits") edits = true;
        else if (a == "--gallery") gallery_mode = true;
    }
    platform::Application app;  // initialises COM on this thread
    if (gallery_mode) {
        auto dev = ui::RenderDevice::create({.warp = warp});
        if (!dev) return 1;
        auto uw = ui::UiWindow::create(*dev, {.title = L"PyTCAD - N2/N3a gallery", .width = 760, .height = 560});
        if (!uw) return 1;
        ui::gallery::build(**uw, [](const std::string& s) { std::printf("%s\n", s.c_str()); });
        if (!shot.empty()) {
            (*uw)->resizeClient(760, 560);
            ui::Image img;
            const auto st = (*uw)->renderNow(&img);
            auto r = ui::savePng((*dev)->wic(), img, shot);
            std::printf("%s: %dx%d, status %d, %s\n", shot.c_str(), img.width, img.height, static_cast<int>(st), r ? "saved" : r.error().c_str());
            return r && st == ui::FrameStatus::Presented ? 0 : 1;
        }
        (*uw)->window().handlers.on_close_requested = [&app] {
            app.quit(0);
            return true;
        };
        (*uw)->window().show();
        (*uw)->renderNow();
        return app.run();
    }
    if (edits) {
        auto dev = ui::RenderDevice::create({.warp = warp});
        if (!dev) return 1;
        auto uw = ui::UiWindow::create(*dev, {.title = L"PyTCAD - N2e line edits (IME checklist)", .width = 520, .height = 160});
        if (!uw) return 1;
        auto* col = (*uw)->root().setLayout<ui::BoxLayout>(ui::Orientation::Vertical);
        auto* a = col->add<ui::LineEdit>((*uw)->window().hwnd());
        auto* b = col->add<ui::LineEdit>((*uw)->window().hwnd());
        col->addStretch(1);
        a->setText("Type here (Japanese IME, Pinyin, Win+. emoji, Win+H dictation)");
        b->setText("second edit: Tab moves between the two");
        a->on_editing_finished = [a] { std::printf("editing finished: %s\n", a->text().c_str()); };
        std::printf("TSF %s\n", a->tsfReady() ? "ready" : "NOT available (WM_CHAR only)");
        (*uw)->window().handlers.on_close_requested = [&app] {
            app.quit(0);
            return true;
        };
        (*uw)->window().show();
        a->setFocus();
        (*uw)->renderNow();
        return app.run();
    }
    auto dev = ui::RenderDevice::create({.warp = warp});
    if (!dev) {
        std::fprintf(stderr, "render device: %s\n", dev.error().c_str());
        return 1;
    }
    auto win = platform::Window::create({.title = L"PyTCAD - N2a render core", .width = 640, .height = 400});
    if (!win) {
        std::fprintf(stderr, "window: %s\n", win.error().c_str());
        return 1;
    }
    platform::Window& w = **win;
    auto [cw, ch] = w.clientSize();
    auto surf = ui::WindowSurface::create(*dev, w.hwnd(), cw, ch, w.dpiScale());
    if (!surf) {
        std::fprintf(stderr, "surface: %s\n", surf.error().c_str());
        return 1;
    }
    ui::WindowSurface& s = **surf;
    auto paint = [&](ui::Image* capture = nullptr) {
        const auto st = s.render(
            [&](ID2D1DeviceContext2* ctx) {
                // fit the 320x200 DIP scene into the window, keeping its aspect
                const auto size = s.sizeDips();
                const float k = std::min(size.width / ui::testing::kSceneWidthDip, size.height / ui::testing::kSceneHeightDip);
                ctx->Clear(D2D1::ColorF(0xFFFFFF, 1.0f));
                ctx->SetTransform(D2D1::Matrix3x2F::Scale(k, k));
                ui::testing::drawScene(ctx, **dev);
            },
            {.capture = capture});
        if (st == ui::FrameStatus::Failed) std::fprintf(stderr, "frame failed: %s\n", s.lastError().c_str());
    };
    w.handlers.on_resize = [&](int wpx, int hpx) {
        (void)s.resize(wpx, hpx);
        paint();
    };
    w.handlers.on_dpi_changed = [&](double scale) {
        s.setDpiScale(scale);
        paint();
    };
    w.handlers.on_paint = [&](HDC, const RECT&) { paint(); };
    w.handlers.on_key = [&](const platform::KeyEvent& e) {
        if (!e.down) return false;
        if (e.vk == VK_ESCAPE) {
            w.requestClose();
            return true;
        }
        if (e.vk == VK_F5) {
            std::printf("simulating device loss: %s\n", (*dev)->simulateLoss() ? "removed" : "not supported");
            paint();
            std::printf("device generation now %llu\n", static_cast<unsigned long long>((*dev)->generation()));
            return true;
        }
        return false;
    };
    w.handlers.on_close_requested = [&] {
        app.quit(0);
        return true;
    };
    if (!shot.empty()) {
        ui::Image img;
        paint(&img);
        auto r = ui::savePng((*dev)->wic(), img, shot);
        std::printf("%s: %dx%d, %s\n", shot.c_str(), img.width, img.height, r ? "saved" : r.error().c_str());
        return r ? 0 : 1;
    }
    std::printf("adapter: %ls (%s); F5 = simulate device loss, Esc = close\n", (*dev)->adapterName().c_str(),
                (*dev)->isSoftware() ? "software" : "hardware");
    w.show();
    paint();
    return app.run();
}

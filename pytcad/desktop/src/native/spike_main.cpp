// tcad_native_spike -- the first spike of the Qt purge (NATIVE-DESKTOP-PLAN.md section 27.4):
// the EXISTING field view (FieldScene: the whole VTK pipeline of a solved result) shown in a
// native Win32 window, with no Qt, no ADS and no vtkGUISupportQt.
//
//   tcad_native_spike <result.npz> [--field NAME] [--size WxH] [--log] [--contours] [--mesh]
//                     [--screenshot out.png] [--json out.json]
// With --screenshot the window is shown, two frames are drawn, the frame buffer is written to
// the PNG, a JSON report is written (if --json), and the process exits 0 only if the image is
// not blank and NO Qt module is loaded in this process. Without it the window stays open:
//   hover: the readout is shown in the title bar    F: next field    L: log scale
//   C: contours   M: mesh lines   R: reset view   1..7: iso/+x/-x/+y/-y/+z/-z (3D)   Esc: quit
#include "data/npz.hpp"
#include "data/result_model.hpp"
#include "native/vtk_win32_host.hpp"
#include "native/win32_window.hpp"
#include "views/field/field_scene.hpp"

#include <nlohmann/json.hpp>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkUnsignedCharArray.h>
#include <vtkWindowToImageFilter.h>
#include <vtkImageData.h>
#include <vtkPointData.h>

#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using tcad::desktop::FieldScene;
using tcad::desktop::ResultModel;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// File names of every module loaded in this process, lower-cased.
std::vector<std::string> loadedModuleNames() {
    std::vector<HMODULE> mods(1024);
    DWORD needed = 0;
    if (!EnumProcessModulesEx(GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)),
                              &needed, LIST_MODULES_ALL))
        return {};
    if (needed > mods.size() * sizeof(HMODULE)) {
        mods.resize(needed / sizeof(HMODULE) + 16);
        if (!EnumProcessModulesEx(GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)),
                                  &needed, LIST_MODULES_ALL))
            return {};
    }
    std::vector<std::string> out;
    for (std::size_t i = 0; i < needed / sizeof(HMODULE); ++i) {
        wchar_t path[MAX_PATH * 2];
        if (!GetModuleFileNameW(mods[i], path, static_cast<DWORD>(std::size(path)))) continue;
        std::wstring w(path);
        const auto slash = w.find_last_of(L"\\/");
        std::string name;
        for (wchar_t c : w.substr(slash == std::wstring::npos ? 0 : slash + 1)) name.push_back(static_cast<char>(std::tolower(c < 128 ? c : '?')));
        out.push_back(name);
    }
    return out;
}

bool isQtModule(const std::string& n) {
    return n.starts_with("qt5") || n.starts_with("qt6") || n.starts_with("qtadvanceddocking") ||
           n.find("guisupportqt") != std::string::npos || n.find("renderingqt") != std::string::npos ||
           n == "qwindows.dll";
}

struct ImageStats {
    int width = 0, height = 0;
    std::size_t distinct_colours = 0;
    double non_background_fraction = 0;
};

// Capture the render window's front buffer as RGB, write the PNG, and measure it.
bool captureAndMeasure(vtkRenderWindow* rw, const std::string& png, ImageStats* st) {
    vtkNew<vtkWindowToImageFilter> grab;
    grab->SetInput(rw);
    grab->SetInputBufferTypeToRGB();
    grab->ReadFrontBufferOff();
    grab->Update();
    vtkImageData* img = grab->GetOutput();
    int dims[3];
    img->GetDimensions(dims);
    st->width = dims[0];
    st->height = dims[1];
    auto* px = vtkUnsignedCharArray::SafeDownCast(img->GetPointData()->GetScalars());
    if (!px || px->GetNumberOfComponents() < 3) return false;
    std::set<unsigned> colours;
    std::size_t different = 0;
    const vtkIdType n = px->GetNumberOfTuples();
    unsigned bg = 0;
    if (n > 0) {
        unsigned char* p = px->GetPointer(0);
        bg = (unsigned(p[0]) << 16) | (unsigned(p[1]) << 8) | p[2];  // the corner pixel is background
    }
    for (vtkIdType i = 0; i < n; ++i) {
        unsigned char* p = px->GetPointer(i * px->GetNumberOfComponents());
        const unsigned c = (unsigned(p[0]) << 16) | (unsigned(p[1]) << 8) | p[2];
        if (colours.size() < 100000) colours.insert(c);
        if (c != bg) ++different;
    }
    st->distinct_colours = colours.size();
    st->non_background_fraction = n ? static_cast<double>(different) / static_cast<double>(n) : 0.0;
    if (!png.empty()) {
        vtkNew<vtkPNGWriter> w;
        w->SetFileName(png.c_str());
        w->SetInputData(img);
        w->Write();
    }
    return true;
}

struct Args {
    std::string result, field, png, json;
    int w = 1280, h = 800;
    bool log = false, contours = false, mesh = false;
};

bool parseArgs(int argc, char** argv, Args* a) {
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (s == "--field") a->field = val();
        else if (s == "--screenshot") a->png = val();
        else if (s == "--json") a->json = val();
        else if (s == "--size") {
            const std::string v = val();
            if (std::sscanf(v.c_str(), "%dx%d", &a->w, &a->h) != 2 || a->w < 200 || a->h < 150) return false;
        } else if (s == "--log") a->log = true;
        else if (s == "--contours") a->contours = true;
        else if (s == "--mesh") a->mesh = true;
        else if (!s.starts_with("--") && a->result.empty()) a->result = s;
        else return false;
    }
    return !a->result.empty();
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, &args)) {
        std::fputs("usage: tcad_native_spike <result.npz> [--field NAME] [--size WxH] [--log] [--contours] [--mesh]\n"
                   "                         [--screenshot out.png] [--json out.json]\n", stderr);
        return 2;
    }

    std::unique_ptr<tcad::desktop::NpzFile> npz;
    std::unique_ptr<ResultModel> model;
    try {
        npz = std::make_unique<tcad::desktop::NpzFile>(tcad::desktop::NpzFile::open(widen(args.result)));
        model = std::make_unique<ResultModel>(ResultModel::from_npz(*npz, args.result));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cannot read %s: %s\n", args.result.c_str(), e.what());
        return 3;
    }
    if (model->dimensionality() < 2 || model->scalar_names().empty()) {
        std::fprintf(stderr, "%s is a %dD result: the field view shows 2D and 3D results\n", args.result.c_str(),
                     model->dimensionality());
        return 3;
    }

    auto win = tcad::native::Window::create(L"PyTCAD native spike", args.w, args.h);
    if (!win) {
        std::fprintf(stderr, "window: %s\n", win.error().c_str());
        return 4;
    }
    auto& window = **win;
    const auto [cw, ch] = window.clientSize();

    std::unique_ptr<tcad::native::VtkWin32Host> host;
    FieldScene scene;
    try {
        host = std::make_unique<tcad::native::VtkWin32Host>(window.hwnd(), cw, ch);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "render host: %s\n", e.what());
        return 4;
    }
    scene.attach(host->renderWindow(), host.get());
    host->bind(&scene);
    scene.on_readout_changed = [&](const std::string& text) { window.setTitle(text.empty() ? L"PyTCAD native spike" : widen(text)); };

    window.callbacks.on_size = [&](int w, int h) { host->resize(w, h); };
    window.callbacks.on_close = [&] { DestroyWindow(window.hwnd()); };

    scene.setResult(model.get());
    if (!args.field.empty()) {
        const auto& names = model->scalar_names();
        if (std::find(names.begin(), names.end(), args.field) == names.end()) {
            std::fprintf(stderr, "no field '%s' in %s\n", args.field.c_str(), args.result.c_str());
            return 3;
        }
        scene.setField(args.field);
    }
    if (args.log) scene.setLogScale(true);
    if (args.contours && !scene.is3D()) scene.setContours(true);
    if (args.mesh && !scene.is3D()) scene.setMeshLines(true);

    host->on_key = [&](const std::string& key) {
        if (key == "Escape") {
            window.requestClose();
        } else if (key == "f" || key == "F") {
            const auto& names = model->scalar_names();
            const auto it = std::find(names.begin(), names.end(), scene.field());
            scene.setField(names[(static_cast<std::size_t>(it - names.begin()) + 1) % names.size()]);
        } else if (key == "l" || key == "L") {
            scene.setLogScale(!scene.logScale());
        } else if ((key == "c" || key == "C") && !scene.is3D()) {
            scene.setContours(!scene.contours());
        } else if ((key == "m" || key == "M") && !scene.is3D()) {
            scene.setMeshLines(!scene.meshLines());
        } else if (key == "r" || key == "R") {
            scene.resetView();
        } else if (scene.is3D() && key.size() == 1 && key[0] >= '1' && key[0] <= '7') {
            using tcad::desktop::ViewPreset;
            const ViewPreset presets[] = {ViewPreset::Iso,    ViewPreset::PlusX,  ViewPreset::MinusX, ViewPreset::PlusY,
                                          ViewPreset::MinusY, ViewPreset::PlusZ, ViewPreset::MinusZ};
            scene.setViewPreset(presets[key[0] - '1']);
        }
    };

    window.show();
    SetFocus(host->childWindow());
    int exit_code = 0;
    window.pump(&exit_code);
    scene.renderNow();
    scene.renderNow();

    if (args.png.empty()) return window.run();  // interactive

    // ---- the automatic check: display, hover, no Qt --------------------------------------
    ImageStats st;
    const bool captured = captureAndMeasure(host->renderWindow(), args.png, &st);
    const auto [px, py] = window.clientSize();
    std::string hover;
    const bool hover_hit = scene.readoutAtStr(0.4 * px / window.dpiScale(), 0.5 * py / window.dpiScale(), &hover);
    const auto modules = loadedModuleNames();
    std::vector<std::string> qt_modules;
    for (const auto& m : modules)
        if (isQtModule(m)) qt_modules.push_back(m);

    const bool displayed = captured && st.distinct_colours >= 8 && st.non_background_fraction > 0.05;
    const bool ok = displayed && qt_modules.empty();
    nlohmann::json j = {{"ok", ok},
                        {"result", args.result},
                        {"dimensionality", model->dimensionality()},
                        {"field", scene.field()},
                        {"window_px", {px, py}},
                        {"dpi_scale", window.dpiScale()},
                        {"image", {{"path", args.png}, {"width", st.width}, {"height", st.height},
                                   {"distinct_colours", st.distinct_colours},
                                   {"non_background_fraction", st.non_background_fraction}}},
                        {"displayed", displayed},
                        {"hover", {{"hit", hover_hit}, {"text", hover}}},
                        {"gl_capabilities", host->glCapabilities()},
                        {"loaded_module_count", modules.size()},
                        {"qt_modules_loaded", qt_modules}};
    std::printf("%s\n", j.dump(2).c_str());
    if (!args.json.empty()) {
        std::ofstream f(args.json, std::ios::binary);
        f << j.dump(2) << "\n";
    }
    return ok ? 0 : 1;
}

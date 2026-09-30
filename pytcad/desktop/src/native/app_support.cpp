#include "native/app_support.hpp"

#include <windows.h>

#include <psapi.h>

#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkPointData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkWindowToImageFilter.h>

#include <cctype>
#include <set>

namespace tcad::native {

std::vector<std::string> loadedModuleNames() {
    std::vector<HMODULE> mods(1024);
    DWORD needed = 0;
    if (!EnumProcessModulesEx(GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed, LIST_MODULES_ALL))
        return {};
    if (needed > mods.size() * sizeof(HMODULE)) {
        mods.resize(needed / sizeof(HMODULE) + 16);
        if (!EnumProcessModulesEx(GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed, LIST_MODULES_ALL))
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
           n.find("guisupportqt") != std::string::npos || n.find("renderingqt") != std::string::npos || n == "qwindows.dll";
}

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
    const int comps = px->GetNumberOfComponents();
    const vtkIdType n = px->GetNumberOfTuples();
    std::set<unsigned> colours;
    std::size_t different = 0;
    unsigned bg = 0;
    if (n > 0) {
        unsigned char* p = px->GetPointer(0);
        bg = (unsigned(p[0]) << 16) | (unsigned(p[1]) << 8) | p[2];  // the corner pixel is background
    }
    for (vtkIdType i = 0; i < n; ++i) {
        unsigned char* p = px->GetPointer(i * comps);
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

}  // namespace tcad::native

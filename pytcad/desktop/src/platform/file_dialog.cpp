#include "platform/file_dialog.hpp"

#include <shobjidl.h>
#include <wrl/client.h>

namespace tcad::platform {
namespace {

using Microsoft::WRL::ComPtr;

enum class Kind { Open, Save, Folder };

HRESULT configure(IFileDialog* d, const FileDialogOptions& o, Kind kind) {
    DWORD flags = 0;
    d->GetOptions(&flags);
    flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    if (kind == Kind::Open) flags |= FOS_FILEMUSTEXIST;
    if (kind == Kind::Save) flags |= FOS_OVERWRITEPROMPT;
    if (kind == Kind::Folder) flags |= FOS_PICKFOLDERS;
    d->SetOptions(flags);
    if (!o.title.empty()) d->SetTitle(o.title.c_str());
    if (kind != Kind::Folder && !o.filters.empty()) {
        std::vector<COMDLG_FILTERSPEC> specs;
        specs.reserve(o.filters.size());
        for (const auto& f : o.filters) specs.push_back({f.name.c_str(), f.spec.c_str()});
        d->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        d->SetFileTypeIndex(1);
    }
    if (!o.default_extension.empty()) d->SetDefaultExtension(o.default_extension.c_str());
    if (!o.initial_name.empty()) d->SetFileName(o.initial_name.c_str());
    if (!o.initial_directory.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(o.initial_directory.c_str(), nullptr, IID_PPV_ARGS(&folder)))) d->SetFolder(folder.Get());
    }
    return S_OK;
}

std::optional<std::filesystem::path> show(HWND owner, const FileDialogOptions& o, Kind kind) {
    ComPtr<IFileDialog> dialog;
    const HRESULT hr = kind == Kind::Save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
                                          : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return std::nullopt;
    configure(dialog.Get(), o, kind);
    if (FAILED(dialog->Show(owner))) return std::nullopt;  // cancelled (HRESULT_FROM_WIN32(ERROR_CANCELLED)) or failed
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    std::filesystem::path out(path);
    CoTaskMemFree(path);
    return out;
}

}  // namespace

std::optional<std::filesystem::path> openFile(HWND owner, const FileDialogOptions& o) { return show(owner, o, Kind::Open); }
std::optional<std::filesystem::path> saveFile(HWND owner, const FileDialogOptions& o) { return show(owner, o, Kind::Save); }
std::optional<std::filesystem::path> pickFolder(HWND owner, const FileDialogOptions& o) { return show(owner, o, Kind::Folder); }

bool fileDialogsAvailable() {
    for (const CLSID& clsid : {CLSID_FileOpenDialog, CLSID_FileSaveDialog}) {
        ComPtr<IFileDialog> d;
        if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
        FileDialogOptions o;
        o.title = L"probe";
        o.filters = {{L"All", L"*.*"}};
        configure(d.Get(), o, Kind::Open);
    }
    return true;
}

}  // namespace tcad::platform

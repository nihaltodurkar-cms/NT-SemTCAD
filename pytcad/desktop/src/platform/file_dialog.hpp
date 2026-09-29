// The Windows common file dialogs (N1; replaces QFileDialog): IFileOpenDialog / IFileSaveDialog, in the
// single-threaded apartment the Application initialises. Paths are returned as std::filesystem::path.
#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tcad::platform {

struct FileFilter {
    std::wstring name;  // "Result files"
    std::wstring spec;  // "*.npz;*.json"
};

struct FileDialogOptions {
    std::wstring title;
    std::vector<FileFilter> filters;
    std::wstring default_extension;                // "npz" (no dot), for save
    std::filesystem::path initial_directory;       // may be empty
    std::wstring initial_name;                     // save: the suggested file name
};

// Show the dialog modally over `owner`. nullopt: cancelled (or the dialog could not be created).
std::optional<std::filesystem::path> openFile(HWND owner, const FileDialogOptions& options);
std::optional<std::filesystem::path> saveFile(HWND owner, const FileDialogOptions& options);
std::optional<std::filesystem::path> pickFolder(HWND owner, const FileDialogOptions& options);
// True when the COM classes behind the dialogs can be created here (they are created, configured and released
// without being shown): the automated check that the dialogs are available.
bool fileDialogsAvailable();

}  // namespace tcad::platform

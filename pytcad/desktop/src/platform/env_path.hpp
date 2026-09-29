// Where the Python backend lives and the environment it runs in (N1; the Qt-free port of
// backend_client.cpp's resolveBackendConfig / pythonProcessEnvironment / resolveManifestPath). Pure over
// std::filesystem and strings, so it is unit-tested on any platform.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tcad::platform {

struct RpcConfig {
    std::string python;                                 // the backend's interpreter
    std::vector<std::string> args{"-m", "backend_service"};
    std::string working_dir;                            // pytcad/ (backend_service's parent)
    std::vector<std::string> strip_from_path;           // directories removed from the child's PATH
    bool debug = false;                                 // TCAD_BACKEND_DEBUG=1 (debug.* methods)
    int default_timeout_ms = 30000;
    int shutdown_wait_ms = 2000;
};

// A directory as compared in PATH lists: lexically normalised, '/' separators, no trailing separator,
// lower-cased (Windows paths are case-insensitive).
std::string normalizedDir(const std::string& d);

// PATH for a Python child: `current_path` (split on `sep`) with `strip` removed and -- when `python` sits at the
// root of a conda-style prefix (a Library/bin directory beside it) -- that prefix's own DLL directories
// PREPENDED, exactly the ones `conda activate` adds (<prefix>, Library/mingw-w64/bin, Library/usr/bin,
// Library/bin, Scripts, bin), those that exist. Without them a non-activated conda interpreter aborts on its
// first LAPACK call (exit 0xC06D007F, found 2026-09-29). `native_seps` writes added directories with '\\'.
std::string pythonPathEnvironment(const std::string& current_path, const std::string& python,
                                  const std::vector<std::string>& strip, char sep = ';', bool native_seps = true);

// A desktop_runtime.json path value as used: an absolute path unchanged, a relative one resolved against
// `app_dir` (the installed layout ships "runtime/python.exe" and "backend" next to the executable). Empty stays
// empty.
std::string resolveManifestPath(const std::string& app_dir, const std::string& value);

// Where the backend lives, in order: desktop_runtime.json's backend_python / backend_root / runtime_bin
// (`manifest_text`, "" if there is none), then `settings_python`, then the TCAD_BACKEND_PYTHON /
// TCAD_BACKEND_ROOT environment variables (read through `getenv`). An unreadable manifest is ignored.
RpcConfig resolveBackendConfig(const std::string& app_dir, const std::string& manifest_text,
                                          const std::string& settings_python,
                                          const std::function<std::optional<std::string>(const char*)>& getenv);

}  // namespace tcad::platform

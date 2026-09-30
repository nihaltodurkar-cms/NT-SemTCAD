"""Print, as JSON, every module loaded into THIS process (Windows only).

Run by s4_evidence.ps1 with the staged runtime\\python.exe to see which UCRT the operating system really
loaded for it. It asks the process about ITSELF (psapi EnumProcessModulesEx, LIST_MODULES_ALL), so there is no
start/sleep/sample race and no cross-process access right involved -- the two things that made the external
Process.Modules sampling report "no matching modules read".

    python loaded_modules.py [--pattern REGEX]

Exit 0 and one JSON object on stdout: {executable, prefix, python, platform, moduleCount, modules[], matches[]}.
Exit 2 (message on stderr) if not on Windows or if the module list could not be read: never a silent empty list.
"""
import argparse
import json
import re
import sys

DEFAULT_PATTERN = r"^(ucrtbase|api-ms-win-crt|vcruntime|msvcp|concrt|vccorlib|vcamp|vcomp)"
LIST_MODULES_ALL = 3


def read_modules():
    import ctypes
    from ctypes import wintypes
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetCurrentProcess.restype = wintypes.HANDLE
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p), wintypes.DWORD,
                                           ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.EnumProcessModulesEx.restype = wintypes.BOOL
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.LPWSTR, wintypes.DWORD]
    psapi.GetModuleFileNameExW.restype = wintypes.DWORD
    proc = k32.GetCurrentProcess()
    count = 1024
    while True:
        arr = (ctypes.c_void_p * count)()
        needed = wintypes.DWORD(0)
        if not psapi.EnumProcessModulesEx(proc, arr, ctypes.sizeof(arr), ctypes.byref(needed), LIST_MODULES_ALL):
            raise OSError(f"EnumProcessModulesEx failed (winerror {ctypes.get_last_error()})")
        n = needed.value // ctypes.sizeof(ctypes.c_void_p)
        if n <= count:
            break
        count = n + 64                                    # the list grew between the two calls: retry bigger
    out = []
    for i in range(n):
        buf = ctypes.create_unicode_buffer(32768)
        if psapi.GetModuleFileNameExW(proc, arr[i], buf, len(buf)):
            out.append(buf.value)
    return out


def summarize(paths, prefix, pattern=DEFAULT_PATTERN):
    """Pure: module paths -> the report dict. `prefix` is the directory whose files count as 'from the stage'."""
    rx = re.compile(pattern, re.IGNORECASE)
    norm = prefix.replace("/", "\\").rstrip("\\").lower() + "\\"
    mods = []
    for p in paths:
        name = re.split(r"[\\/]", p)[-1]
        mods.append({"module": name, "path": p, "fromStage": p.replace("/", "\\").lower().startswith(norm)})
    return {"moduleCount": len(mods), "modules": mods, "matches": [m for m in mods if rx.search(m["module"])]}


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--pattern", default=DEFAULT_PATTERN)
    ap.add_argument("--prefix", default=None, help="stage root for fromStage (default: this interpreter's directory)")
    args = ap.parse_args(argv)
    if sys.platform != "win32":
        print("loaded_modules.py: Windows only", file=sys.stderr)
        return 2
    try:
        paths = read_modules()
    except OSError as e:
        print(f"loaded_modules.py: {e}", file=sys.stderr)
        return 2
    if not paths:
        print("loaded_modules.py: the module list is empty (cannot be: this process has python.exe itself)", file=sys.stderr)
        return 2
    import os
    prefix = args.prefix or os.path.dirname(sys.executable)
    rep = {"executable": sys.executable, "prefix": prefix, "python": sys.version.split()[0], "platform": sys.platform}
    rep.update(summarize(paths, prefix, args.pattern))
    json.dump(rep, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())

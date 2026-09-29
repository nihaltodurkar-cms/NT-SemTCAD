# Stage a self-contained copy of the native desktop app (NATIVE-DESKTOP-PLAN.md
# section 26, P5-S1): the Release build, every DLL it actually loads (found by
# walking PE imports, not from a hand-written list), the Qt plugins
# windeployqt6 selects, and a desktop_runtime.json whose paths are RELATIVE to
# the exe (resolveManifestPath in backend_client.cpp).
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\stage.ps1 [-Out <dir>] [-DevBackend] [-Check <a.npz>]
#
# -DevBackend: until the slim Python runtime (P5-S2) exists, point
#   backend_python/backend_root at this machine's tcad-dev env and source tree
#   (absolute paths). Without it the manifest names runtime\python.exe and
#   backend\, which S2 fills in.
# -Check <npz>: after staging, run the staged app's --selftest on <npz> with a
#   PATH holding only C:\Windows directories -- proof that nothing is picked up
#   from the conda envs.
param(
    [string] $Out = "",
    [switch] $DevBackend,
    [string] $Check = "",
    [switch] $NoBuild
)
$ErrorActionPreference = "Stop"
$desktop = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $desktop
$build = Join-Path $root "build\desktop"
if (-not $Out) { $Out = Join-Path $root "dist\TCAD" }

# 1. Release build (build.ps1 does the MSVC + tcad-gui setup).
if (-not $NoBuild) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $desktop "build.ps1") -Config Release
    if ($LASTEXITCODE) { throw "build.ps1 failed" }
}
$exe = Join-Path $build "tcad_desktop.exe"
if (-not (Test-Path $exe)) { throw "no Release build at $exe" }

# MSVC environment for dumpbin (the same vswhere/vcvars route build.ps1 uses).
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$env:PATH = (Split-Path -Parent $vswhere) + ";" + $env:PATH
foreach ($line in (cmd /c "`"$vcvars`" >nul && set")) {
    if ($line -match '^([^=]+)=(.*)$') {
        try { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") } catch { }
    }
}
$gui = (conda env list --json | ConvertFrom-Json).envs |
    Where-Object { (Split-Path $_ -Leaf) -eq "tcad-gui" } | Select-Object -First 1
if (-not $gui) { throw "conda env 'tcad-gui' not found" }
$guibin = Join-Path $gui "Library\bin"

# 2. Fresh stage directory with the exe.
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item $exe $Out

# 3. Qt plugins + Qt DLLs (windeployqt6 knows which plugins Qt Widgets needs).
# conda-forge ships qtpaths as qtpaths6.exe, which windeployqt6 does not find
# on its own ("Unable to query qtpaths"), so it is named explicitly.
& (Join-Path $guibin "windeployqt6.exe") --qtpaths (Join-Path $guibin "qtpaths6.exe") `
    --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --no-compiler-runtime --dir $Out (Join-Path $Out "tcad_desktop.exe") | Out-Null
if ($LASTEXITCODE) { throw "windeployqt6 failed" }

# 4. The DLL closure: walk every staged PE file's imports; any import that lives
#    in tcad-gui's Library\bin (VTK, ADS, TBB, the MSVC runtime, ...) is copied
#    next to the exe, until nothing new appears. System DLLs are left alone.
function Get-Imports([string] $file) {
    $names = @()
    foreach ($l in (& dumpbin /nologo /dependents $file)) {
        if ($l -match '^\s+(\S+\.dll)\s*$') { $names += $matches[1] }
    }
    return $names
}
$queue = New-Object System.Collections.Queue
Get-ChildItem $Out -Recurse -Include *.exe, *.dll | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
$copied = 0
while ($queue.Count) {
    $f = $queue.Dequeue()
    foreach ($dll in (Get-Imports $f)) {
        $key = $dll.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        if (Test-Path (Join-Path $Out $dll)) { continue }
        $src = Join-Path $guibin $dll
        if (Test-Path $src) {
            Copy-Item $src $Out
            $copied++
            $queue.Enqueue((Join-Path $Out $dll))
        }
    }
}

# 5. Manifest: relative paths (the installed layout) or, with -DevBackend, this
#    machine's backend until P5-S2's runtime exists.
if ($DevBackend) {
    $dev = (conda env list --json | ConvertFrom-Json).envs |
        Where-Object { (Split-Path $_ -Leaf) -eq "tcad-dev" } | Select-Object -First 1
    $manifest = [ordered]@{ backend_python = (Join-Path $dev "python.exe"); backend_root = $root; runtime_bin = "" }
} else {
    $manifest = [ordered]@{ backend_python = "runtime\python.exe"; backend_root = "backend"; runtime_bin = "" }
}
$manifest | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $Out "desktop_runtime.json")

$files = Get-ChildItem $Out -Recurse -File
$mb = [math]::Round((($files | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host "staged $($files.Count) files ($mb MB, $copied DLLs from tcad-gui) -> $Out"

# 6. Optional check with a scrubbed PATH (Windows directories only).
if ($Check) {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & (Join-Path $Out "tcad_desktop.exe") --selftest $Check
    if ($LASTEXITCODE) { throw "staged --selftest failed (exit $LASTEXITCODE)" }
    Write-Host "staged --selftest passed with a scrubbed PATH"
}

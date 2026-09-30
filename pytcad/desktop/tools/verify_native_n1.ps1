# The Windows gate of N1 (NATIVE-DESKTOP-PLAN.md 27.5): the native platform layer and the minimal native window.
#   1. build the Qt-free targets (build.ps1 -NoLegacyQt)                         [skipped with -SkipBuild]
#   2. tcad_platform_core_tests.exe: the portable core (JSON-RPC session, config, settings, DPI/layout, shortcuts)
#   3. stage tcad_native.exe + its DLL import closure (NO windeployqt)
#   4. desktop\tools\check_no_qt.py <stage>            -> ZERO Qt files, plugins, references, packages
#   5. tcad_native.exe --selftest on each result (scrubbed PATH): layout/DPI, input, shortcuts, settings, file dialogs,
#      timers, cross-thread post, the REAL backend over JSON-RPC (methods, FIFO, error mapping, shutdown, restart),
#      the existing FieldScene displayed and hover-picked in the native window, and no Qt module loaded in the process
#   6. verify_native_spike.ps1 -SkipBuild            -> the N0 spike gate, kept as a permanent regression
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\verify_native_n1.ps1 -Result <2D.npz>, <3D.npz>
#       [-Out dist\TCAD-native-n1] [-Config Release] [-SkipBuild] [-BackendPython <python.exe>] [-BackendRoot <pytcad dir>]
# Exit 0 only if every step passed; <Out>\..\native-n1-report.json records each. Run by the user on Windows; nothing
# here has been run by its author.
param(
    [Parameter(Mandatory = $true)] [string[]] $Result,
    [string] $Out = "",
    [ValidateSet("Release", "RelWithDebInfo", "Debug")] [string] $Config = "Release",
    [switch] $SkipBuild,
    [string] $BackendPython = "",
    [string] $BackendRoot = ""
)
$ErrorActionPreference = "Stop"

# dumpbin (the import-closure walk below, and in verify_native_spike.ps1, which runs as a child and inherits this
# environment) is on PATH only inside an MSVC developer environment. build.ps1 sets one up only in its own child
# process, so from a plain PowerShell the staging step failed with "dumpbin is not recognized" (found 2026-09-30 on
# Windows). Import vcvars64 here, the same vswhere route build.ps1 and stage.ps1 use, unless dumpbin is already there.
if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: install Visual Studio with the C++ workload" }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio install with the MSVC x64 tools" }
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    $env:PATH = (Split-Path -Parent $vswhere) + ";" + $env:PATH
    foreach ($line in (cmd /c "`"$vcvars`" >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') {
            try { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") } catch { }
        }
    }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw "vcvars64 did not put dumpbin on PATH" }
}
$tools = $PSScriptRoot
$desktop = Split-Path -Parent $tools
$root = Split-Path -Parent $desktop
if (-not $Out) { $Out = Join-Path $root "dist\TCAD-native-n1" }
$build = Join-Path $root "build\desktop-native"
$reports = Split-Path -Parent $Out
$results = New-Object System.Collections.ArrayList
function Record([string] $name, [bool] $ok, [string] $detail) {
    [void]$results.Add([ordered]@{ step = $name; ok = $ok; detail = $detail })
    Write-Host ("{0,-4} {1,-12} {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}
function Finish {
    $ok = -not @($results | Where-Object { -not $_.ok }).Count
    New-Item -ItemType Directory -Force $reports | Out-Null
    [ordered]@{ ok = $ok; out = $Out; steps = $results } | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $reports "native-n1-report.json")
    Write-Host $(if ($ok) { "NATIVE N1: ALL STEPS PASSED" } else { "NATIVE N1: FAILED" })
    exit $(if ($ok) { 0 } else { 1 })
}

# 1. build
if (-not $SkipBuild) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $desktop "build.ps1") -Config $Config -NoLegacyQt
    Record "build" ($LASTEXITCODE -eq 0) "build.ps1 -NoLegacyQt exit $LASTEXITCODE"
    if ($LASTEXITCODE) { Finish }
}
$exe = Join-Path $build "tcad_native.exe"
if (-not (Test-Path $exe)) { Record "build" $false "$exe not found"; Finish }

# 2. the portable core's unit tests
$tests = Join-Path $build "tcad_platform_core_tests.exe"
if (Test-Path $tests) {
    $t = & $tests 2>&1 | Out-String
    Record "core-tests" ($LASTEXITCODE -eq 0) (($t.Trim() -split "`r?`n")[-1])
    if ($LASTEXITCODE) { Write-Host $t }
} else { Record "core-tests" $false "$tests not found" }

# 3. stage: the exe and every import that lives in tcad-gui's Library\bin, transitively
$gui = ((conda env list --json | Out-String) | ConvertFrom-Json).envs | Where-Object { (Split-Path $_ -Leaf) -eq "tcad-gui" } | Select-Object -First 1
if (-not $gui) { Record "stage" $false "conda env tcad-gui not found"; Finish }
$guibin = Join-Path $gui "Library\bin"
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item $exe $Out
function Get-Imports([string] $file) {
    $names = @()
    foreach ($l in (& dumpbin /nologo /dependents $file)) { if ($l -match '^\s+(\S+\.dll)\s*$') { $names += $matches[1] } }
    return $names
}
$queue = New-Object System.Collections.Queue
$queue.Enqueue((Join-Path $Out "tcad_native.exe"))
$seen = @{}; $copied = @()
while ($queue.Count) {
    $f = $queue.Dequeue()
    foreach ($dll in (Get-Imports $f)) {
        $key = $dll.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        if ($key -like "api-ms-win-*" -or $key -eq "ucrtbase.dll") { continue }   # the OS supplies the UCRT (26.9.6)
        $src = Join-Path $guibin $dll
        if ((Test-Path $src) -and -not (Test-Path (Join-Path $Out $dll))) { Copy-Item $src $Out; $copied += $dll; $queue.Enqueue((Join-Path $Out $dll)) }
    }
}
Record "stage" $true "$($copied.Count) DLLs copied next to the exe from tcad-gui (import closure of the exe)"

# 4. the hard no-Qt scanner
$py = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { (Join-Path $gui "python.exe") }
$scan = & $py (Join-Path $tools "check_no_qt.py") $Out --json (Join-Path $reports "native-n1-noqt.json") 2>&1 | Out-String
Record "no-qt" ($LASTEXITCODE -eq 0) $(if ($LASTEXITCODE -eq 0) { ($scan.Trim() -split "`r?`n")[-1] } else { $scan.Trim() })

# 5. the self-test, on each result. The backend comes from the build's desktop_runtime.json unless given.
if (-not $BackendPython -or -not $BackendRoot) {
    $manifest = Join-Path $build "desktop_runtime.json"
    if (Test-Path $manifest) {
        $m = Get-Content -Raw $manifest | ConvertFrom-Json
        if (-not $BackendPython) { $BackendPython = $m.backend_python }
        if (-not $BackendRoot) { $BackendRoot = $m.backend_root }
    }
}
if (-not $BackendPython) { Record "backend" $false "no backend interpreter: pass -BackendPython (the tcad-dev python) or build with it recorded in desktop_runtime.json"; Finish }
foreach ($r in $Result) {
    $name = [IO.Path]::GetFileNameWithoutExtension($r)
    $json = Join-Path $reports "native-n1-$name.json"
    $png = Join-Path $reports "native-n1-$name.png"
    if (Test-Path $json) { Remove-Item $json }
    $saved = @{ PATH = $env:PATH; PY = $env:TCAD_BACKEND_PYTHON; ROOT = $env:TCAD_BACKEND_ROOT }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"                 # only the staged DLLs and the OS, like the S2 self-test
    $env:TCAD_BACKEND_PYTHON = $BackendPython
    if ($BackendRoot) { $env:TCAD_BACKEND_ROOT = $BackendRoot }
    try { & (Join-Path $Out "tcad_native.exe") (Resolve-Path $r).Path --selftest $json --screenshot $png | Out-Host; $code = $LASTEXITCODE }
    finally { $env:PATH = $saved.PATH; $env:TCAD_BACKEND_PYTHON = $saved.PY; $env:TCAD_BACKEND_ROOT = $saved.ROOT }
    if (-not (Test-Path $json)) { Record "selftest:$name" $false "exit $code and no report written"; continue }
    $j = Get-Content -Raw $json | ConvertFrom-Json
    $bad = @($j.checks | Where-Object { -not $_.ok })
    Record "selftest:$name" (($code -eq 0) -and $j.ok -and ($j.qt_modules_loaded.Count -eq 0)) "exit $code; $(@($j.checks).Count) checks, $($bad.Count) failed$(if ($bad.Count) { ': ' + (($bad | ForEach-Object { $_.name + ' (' + $_.detail + ')' }) -join '; ') }); backend pid $($j.backend.pid), $($j.backend.methods) methods; png: $png"
}

# 6. the N0 spike gate, unchanged, as the permanent regression.
# NOT `powershell -File ... -Result $Result`: -File cannot pass an array, so -Result got only the first file and the
# SECOND bound positionally to the spike script's -Out, which it deletes (Remove-Item -Recurse -Force $Out) and stages
# into -- every run with two results destroyed the second result file (found 2026-09-30). -Command with each path
# quoted passes the real array; -Out is named explicitly as well.
$quoted = ($Result | ForEach-Object { "'" + ((Resolve-Path $_).Path -replace "'", "''") + "'" }) -join ","
$spikeOut = Join-Path $root "dist\TCAD-native-spike"
& powershell -ExecutionPolicy Bypass -Command "& '$((Join-Path $tools "verify_native_spike.ps1") -replace "'", "''")' -Result $quoted -Out '$($spikeOut -replace "'", "''")' -SkipBuild -Config $Config; exit `$LASTEXITCODE"
Record "spike-gate" ($LASTEXITCODE -eq 0) "verify_native_spike.ps1 exit $LASTEXITCODE"
Finish

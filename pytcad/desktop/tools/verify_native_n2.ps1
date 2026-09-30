# The Windows gate of N2 (NATIVE-DESKTOP-PLAN.md 27.7): the native UI framework core.
#   1. build the Qt-free targets (build.ps1 -NoLegacyQt)                                   [skipped with -SkipBuild]
#   2. tcad_ui_core_tests.exe: the portable core (layouts, input routing, the edit model)
#   3. tcad_ui_render_tests.exe: the render core, widget tree, input, text, edits/TSF, UI Automation, high contrast;
#      goldens on WARP compared exactly -- STALE (captured on another WARP/D2D/DirectWrite/font/ClearType setup) is
#      reported, not failed, with the re-capture command (the machine-specific-golden rule)
#   4. a soak of the D3D12/DXGI debug-layer child (device loss and recovery with the debug layers): N2d's open
#      intermittent crash must not come back (-Soak N runs, default 25)
#   5. stage tcad_ui_demo.exe + its DLL import closure from tcad-gui; check_no_qt.py -> ZERO Qt; and every import is
#      a Windows system DLL (the framework needs nothing else)
#   6. the gallery (tcad_ui_demo --gallery --warp --screenshot) from the STAGE with PATH = the Windows dirs only
#   7. verify_native_n1.ps1 -SkipBuild (N1, which runs the N0 spike gate): the regression
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\verify_native_n2.ps1 -Result <2D.npz>, <3D.npz>
#       [-Out dist\TCAD-native-n2] [-Config Release] [-SkipBuild] [-Soak 25]
# Exit 0 only if every step passed; <Out>\..\native-n2-report.json records each.
param(
    [Parameter(Mandatory = $true)] [string[]] $Result,
    [string] $Out = "",
    [ValidateSet("Release", "RelWithDebInfo", "Debug")] [string] $Config = "Release",
    [switch] $SkipBuild,
    [int] $Soak = 25
)
$ErrorActionPreference = "Stop"

# dumpbin needs an MSVC developer environment (the same vswhere route as build.ps1 / verify_native_n1.ps1)
if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: install Visual Studio with the C++ workload" }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio install with the MSVC x64 tools" }
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    $env:PATH = (Split-Path -Parent $vswhere) + ";" + $env:PATH  # vcvars64 itself runs vswhere
    foreach ($line in (cmd /c "`"$vcvars`" >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') { try { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") } catch { } }
    }
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) { throw "vcvars64 did not put dumpbin on PATH" }
}
$tools = $PSScriptRoot
$desktop = Split-Path -Parent $tools
$root = Split-Path -Parent $desktop
if (-not $Out) { $Out = Join-Path $root "dist\TCAD-native-n2" }
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
    [ordered]@{ ok = $ok; out = $Out; steps = $results } | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $reports "native-n2-report.json")
    Write-Host $(if ($ok) { "NATIVE N2: ALL STEPS PASSED" } else { "NATIVE N2: FAILED" })
    exit $(if ($ok) { 0 } else { 1 })
}

# 1. build
if (-not $SkipBuild) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $desktop "build.ps1") -Config $Config -NoLegacyQt
    Record "build" ($LASTEXITCODE -eq 0) "build.ps1 -NoLegacyQt exit $LASTEXITCODE"
    if ($LASTEXITCODE) { Finish }
}

# 2. the portable core
$core = Join-Path $build "tcad_ui_core_tests.exe"
if (Test-Path $core) {
    $t = & $core 2>&1 | Out-String
    Record "core-tests" ($LASTEXITCODE -eq 0) (($t.Trim() -split "`r?`n")[-1])
    if ($LASTEXITCODE) { Write-Host $t }
} else { Record "core-tests" $false "$core not found" }

# 3. the Windows tests, goldens included
$render = Join-Path $build "tcad_ui_render_tests.exe"
if (Test-Path $render) {
    $t = & $render 2>&1 | Out-String
    $code = $LASTEXITCODE
    $lines = $t -split "`r?`n"
    $stale = @($lines | Where-Object { $_ -match "STALE goldens" }).Count
    $matched = @($lines | Where-Object { $_ -match "\.png: 0 differing pixels" }).Count
    $detail = (($t.Trim() -split "`r?`n")[-1]) + "; goldens matched exactly: $matched" + $(if ($stale) { "; STALE (other rendering setup): $stale -- re-capture with tcad_ui_render_tests --capture" } else { "" })
    Record "render-tests" ($code -eq 0) $detail
    if ($code) { $lines | Where-Object { $_ -match "^(FAIL|  CHECK|  CRASH|CRASH)" } | ForEach-Object { Write-Host $_ } }
} else { Record "render-tests" $false "$render not found" }

# 4. the debug-layer soak
if ((Test-Path $render) -and $Soak -gt 0) {
    $bad = 0
    for ($i = 0; $i -lt $Soak; $i++) {
        $o = & $render debug_layer_reports_no_errors_and_nothing_outlives_the_device --debug-layer 2>&1 | Out-String
        if ($LASTEXITCODE) {
            $bad++
            Write-Host ("  soak run {0}: exit {1}" -f $i, $LASTEXITCODE)
            ($o -split "`r?`n") | Where-Object { $_ -match "step|CRASH|\+0x" } | Select-Object -Last 30 | ForEach-Object { Write-Host "    $_" }
        }
    }
    Record "debug-soak" ($bad -eq 0) "$bad of $Soak debug-layer device-loss runs failed"
}

# 5. stage the demo + its import closure; no Qt; only system DLLs
$demo = Join-Path $build "tcad_ui_demo.exe"
if (-not (Test-Path $demo)) { Record "stage" $false "$demo not found"; Finish }
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item $demo $Out
$gui = ((conda env list --json | Out-String) | ConvertFrom-Json).envs | Where-Object { (Split-Path $_ -Leaf) -eq "tcad-gui" } | Select-Object -First 1
$guibin = if ($gui) { Join-Path $gui "Library\bin" } else { "" }
$imports = New-Object System.Collections.ArrayList
$copied = @()
$queue = New-Object System.Collections.Queue
$queue.Enqueue((Join-Path $Out "tcad_ui_demo.exe"))
$seen = @{}
while ($queue.Count) {
    $f = $queue.Dequeue()
    foreach ($l in (& dumpbin /nologo /dependents $f)) {
        if ($l -notmatch '^\s+(\S+\.dll)\s*$') { continue }
        $dll = $matches[1]; $key = $dll.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        [void]$imports.Add($dll)
        # the MSVC runtime comes from Microsoft's vc_redist and the UCRT from the OS (decision 26.9.7): never copied
        if ($key -match '^(msvcp140|vcruntime140|concrt140|vcomp140|vccorlib140|ucrtbase|api-ms-win-)') { continue }
        $src = if ($guibin) { Join-Path $guibin $dll } else { "" }
        if ($src -and (Test-Path $src)) { Copy-Item $src $Out; $copied += $dll; $queue.Enqueue((Join-Path $Out $dll)) }
    }
}
$nonSystem = @($imports | Where-Object { -not (Test-Path (Join-Path "$env:SystemRoot\System32" $_)) -and $_ -notlike "api-ms-win-*" })
Record "stage" ($copied.Count -eq 0 -and $nonSystem.Count -eq 0) ("imports: " + ($imports -join ", ") + $(if ($copied.Count) { "; copied from tcad-gui: " + ($copied -join ", ") } else { "; nothing needed from tcad-gui" }))
$py = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } elseif ($gui) { Join-Path $gui "python.exe" } else { "" }
if ($py) {
    $scan = & $py (Join-Path $tools "check_no_qt.py") $Out --json (Join-Path $reports "native-n2-noqt.json") 2>&1 | Out-String
    Record "no-qt" ($LASTEXITCODE -eq 0) $(if ($LASTEXITCODE -eq 0) { ($scan.Trim() -split "`r?`n")[-1] } else { $scan.Trim() })
} else { Record "no-qt" $false "no python to run check_no_qt.py" }

# 6. the gallery from the stage, PATH = Windows only, on WARP
$png = Join-Path $reports "native-n2-gallery.png"
if (Test-Path $png) { Remove-Item $png }
$saved = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
try { $g = & (Join-Path $Out "tcad_ui_demo.exe") --gallery --warp --screenshot $png 2>&1 | Out-String; $code = $LASTEXITCODE }
finally { $env:PATH = $saved }
$size = if (Test-Path $png) { (Get-Item $png).Length } else { 0 }
Record "gallery" (($code -eq 0) -and $size -gt 20000) "exit $code; $png ($size bytes)"

# 7. N1 (and through it N0) as the regression; -Command so the result array reaches it intact
$quoted = ($Result | ForEach-Object { "'" + ((Resolve-Path $_).Path -replace "'", "''") + "'" }) -join ","
& powershell -ExecutionPolicy Bypass -Command "& '$((Join-Path $tools "verify_native_n1.ps1") -replace "'", "''")' -Result $quoted -SkipBuild -Config $Config; exit `$LASTEXITCODE"
Record "n1-gate" ($LASTEXITCODE -eq 0) "verify_native_n1.ps1 exit $LASTEXITCODE"
Finish

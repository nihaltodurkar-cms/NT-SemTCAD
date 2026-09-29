# The Windows gate of the first Qt-purge spike (NATIVE-DESKTOP-PLAN.md section 27.4):
#   1. build the Qt-free targets (build.ps1 -NoLegacyQt)             [skipped with -SkipBuild]
#   2. stage tcad_native_spike.exe + its DLL closure into <Out>      (NO windeployqt; the closure comes from
#      the exe's own import table, so a Qt DLL can only get in if something linked imports it)
#   3. desktop\tools\check_no_qt.py <Out>          -> must find ZERO Qt files, plugins, references, packages
#   4. run the spike on each result with --screenshot: the field view must really be displayed (image not
#      blank), hover must hit, and the process itself must have NO Qt module loaded (self-reported)
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\verify_native_spike.ps1 -Result a.npz [, b.npz ...]
#       [-Out dist\TCAD-native-spike] [-Config Release] [-SkipBuild]
# Exit 0 only if every step passed; <Out>\..\native-spike-report.json records each one. This script is run by
# the user on Windows; nothing here has been run by its author.
param(
    [Parameter(Mandatory = $true)] [string[]] $Result,
    [string] $Out = "",
    [ValidateSet("Release", "RelWithDebInfo", "Debug")] [string] $Config = "Release",
    [switch] $SkipBuild
)
$ErrorActionPreference = "Stop"
$tools = $PSScriptRoot
$desktop = Split-Path -Parent $tools
$root = Split-Path -Parent $desktop
if (-not $Out) { $Out = Join-Path $root "dist\TCAD-native-spike" }
$build = Join-Path $root "build\desktop-native"
$results = New-Object System.Collections.ArrayList
function Record([string] $name, [bool] $ok, [string] $detail) {
    [void]$results.Add([ordered]@{ step = $name; ok = $ok; detail = $detail })
    Write-Host ("{0,-4} {1,-10} {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}
function Finish {
    $ok = -not @($results | Where-Object { -not $_.ok }).Count
    New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
    [ordered]@{ ok = $ok; out = $Out; steps = $results } | ConvertTo-Json -Depth 6 |
        Set-Content -Encoding utf8 (Join-Path (Split-Path -Parent $Out) "native-spike-report.json")
    Write-Host $(if ($ok) { "NATIVE SPIKE: ALL STEPS PASSED" } else { "NATIVE SPIKE: FAILED" })
    exit $(if ($ok) { 0 } else { 1 })
}

# 1. build
if (-not $SkipBuild) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $desktop "build.ps1") -Config $Config -NoLegacyQt
    Record "build" ($LASTEXITCODE -eq 0) "build.ps1 -NoLegacyQt exit $LASTEXITCODE"
    if ($LASTEXITCODE) { Finish }
}
$exe = Join-Path $build "tcad_native_spike.exe"
if (-not (Test-Path $exe)) { Record "build" $false "$exe not found"; Finish }

# 2. stage: the exe, then every import that lives in tcad-gui's Library\bin, transitively
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
$queue.Enqueue((Join-Path $Out "tcad_native_spike.exe"))
$seen = @{}; $copied = @()
while ($queue.Count) {
    $f = $queue.Dequeue()
    foreach ($dll in (Get-Imports $f)) {
        $key = $dll.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        if ($key -like "api-ms-win-*" -or $key -eq "ucrtbase.dll") { continue }   # the OS supplies the UCRT (26.9.6)
        $src = Join-Path $guibin $dll
        if ((Test-Path $src) -and -not (Test-Path (Join-Path $Out $dll))) {
            Copy-Item $src $Out; $copied += $dll; $queue.Enqueue((Join-Path $Out $dll))
        }
    }
}
Record "stage" $true "$($copied.Count) DLLs copied next to the exe from tcad-gui (import closure of the exe)"

# 3. the hard no-Qt scanner
$py = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { (Join-Path $gui "python.exe") }
$scan = & $py (Join-Path $tools "check_no_qt.py") $Out --json (Join-Path (Split-Path -Parent $Out) "native-spike-noqt.json") 2>&1 | Out-String
Record "no-qt" ($LASTEXITCODE -eq 0) $(if ($LASTEXITCODE -eq 0) { ($scan.Trim() -split "`r?`n")[-1] } else { $scan.Trim() })

# 4. run: display + hover + no Qt module loaded in the process
$i = 0
foreach ($r in $Result) {
    $i++
    $name = [IO.Path]::GetFileNameWithoutExtension($r)
    $png = Join-Path (Split-Path -Parent $Out) "native-spike-$name.png"
    $json = Join-Path (Split-Path -Parent $Out) "native-spike-$name.json"
    $saved = $env:PATH
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"        # only the staged DLLs and the OS: like the S2 self-test
    try { & (Join-Path $Out "tcad_native_spike.exe") (Resolve-Path $r).Path --screenshot $png --json $json | Out-Null; $code = $LASTEXITCODE }
    finally { $env:PATH = $saved }
    if (-not (Test-Path $json)) { Record "run:$name" $false "exit $code and no report written"; continue }
    $j = Get-Content -Raw $json | ConvertFrom-Json
    $detail = "exit $code; displayed=$($j.displayed) ($($j.image.distinct_colours) colours, $([math]::Round($j.image.non_background_fraction, 3)) non-bg); hover hit=$($j.hover.hit); qt modules loaded: $(@($j.qt_modules_loaded).Count); png: $png"
    Record "run:$name" (($code -eq 0) -and $j.ok -and $j.displayed -and ($j.qt_modules_loaded.Count -eq 0)) $detail
}
Finish

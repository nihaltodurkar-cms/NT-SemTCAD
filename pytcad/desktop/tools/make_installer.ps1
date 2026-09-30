# Build the Windows installer from a staged app (NATIVE-DESKTOP-PLAN.md section 26.3, P5-S3):
# dist\TCAD (desktop\tools\stage.ps1, the full S2 staging, NOT -DevBackend) ->
# dist\installer\TCAD-<version>-unsigned-setup.exe, via Inno Setup (decision 26.4-1).
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\make_installer.ps1 `
#       [-Stage <dir>] [-OutDir <dir>] [-Iscc <path\ISCC.exe>] [-VcRedist <vc_redist.x64.exe>] [-Signed] [-WriteSandboxConfig]
#
# -Stage: the stage.ps1 output (default dist\TCAD). Refused unless it is a complete, RELATIVE
#   layout: a -DevBackend stage would ship this machine's absolute interpreter path; a stage
#   holding gmsh/tetgen/scikit-image would put GPL/AGPL code in the base installer (26.4-4).
# -VcRedist: Microsoft's vc_redist.x64.exe to embed (default: the newest one in the Visual Studio found by
#   vswhere, VC\Redist\MSVC\*). REQUIRED when the stage was made with -ExcludeMsvcRuntime (its
#   licenses\excluded-packages.json lists vc14_runtime): the installer then runs it when the machine's runtime
#   is older than that package's version (decision NATIVE-DESKTOP-PLAN.md 26.9.7). Refused unless it is
#   Authenticode-valid and at least that version.
# -Signed: drop the "-unsigned" name suffix. Only S6 (signtool) should pass it.
# -WriteSandboxConfig: also write <OutDir>\tcad-install-test.wsb, a Windows Sandbox config that
#   runs desktop\tools\verify_install.ps1 (install -> run -> uninstall -> diff) on a clean machine.
#
# Prerequisite (once): Inno Setup 6.3 or newer (https://jrsoftware.org/isdl.php), installed like
# any Windows program -- NOT into a conda env.
param(
    [string] $Stage = "",
    [string] $OutDir = "",
    [string] $Iscc = "",
    [string] $VcRedist = "",
    [switch] $Signed,
    [switch] $WriteSandboxConfig
)
$ErrorActionPreference = "Stop"
$desktop = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $desktop

# The version the app itself is built with: project(tcad_desktop VERSION x.y.z ...).
function Get-DesktopVersion([string] $cmakeLists) {
    $m = [regex]::Match((Get-Content -Raw $cmakeLists), 'project\(\s*tcad_desktop\s+VERSION\s+(\d+\.\d+\.\d+)')
    if (-not $m.Success) { throw "no 'project(tcad_desktop VERSION x.y.z' in $cmakeLists" }
    return $m.Groups[1].Value
}

# The problems that make a stage unfit to install (empty list = fit).
function Get-StageProblems([string] $stage) {
    $p = @()
    foreach ($rel in @("tcad_desktop.exe", "desktop_runtime.json", "runtime\python.exe",
                       "backend\backend_service\__main__.py", "backend\gui\services\solver_runner.py",
                       "licenses\THIRD_PARTY_NOTICES.txt", "licenses\manifest.json")) {
        if (-not (Test-Path (Join-Path $stage $rel))) { $p += "missing $rel" }
    }
    if (-not (Get-ChildItem (Join-Path $stage "backend\pytcad") -Filter "_core*.pyd" -ErrorAction SilentlyContinue)) {
        $p += "missing backend\pytcad\_core*.pyd (the compiled extension)"
    }
    $manifest = Join-Path $stage "desktop_runtime.json"
    if (Test-Path $manifest) {
        try {
            $j = Get-Content -Raw $manifest | ConvertFrom-Json
            foreach ($k in @("backend_python", "backend_root")) {
                $v = $j.$k
                if (-not $v) { $p += "desktop_runtime.json has no $k" }
                elseif ([System.IO.Path]::IsPathRooted($v)) { $p += "desktop_runtime.json $k is absolute ($v): a -DevBackend stage cannot be installed" }
            }
        } catch { $p += "desktop_runtime.json is not valid JSON" }
    }
    # P5-S4: the bundle must describe THIS stage (every package and DLL covered, every text intact,
    # nothing strong-copyleft in a base bundle). gen_licenses.py --verify-bundle is stdlib-only.
    $py = Join-Path $stage "runtime\python.exe"
    if ((Test-Path $py) -and (Test-Path (Join-Path $stage "licenses\manifest.json"))) {
        $out = & $py (Join-Path $PSScriptRoot "gen_licenses.py") --verify-bundle $stage 2>&1
        if ($LASTEXITCODE) { $p += ("licence bundle does not match the stage:`n      " + (($out | Out-String).Trim() -replace "`r?`n", "`n      ")) }
    }
    # decision 26.4-4: the GPL/AGPL add-on packages are not in the base installer
    $sp = Join-Path $stage "runtime\Lib\site-packages"
    foreach ($pkg in @("gmsh", "skimage", "tetgen")) {
        if (Test-Path (Join-Path $sp $pkg)) { $p += "runtime contains $pkg (the base installer must not ship it, decision 26.4-4)" }
    }
    return $p
}

# A Windows Sandbox config: read-only installer + tools folders, a writable log folder, no network,
# and verify_install.ps1 started at logon.
function New-SandboxConfig([string] $installerDir, [string] $installerName, [string] $toolsDir, [string] $logDir) {
    $x = @(
        "<Configuration>",
        "  <VGpu>Enable</VGpu>",
        "  <Networking>Disable</Networking>",
        "  <MappedFolders>",
        "    <MappedFolder><HostFolder>$installerDir</HostFolder><SandboxFolder>C:\installer</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>",
        "    <MappedFolder><HostFolder>$toolsDir</HostFolder><SandboxFolder>C:\tools</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>",
        "    <MappedFolder><HostFolder>$logDir</HostFolder><SandboxFolder>C:\log</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>",
        "  </MappedFolders>",
        "  <LogonCommand>",
        "    <Command>powershell -ExecutionPolicy Bypass -File C:\tools\verify_install.ps1 -Installer C:\installer\$installerName -ToolsDir C:\tools -LogDir C:\log</Command>",
        "  </LogonCommand>",
        "</Configuration>")
    return ($x -join "`r`n")
}

# The MSVC runtime version the stage relies on the machine for: the vc14_runtime record stage.ps1
# -ExcludeMsvcRuntime wrote into licenses\excluded-packages.json. $null when the runtime was not excluded.
function Get-RequiredVcRuntime([string] $stage) {
    $f = Join-Path $stage "licenses\excluded-packages.json"
    if (-not (Test-Path $f)) { return $null }
    # assigned first: Windows PowerShell 5.1's ConvertFrom-Json emits a JSON array as ONE object, which @(...)
    # would wrap again (then $r.version is every record's version at once); a variable enumerates it
    $recs = Get-Content -Raw $f | ConvertFrom-Json
    foreach ($r in $recs) {
        if ($r.package -eq "vc14_runtime") { return [version]$r.version }
    }
    return $null
}

# vc_redist.x64.exe: the explicit path, else the highest-versioned one in the latest Visual Studio.
function Find-VcRedist([string] $explicit) {
    if ($explicit) {
        if (-not (Test-Path $explicit)) { throw "-VcRedist $explicit does not exist" }
        return (Resolve-Path $explicit).Path
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: pass -VcRedist <vc_redist.x64.exe>" }
    $vs = & $vswhere -latest -products * -property installationPath
    $c = @(Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Recurse -Filter "vc_redist.x64.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.VersionInfo.FileVersion } -Descending)
    if (-not $c.Count) { throw "no vc_redist.x64.exe under $vs\VC\Redist\MSVC: pass -VcRedist" }
    return $c[0].FullName
}

function Find-Iscc([string] $explicit) {
    if ($explicit) {
        if (-not (Test-Path $explicit)) { throw "-Iscc $explicit does not exist" }
        return $explicit
    }
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($base in @($env:LOCALAPPDATA + "\Programs", ${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $base) { continue }
        $c = Join-Path $base "Inno Setup 6\ISCC.exe"
        if (Test-Path $c) { return $c }
    }
    throw "Inno Setup's ISCC.exe not found. Install Inno Setup 6.3+ (https://jrsoftware.org/isdl.php), not into a conda env, or pass -Iscc."
}

# -- main (skipped when the functions above are dot-sourced for testing) -------------------
if ($MyInvocation.InvocationName -eq ".") { return }

if (-not $Stage) { $Stage = Join-Path $root "dist\TCAD" }
if (-not $OutDir) { $OutDir = Join-Path $root "dist\installer" }
$Stage = (Resolve-Path $Stage).Path
$version = Get-DesktopVersion (Join-Path $desktop "CMakeLists.txt")
$problems = Get-StageProblems $Stage
if ($problems.Count) {
    throw ("the stage at $Stage cannot be installed (run stage.ps1 without -DevBackend/-NoRuntime first):`n  - " + ($problems -join "`n  - "))
}
$vcDefines = @()
$vcNeed = Get-RequiredVcRuntime $Stage
if ($vcNeed) {
    $redist = Find-VcRedist $VcRedist
    $have = [version](Get-Item $redist).VersionInfo.FileVersion
    $sig = (Get-AuthenticodeSignature $redist).Status
    if ($sig -ne "Valid") { throw "$redist : Authenticode status $sig, not Valid" }
    if ($have -lt $vcNeed) { throw "$redist is $have; the stage needs the MSVC runtime $vcNeed or newer" }
    $vcDefines = @("/DVcRedist=$redist", "/DVcMajor=$($vcNeed.Major)", "/DVcMinor=$($vcNeed.Minor)", "/DVcBld=$($vcNeed.Build)")
    Write-Host "embedding $redist ($have); runs at install when the machine's MSVC runtime is older than $vcNeed"
} elseif ($VcRedist) {
    throw "-VcRedist given, but the stage ships its own MSVC runtime (not staged with -ExcludeMsvcRuntime)"
}
$suffix = if ($Signed) { "" } else { "-unsigned" }
$iscc = Find-Iscc $Iscc
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

& $iscc "/DAppVersion=$version" "/DStageDir=$Stage" "/DOutDir=$OutDir" "/DNameSuffix=$suffix" @vcDefines (Join-Path $desktop "installer\tcad.iss")
if ($LASTEXITCODE) { throw "ISCC failed (exit $LASTEXITCODE)" }

$name = "TCAD-$version$suffix-setup.exe"
$exe = Join-Path $OutDir $name
if (-not (Test-Path $exe)) { throw "ISCC reported success but $exe does not exist" }
$mb = [math]::Round(((Get-Item $exe).Length / 1MB), 1)
$sha = (Get-FileHash -Algorithm SHA256 $exe).Hash
Write-Host "built $exe ($mb MB)"
Write-Host "  sha256 $sha"

if ($WriteSandboxConfig) {
    $log = Join-Path $OutDir "sandbox-log"
    New-Item -ItemType Directory -Force $log | Out-Null
    $wsb = Join-Path $OutDir "tcad-install-test.wsb"
    [System.IO.File]::WriteAllText($wsb, (New-SandboxConfig $OutDir $name $PSScriptRoot $log), (New-Object System.Text.UTF8Encoding $false))
    Write-Host "sandbox config: $wsb   (double-click it; results appear in $log\verify_install.json)"
}

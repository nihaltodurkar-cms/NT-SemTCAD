# The S3 gate (NATIVE-DESKTOP-PLAN.md section 26.3): install -> run the three examples ->
# uninstall, on a machine with NO conda envs, diffing the file list and registry before and
# after. Meant for Windows Sandbox (make_installer.ps1 -WriteSandboxConfig writes a .wsb that
# runs it), but works on any clean Windows user account.
#
#   powershell -ExecutionPolicy Bypass -File verify_install.ps1 -Installer <TCAD-...-setup.exe> `
#       [-ToolsDir <dir holding check_runtime.py>] [-LogDir <dir>] [-KeepInstalled]
#
# Exit 0 only if every check passed; <LogDir>\verify_install.json records each one. The checks:
#   install     the setup exits 0, silently, per-user (admin only for the MSVC runtime, when the machine lacks it)
#   vcruntime   (a stage made with -ExcludeMsvcRuntime) the machine-wide MSVC runtime is installed, at least
#               the version the stage excluded -- the installer's embedded vc_redist put it there
#   layout      exe, runtime\python.exe, backend\pytcad\_core*.pyd, manifest, Start menu shortcut,
#               Add/Remove Programs entry, "Open with" keys -- and NO .json/.npz default association
#   runtime     check_runtime.py on the INSTALLED runtime + backend: import closure, PARDISO, the
#               backend handshake, and diode_1d / mosfet_2d / resistor_3d solved and validated
#   selftest    the installed tcad_desktop.exe --selftest on each example result, with PATH = the
#               Windows directories only
#   uninstall   the uninstaller exits 0 and finishes
#   clean       nothing of the install is left: file list under %LOCALAPPDATA%\Programs and the
#               Start menu, and the registry keys, are identical to the "before" snapshot
#   userdata    a sentinel project file in Documents, and the app's own settings/runs folders,
#               were NOT deleted
param(
    [Parameter(Mandatory = $true)] [string] $Installer,
    [string] $ToolsDir = $PSScriptRoot,
    [string] $LogDir = "",
    [switch] $KeepInstalled
)
$ErrorActionPreference = "Stop"

# Every file and directory under $root, as sorted relative paths (empty if $root is absent).
function Get-FileSet([string] $root) {
    if (-not (Test-Path $root)) { return @() }
    $base = (Resolve-Path $root).Path.TrimEnd('\', '/')
    return @(Get-ChildItem $root -Recurse -Force -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName.Substring($base.Length).TrimStart('\', '/') } | Sort-Object)
}

# What appeared and what disappeared between two snapshots. Either snapshot may be EMPTY, and an
# empty array returned by a function reaches the caller as $null (PowerShell unrolls it): after a
# clean uninstall %LOCALAPPDATA%\Programs is empty, so the "after" snapshot is $null. Piping a bare
# $null sends ONE null object down the pipeline, and ContainsKey($null) throws "Key cannot be null"
# (found in Windows Sandbox). So: foreach (zero iterations on $null), and never look up a null.
function Compare-Sets([string[]] $before, [string[]] $after) {
    $b = @{}; foreach ($x in $before) { if ($null -ne $x) { $b[$x] = $true } }
    $a = @{}; foreach ($x in $after) { if ($null -ne $x) { $a[$x] = $true } }
    $added = New-Object System.Collections.ArrayList
    foreach ($x in $after) { if (($null -ne $x) -and -not $b.ContainsKey($x)) { [void]$added.Add($x) } }
    $removed = New-Object System.Collections.ArrayList
    foreach ($x in $before) { if (($null -ne $x) -and -not $a.ContainsKey($x)) { [void]$removed.Add($x) } }
    return [ordered]@{ added = @($added); removed = @($removed) }
}

$results = New-Object System.Collections.ArrayList
function Record([string] $name, [bool] $ok, [string] $detail) {
    [void]$results.Add([ordered]@{ check = $name; ok = $ok; detail = $detail })
    Write-Host ("{0,-4} {1,-10} {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)
}

# -- main (skipped when the functions above are dot-sourced for testing) --------------------
if ($MyInvocation.InvocationName -eq ".") { return }

if (-not $LogDir) { $LogDir = Join-Path $env:TEMP "tcad-verify-install" }
New-Item -ItemType Directory -Force $LogDir | Out-Null
$app = Join-Path $env:LOCALAPPDATA "Programs\TCAD"
$programs = Join-Path $env:LOCALAPPDATA "Programs"
$startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$regRoots = @("HKCU:\Software\Classes\Applications", "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
              "HKCU:\Software\Classes\.json", "HKCU:\Software\Classes\.npz")
function Get-RegSet { foreach ($r in $regRoots) { if (Test-Path $r) { Get-ChildItem $r | ForEach-Object { $_.PSPath -replace '^.*::', '' } } } }
$scratch = Join-Path $LogDir "check"
try {
    $assocBefore = @{}
    foreach ($e in @(".json", ".npz")) { $assocBefore[$e] = (Test-Path "HKCU:\Software\Classes\$e") }
    $before = @{ files = (Get-FileSet $programs); menu = (Get-FileSet $startMenu); reg = @(Get-RegSet | Sort-Object) }
    $sentinel = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "tcad_sentinel_project.json"
    Set-Content -Path $sentinel -Value '{"sentinel": true}'
    $settingsDir = Join-Path $env:APPDATA "PyTCAD"
    $runsDir = Join-Path $env:LOCALAPPDATA "PyTCAD\runs"
    New-Item -ItemType Directory -Force $settingsDir, $runsDir | Out-Null
    Set-Content -Path (Join-Path $settingsDir "sentinel.txt") -Value "keep"
    Set-Content -Path (Join-Path $runsDir "sentinel.txt") -Value "keep"

    # install
    $p = Start-Process -FilePath $Installer -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGS", "/NORESTART", "/SP-", "/LOG=$LogDir\install.log") -Wait -PassThru
    Record "install" ($p.ExitCode -eq 0) "setup exit code $($p.ExitCode)"

    # layout
    $need = @("tcad_desktop.exe", "desktop_runtime.json", "runtime\python.exe", "backend\backend_service\__main__.py",
              "licenses\THIRD_PARTY_NOTICES.txt", "licenses\manifest.json")
    $missing = @($need | Where-Object { -not (Test-Path (Join-Path $app $_)) })
    if (-not (Get-ChildItem (Join-Path $app "backend\pytcad") -Filter "_core*.pyd" -ErrorAction SilentlyContinue)) { $missing += "backend\pytcad\_core*.pyd" }
    $lnk = Get-ChildItem $startMenu -Filter "PyTCAD Desktop*.lnk" -Recurse -ErrorAction SilentlyContinue
    if (-not $lnk) { $missing += "Start menu shortcut" }
    if (-not (Get-ChildItem "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall" | Where-Object { $_.PSChildName -like "{575F975D-8CB7-490C-8FAA-7A4870CC2EE3}*" })) { $missing += "Add/Remove Programs entry" }
    if (-not (Test-Path "HKCU:\Software\Classes\Applications\tcad_desktop.exe\shell\open\command")) { $missing += "Open-with command key" }
    # an association key that did not exist before the install and does now would be a default takeover
    $hijack = @(".json", ".npz" | Where-Object { (-not $assocBefore[$_]) -and (Test-Path "HKCU:\Software\Classes\$_") })
    if ($hijack) { $missing += "UNEXPECTED default association key(s): $($hijack -join ', ')" }
    Record "layout" ($missing.Count -eq 0) $(if ($missing.Count) { "problems: " + ($missing -join "; ") } else { "all present, no default association" })

    # vcruntime (26.9.7): a stage without its own MSVC runtime needs the machine's, which the installer's
    # embedded vc_redist provides -- at least the vc14_runtime version the stage excluded
    $exf = Join-Path $app "licenses\excluded-packages.json"
    $vcNeed = $null
    if (Test-Path $exf) {
        $recs = Get-Content -Raw $exf | ConvertFrom-Json          # not @(...): PS 5.1 emits a JSON array as one object
        foreach ($r in $recs) { if ($r.package -eq "vc14_runtime") { $vcNeed = [version]$r.version } }
    }
    if ($vcNeed) {
        $k = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" -ErrorAction SilentlyContinue
        $vcHave = if ($k -and $k.Installed -eq 1) { [version]"$($k.Major).$($k.Minor).$($k.Bld)" } else { $null }
        $dll = Test-Path (Join-Path $env:SystemRoot "System32\vcruntime140.dll")
        Record "vcruntime" ($vcHave -and ($vcHave -ge $vcNeed) -and $dll) "machine MSVC runtime $(if ($vcHave) { $vcHave } else { 'NOT installed' }), stage needs $vcNeed; System32\vcruntime140.dll $(if ($dll) { 'present' } else { 'MISSING' })"
    }

    # runtime: the S2 gates, on the installed copy. --scratch keeps the example results.
    $py = Join-Path $app "runtime\python.exe"
    New-Item -ItemType Directory -Force $scratch | Out-Null
    & $py (Join-Path $ToolsDir "check_runtime.py") --backend (Join-Path $app "backend") --runtime (Join-Path $app "runtime") --scratch $scratch *>&1 |
        Tee-Object -FilePath (Join-Path $LogDir "check_runtime.log") | Out-Host
    Record "runtime" ($LASTEXITCODE -eq 0) "check_runtime.py exit $LASTEXITCODE (log: check_runtime.log)"

    # licenses (P5-S4): the INSTALLED bundle still matches the installed files -- every package and DLL
    # covered, every licence text present and unmodified
    $lic = & $py (Join-Path $ToolsDir "gen_licenses.py") --verify-bundle $app *>&1
    $licOut = ($lic | Out-String).Trim()
    Set-Content -Path (Join-Path $LogDir "licenses.log") -Value $licOut
    Record "licenses" ($LASTEXITCODE -eq 0) $(if ($LASTEXITCODE -eq 0) { "installed licence bundle verified" } else { $licOut })

    # selftest: the installed app, PATH = Windows only
    $saved = $env:PATH
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    try {
        foreach ($n in @("diode_1d", "mosfet_2d", "resistor_3d")) {
            $npz = Join-Path $scratch "$n.npz"
            if (-not (Test-Path $npz)) { Record "selftest" $false "$n.npz was not produced"; continue }
            & (Join-Path $app "tcad_desktop.exe") --selftest $npz | Out-Null
            Record "selftest" ($LASTEXITCODE -eq 0) "$n --selftest exit $LASTEXITCODE"
        }
    } finally { $env:PATH = $saved }

    # uninstall (Inno's uninstaller re-launches itself from %TEMP% and returns at once: wait for the folder)
    if (-not $KeepInstalled) {
        $unins = Get-ChildItem $app -Filter "unins*.exe" | Select-Object -First 1
        if (-not $unins) { Record "uninstall" $false "no unins*.exe in $app" }
        else {
            $u = Start-Process -FilePath $unins.FullName -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGS", "/NORESTART") -Wait -PassThru
            $deadline = (Get-Date).AddSeconds(180)
            while ((Test-Path $app) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
            Record "uninstall" (($u.ExitCode -eq 0) -and -not (Test-Path $app)) "uninstaller exit $($u.ExitCode); $app $(if (Test-Path $app) { 'STILL EXISTS' } else { 'removed' })"
        }

        # clean: identical to the "before" snapshots
        $d1 = Compare-Sets $before.files (Get-FileSet $programs)
        $d2 = Compare-Sets $before.menu (Get-FileSet $startMenu)
        $d3 = Compare-Sets $before.reg (@(Get-RegSet | Sort-Object))
        $left = @($d1.added + $d2.added + $d3.added)
        $gone = @($d1.removed + $d2.removed + $d3.removed)
        Record "clean" (($left.Count -eq 0) -and ($gone.Count -eq 0)) $(if ($left.Count -or $gone.Count) { "left behind: " + ($left | Select-Object -First 10 | Out-String).Trim() + " | vanished: " + ($gone | Select-Object -First 10 | Out-String).Trim() } else { "file lists and registry identical to before" })

        # userdata
        $kept = @(@($sentinel, (Join-Path $settingsDir "sentinel.txt"), (Join-Path $runsDir "sentinel.txt")) | Where-Object { Test-Path $_ })
        Record "userdata" ($kept.Count -eq 3) "$($kept.Count) of 3 user files survived the uninstall"
    }
} catch {
    Record "script" $false ("$($_.Exception.Message) at line $($_.InvocationInfo.ScriptLineNumber)")
}
$ok = -not @($results | Where-Object { -not $_.ok }).Count
[ordered]@{ ok = $ok; installer = $Installer; checks = $results } | ConvertTo-Json -Depth 5 |
    Set-Content -Encoding utf8 (Join-Path $LogDir "verify_install.json")
Write-Host $(if ($ok) { "ALL CHECKS PASSED" } else { "FAILED" })
exit $(if ($ok) { 0 } else { 1 })

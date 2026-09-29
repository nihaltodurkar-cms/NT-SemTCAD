# READ-ONLY evidence collector for the S4 licence decisions (NATIVE-DESKTOP-PLAN.md 26.9.3).
# It changes nothing: no policy, no stage, no environment. It writes only <Out>\s4-evidence.json and .txt,
# and (section A) starts and then stops the staged python.exe and tcad_desktop.exe to see which DLLs the
# operating system actually loads for them.
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\s4_evidence.ps1 [-Stage dist\TCAD] [-GuiEnv <prefix>] [-Out build\s4-evidence]
#
# A. ucrt: exactly which Universal CRT files are staged, where, and -- the decisive question -- whether the
#    OS loads them or its own copy (Microsoft: on Windows 10/11 the system UCRT is always used).
# B. MSVC runtime (vc14_runtime, vcomp14): every staged runtime DLL, its hash/version/signature, and whether the
#    SAME file (name, then hash) is in YOUR Visual Studio installation's Redist folder / redist.txt. This is a
#    check against your installation. The authoritative REDIST list is the online one that the "Distributable
#    Code" section of the licence terms for your Visual Studio edition points to: compare the printed names to it.
#    NOTHING here reads or relies on the conda package's LICENSE.TXT.
# C. Exact package name / version / build / checksum / licence-text hashes for every package the S4 drafts name.
param(
    [string] $Stage = "",
    [string] $GuiEnv = "",
    [string] $Out = ""
)
$ErrorActionPreference = "Stop"
$desktop = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $desktop
$sep = [System.IO.Path]::DirectorySeparatorChar

# -- pure helpers (dot-sourceable) ------------------------------------------------------------------

# The conda-meta records of the named packages in a prefix (name, version, build, licence, checksums, files).
function Get-CondaMeta([string] $prefix, [string[]] $names) {
    $found = @()
    $dir = Join-Path $prefix "conda-meta"
    if (-not (Test-Path $dir)) { return $found }
    foreach ($f in (Get-ChildItem $dir -Filter "*.json" -ErrorAction SilentlyContinue)) {
        try { $m = Get-Content -Raw $f.FullName | ConvertFrom-Json } catch { continue }
        if ($m.name -and ($names -contains $m.name)) {
            $files = @()
            if ($m.files) { $files = @($m.files) }
            $found += [pscustomobject]@{
                prefix = $prefix; name = $m.name; version = "$($m.version)"; build = "$($m.build)"
                channel = "$($m.channel)"; license = "$($m.license)"; sha256 = "$($m.sha256)"; md5 = "$($m.md5)"
                url = "$($m.url)"; extracted_package_dir = "$($m.extracted_package_dir)"; files = $files
            }
        }
    }
    return $found
}

# Size, SHA-256, version resources and Authenticode status of one file ("n/a" where the OS has no such API).
function Get-FileFacts([string] $path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return [pscustomobject]@{ path = $path; exists = $false } }
    $item = Get-Item -LiteralPath $path
    $sig = "n/a"; $signer = "n/a"
    try {
        $s = Get-AuthenticodeSignature -LiteralPath $path
        $sig = "$($s.Status)"
        if ($s.SignerCertificate) { $signer = "$($s.SignerCertificate.Subject)" }
    } catch { }
    $vi = $item.VersionInfo
    return [pscustomobject]@{
        path = $path; exists = $true; bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        fileVersion = "$($vi.FileVersion)"; company = "$($vi.CompanyName)"; signature = $sig; signer = $signer
    }
}

# Every distinct *.dll name mentioned in a Redist.txt-style file, lower-cased.
function Get-RedistNames([string[]] $lines) {
    $names = @{}
    foreach ($l in $lines) {
        foreach ($m in [regex]::Matches($l, '[A-Za-z0-9_.\-]+\.dll')) { $names[$m.Value.ToLowerInvariant()] = $true }
    }
    return @($names.Keys | Sort-Object)
}

# For each staged runtime DLL: is the same NAME, and the same FILE (hash), in the Visual Studio redist copy?
function Compare-Redist($staged, $vsFiles, [string[]] $redistTxtNames) {
    $byName = @{}
    foreach ($v in $vsFiles) { $byName[[System.IO.Path]::GetFileName($v.path).ToLowerInvariant()] = $v }
    $rows = @()
    foreach ($s in $staged) {
        $n = [System.IO.Path]::GetFileName($s.path).ToLowerInvariant()
        $v = $byName[$n]
        $rows += [pscustomobject]@{
            file = $n; stagedAt = $s.path; version = $s.fileVersion; signature = $s.signature
            inVsRedistFolder = [bool]$v
            sameHashAsVs = [bool]($v -and $v.sha256 -eq $s.sha256)
            sameVersionAsVs = [bool]($v -and $v.fileVersion -eq $s.fileVersion)
            inRedistTxt = $(if ($redistTxtNames.Count) { [bool]($redistTxtNames -contains $n) } else { $null })
        }
    }
    return $rows
}

function ConvertTo-Native([string] $prefix, [string] $rel) {
    return [System.IO.Path]::Combine($prefix, $rel.Replace('/', $sep).Replace('\', $sep))
}

# -- main (skipped when the functions above are dot-sourced for testing) -----------------------------
if ($MyInvocation.InvocationName -eq ".") { return }

if (-not $Stage) { $Stage = Join-Path $root "dist\TCAD" }
if (-not $Out) { $Out = Join-Path $root "build\s4-evidence" }
$Stage = (Resolve-Path $Stage).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$isWin = ($env:OS -eq "Windows_NT")
$rt = Join-Path $Stage "runtime"
if (-not $GuiEnv -and $isWin) {
    try {
        $GuiEnv = ((conda env list --json | Out-String) | ConvertFrom-Json).envs |
            Where-Object { (Split-Path $_ -Leaf) -eq "tcad-gui" } | Select-Object -First 1
    } catch { $GuiEnv = "" }
}
$report = [ordered]@{ generated_for = $Stage; guiEnv = $GuiEnv }

# ---- A. ucrt -------------------------------------------------------------------------------------
$ucrtMeta = @()
foreach ($p in @($rt, $GuiEnv)) { if ($p) { $ucrtMeta += Get-CondaMeta $p @("ucrt") } }
$A = [ordered]@{ packages = @(); runtimeFiles = @(); appDirDlls = @(); loaded = @(); os = [ordered]@{} }
foreach ($m in $ucrtMeta) { $A.packages += [pscustomobject]@{ prefix = $m.prefix; version = $m.version; build = $m.build; files = $m.files.Count } }
$present = 0; $absent = 0; $bytes = 0
foreach ($m in ($ucrtMeta | Where-Object { $_.prefix -eq $rt })) {
    foreach ($f in $m.files) {
        $facts = Get-FileFacts (ConvertTo-Native $rt $f)
        if ($facts.exists) { $present++; $bytes += $facts.bytes } else { $absent++ }
        $A.runtimeFiles += [pscustomobject]@{ rel = $f; present = $facts.exists; bytes = $facts.bytes; sha256 = $facts.sha256; version = $facts.fileVersion }
    }
}
$A.runtimeSummary = "listed by conda-meta: $($present + $absent); present in the stage: $present; absent: $absent; bytes present: $bytes"
foreach ($d in (Get-ChildItem $Stage -Filter "*.dll" -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^(api-ms-win-.*|ucrtbased?)\.dll$' })) {
    $A.appDirDlls += Get-FileFacts $d.FullName
}
if ($isWin) {
    try {
        $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
        $A.os = [ordered]@{ product = "$($cv.ProductName)"; displayVersion = "$($cv.DisplayVersion)"; build = "$($cv.CurrentBuild).$($cv.UBR)" }
        $A.os.systemUcrtbase = Get-FileFacts (Join-Path $env:SystemRoot "System32\ucrtbase.dll")
    } catch { $A.os = @{ error = "$_" } }
    # Which DLLs does the OS load for the staged programs? Start, sample the module list, stop.
    $targets = @(
        @{ label = "runtime\python.exe"; exe = (Join-Path $rt "python.exe"); args = @("-c", "import time; time.sleep(12)") },
        @{ label = "tcad_desktop.exe"; exe = (Join-Path $Stage "tcad_desktop.exe"); args = @("--settings", (Join-Path $Out "ephemeral.ini")) })
    foreach ($t in $targets) {
        if (-not (Test-Path $t.exe)) { $A.loaded += [pscustomobject]@{ program = $t.label; note = "not found" }; continue }
        $proc = $null
        try {
            $proc = Start-Process -FilePath $t.exe -ArgumentList $t.args -PassThru -WindowStyle Minimized
            Start-Sleep -Seconds 6
            $mods = @($proc.Modules | Where-Object { $_.ModuleName -match '^(ucrtbase|api-ms-win-crt|vcruntime|msvcp|concrt|vccorlib|vcamp|vcomp)' })
            foreach ($mod in $mods) {
                $A.loaded += [pscustomobject]@{ program = $t.label; module = $mod.ModuleName; path = $mod.FileName
                    fromStage = $mod.FileName.StartsWith($Stage, [System.StringComparison]::OrdinalIgnoreCase) }
            }
            if (-not $mods.Count) { $A.loaded += [pscustomobject]@{ program = $t.label; note = "no matching modules read" } }
        } catch { $A.loaded += [pscustomobject]@{ program = $t.label; note = "could not sample modules: $_" } }
        finally { if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } }
    }
} else { $A.loaded += [pscustomobject]@{ note = "skipped: not Windows" } }
$report.A_ucrt = $A

# ---- B. MSVC runtime ---------------------------------------------------------------------------
$msvcMeta = @()
foreach ($p in @($rt, $GuiEnv)) { if ($p) { $msvcMeta += Get-CondaMeta $p @("vc14_runtime", "vcomp14") } }
$pattern = '^(vcruntime140.*|msvcp140.*|concrt140|vccorlib140|vcamp140|vcomp140)\.dll$'
$stagedFiles = @{}
foreach ($f in (Get-ChildItem $Stage -Recurse -File -Filter "*.dll" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match $pattern -and $_.FullName -notlike "*$($sep)licenses$sep*" })) { $stagedFiles[$f.FullName] = $true }
foreach ($m in $msvcMeta) { foreach ($rel in $m.files) {
    if ([System.IO.Path]::GetFileName($rel) -match $pattern) { $c = ConvertTo-Native $m.prefix $rel; if ($m.prefix -eq $rt) { $stagedFiles[$c] = $true } } } }
$stagedFacts = @($stagedFiles.Keys | Sort-Object | ForEach-Object { Get-FileFacts $_ } | Where-Object { $_.exists })
$B = [ordered]@{ packages = @(); stagedDlls = $stagedFacts; vs = [ordered]@{}; comparison = @(); redistTxt = @(); note = "" }
foreach ($m in $msvcMeta) { $B.packages += [pscustomobject]@{ prefix = $m.prefix; name = $m.name; version = $m.version; build = $m.build; licenseField = $m.license; files = $m.files.Count } }
$vsFiles = @(); $redistTxtNames = @()
$vswhere = ""
if ($isWin -and ${env:ProgramFiles(x86)}) { $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe" }
if ($vswhere -and (Test-Path $vswhere)) {
    try {
        $vsj = (& $vswhere -latest -products * -format json | Out-String) | ConvertFrom-Json
        $vs = @($vsj)[0]
        $B.vs = [ordered]@{ displayName = "$($vs.displayName)"; installationVersion = "$($vs.installationVersion)"
            productLine = "$($vs.catalog.productLineVersion)"; productDisplayVersion = "$($vs.catalog.productDisplayVersion)"
            installationPath = "$($vs.installationPath)"; redistVersions = @(); licenceFiles = @() }
        $redistRoot = Join-Path $vs.installationPath "VC\Redist\MSVC"
        if (Test-Path $redistRoot) {
            foreach ($vd in (Get-ChildItem $redistRoot -Directory)) {
                $B.vs.redistVersions += $vd.Name
                foreach ($f in (Get-ChildItem $vd.FullName -Recurse -File -Filter "*.dll" -ErrorAction SilentlyContinue |
                        Where-Object { $_.FullName -match '\\x64\\' -and $_.FullName -notmatch 'debug_nonredist|onecore' })) { $vsFiles += Get-FileFacts $f.FullName }
            }
        }
        foreach ($rt2 in (Get-ChildItem $vs.installationPath -Recurse -File -Depth 5 -Include "redist*.txt" -ErrorAction SilentlyContinue)) {
            $B.redistTxt += $rt2.FullName
            $redistTxtNames += Get-RedistNames (Get-Content $rt2.FullName)
        }
        $redistTxtNames = @($redistTxtNames | Sort-Object -Unique)
        foreach ($lf in (Get-ChildItem $vs.installationPath -File -Depth 2 -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'licen[cs]e|redist' })) { $B.vs.licenceFiles += $lf.FullName }
    } catch { $B.note = "could not read the Visual Studio installation: $_" }
} else { $B.note = "no Visual Studio installation read (not Windows, or vswhere not found)" }
$B.comparison = Compare-Redist $stagedFacts $vsFiles $redistTxtNames
$B.redistTxtNames = $redistTxtNames
$B.authority = "This compares the staged files with the copies in YOUR Visual Studio installation. The authoritative REDIST list is the online list referenced from the 'Distributable Code' section of the Microsoft Software License Terms for your edition; check each 'file' below against it."
$report.B_msvc = $B

# ---- C. exact packages for the S4 drafts -------------------------------------------------------
$names = @("tk", "libtk", "libtcl", "tcl", "tzdata", "libsqlite", "libwinpthread", "pyamg", "libfreetype6", "ucrt", "vc", "vc14_runtime", "vcomp14")
$C = @()
foreach ($p in @($rt, $GuiEnv)) {
    if (-not $p) { continue }
    foreach ($m in (Get-CondaMeta $p $names)) {
        $texts = @()
        if ($m.extracted_package_dir -and (Test-Path (Join-Path $m.extracted_package_dir "info\licenses"))) {
            $texts = @(Get-ChildItem (Join-Path $m.extracted_package_dir "info\licenses") -Recurse -File | ForEach-Object {
                [pscustomobject]@{ file = $_.FullName.Substring($m.extracted_package_dir.Length).TrimStart('\', '/'); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() } })
        }
        $C += [pscustomobject]@{ where = $(if ($p -eq $rt) { "runtime" } else { "tcad-gui" }); name = $m.name; version = $m.version; build = $m.build
            licence = $m.license; archiveSha256 = $m.sha256; archiveMd5 = $m.md5; filesInstalled = $m.files.Count; texts = $texts }
    }
}
$report.C_packages = $C

$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $Out "s4-evidence.json")
$txt = @()
$txt += "== A. ucrt"; $txt += "packages: " + (($A.packages | ForEach-Object { "$($_.prefix) ucrt $($_.version) ($($_.files) files)" }) -join "; ")
$txt += $A.runtimeSummary; $txt += "app-dir UCRT DLLs: $($A.appDirDlls.Count)"
$txt += "OS: $($A.os.product) $($A.os.displayVersion) build $($A.os.build); system ucrtbase.dll: $($A.os.systemUcrtbase.fileVersion)"
foreach ($l in $A.loaded) { $txt += "  loaded  $($l.program)  $($l.module)  <- $($l.path)  fromStage=$($l.fromStage) $($l.note)" }
$txt += ""; $txt += "== B. MSVC runtime"; $txt += "Visual Studio: $($B.vs.displayName) $($B.vs.installationVersion) ($($B.vs.productDisplayVersion)) at $($B.vs.installationPath)"
$txt += "redist.txt files: " + ($B.redistTxt -join "; "); $txt += "redist versions in VS: " + ($B.vs.redistVersions -join ", "); $txt += $B.note
foreach ($r in $B.comparison) { $txt += ("  {0,-28} v{1,-16} sig={2,-9} inVsRedistFolder={3} sameHash={4} sameVersion={5} inRedistTxt={6}" -f $r.file, $r.version, $r.signature, $r.inVsRedistFolder, $r.sameHashAsVs, $r.sameVersionAsVs, $r.inRedistTxt) }
$txt += $B.authority; $txt += ""; $txt += "== C. packages"
foreach ($c in $C) { $txt += "  [$($c.where)] $($c.name) $($c.version) $($c.build)  licence=$($c.licence)  files=$($c.filesInstalled)  texts=$($c.texts.Count)" }
$txt | Set-Content -Encoding UTF8 (Join-Path $Out "s4-evidence.txt")
$txt | ForEach-Object { Write-Host $_ }
Write-Host ""; Write-Host "written: $(Join-Path $Out 's4-evidence.json')  and  s4-evidence.txt   (read-only run: nothing was changed)"

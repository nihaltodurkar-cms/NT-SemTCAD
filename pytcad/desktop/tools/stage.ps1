# Stage a self-contained copy of the native desktop app (NATIVE-DESKTOP-PLAN.md
# section 26): the Release build, every DLL it actually loads (found by walking
# PE imports, not from a hand-written list), the Qt plugins windeployqt6
# selects, a desktop_runtime.json whose paths are RELATIVE to the exe
# (resolveManifestPath in backend_client.cpp) -- and, since P5-S2, the slim
# Python runtime (runtime\) and the backend sources (backend\) it runs.
#
#   powershell -ExecutionPolicy Bypass -File desktop\tools\stage.ps1 `
#       [-Out <dir>] [-Reference <dir>] [-Check <a.npz>] [-DevBackend] [-NoRuntime]
#       [-NoBuild] [-NoVerify] [-RuntimeEnv <name>] [-NoPin] [-RecreateRuntime]
#   powershell -ExecutionPolicy Bypass -File desktop\tools\stage.ps1 -EmitReference <dir>
#
# -EmitReference <dir>: run desktop\tools\check_runtime.py with the tcad-dev
#   interpreter and write versions.json + the three example results into <dir>;
#   then exit. Do this once; -Reference <dir> compares the staged runtime to it.
# -Reference <dir>: pass the -EmitReference directory to the verification, so it
#   compares versions (python, numpy, scipy, pyamg, mkl, BLAS) and the numerical
#   results of the three examples with tcad-dev. Without it the comparison is
#   reported as NOT done.
# -DevBackend: skip the runtime and point backend_python/backend_root at this
#   machine's tcad-dev env and source tree (absolute paths) -- the S1 staging.
# -NoRuntime: stage the app only; the manifest still names runtime\ and backend\.
# -RuntimeEnv: the conda env packed into runtime\ (default tcad-runtime). Never
#   tcad-dev, tcad-gui or tcad-cpp: the script refuses those names.
# -NoPin: create the runtime from desktop\tools\tcad-runtime.yml as written
#   instead of pinning every package to what tcad-dev has.
# -RecreateRuntime: delete and re-create the -RuntimeEnv env from the spec.
# -NoVerify: skip check_runtime.py on the staged runtime + backend.
# -NoLicenses: skip gen_licenses.py (P5-S4). The installer build refuses a stage without the bundle.
# -ExcludeUcrt: do not ship the conda `ucrt` package (the Windows SDK's app-local Universal CRT: ucrtbase.dll and
#   the api-ms-win-*.dll forwarders). On Windows 10/11 the OS always loads its own UCRT and ignores an app-local
#   copy (Microsoft; confirmed on this project's own tcad_desktop.exe: System32\ucrtbase.dll is loaded). Its files are
#   removed from runtime\ by the package's own conda-meta file list, its conda-meta record is deleted so the licence
#   inventory matches what ships, and the app-side copies from tcad-gui are neither copied nor kept. A RUNTIME CHANGE:
#   re-run the full S2 gate (stage.ps1 -Reference ...) and the S3 Sandbox gate. Off by default.
# -DownloadLicenseTexts: when a package's extracted conda cache dir AND tarball are both gone (`conda clean`),
#   re-download that package to restore its licence text -- verified against the sha256/md5 conda recorded.
#   Without it only local tarballs are used (gen_licenses.py --restore-texts); nothing touches the network.
# -Check <npz>: after staging, run the staged app's --selftest on <npz> with a
#   PATH holding only C:\Windows directories -- proof that nothing is picked up
#   from the conda envs.
#
# Prerequisite (once): conda-pack in the `base` env --
#   conda install -n base -c conda-forge conda-pack
# NEVER into tcad-dev or the runtime env (CLAUDE.md: a compiler installed into
# tcad-dev once broke PySide6 through channel/ABI side effects).
param(
    [string] $Out = "",
    [switch] $DevBackend,
    [string] $Check = "",
    [switch] $NoBuild,
    [switch] $NoRuntime,
    [switch] $NoVerify,
    [switch] $NoLicenses,
    [switch] $DownloadLicenseTexts,
    [switch] $ExcludeUcrt,
    [switch] $NoPin,
    [switch] $RecreateRuntime,
    [string] $RuntimeEnv = "tcad-runtime",
    [string] $Reference = "",
    [string] $EmitReference = ""
)
$ErrorActionPreference = "Stop"
$desktop = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $desktop
$build = Join-Path $root "build\desktop"
if (-not $Out) { $Out = Join-Path $root "dist\TCAD" }
$checkRuntime = Join-Path $PSScriptRoot "check_runtime.py"

# A path the user typed is relative to WHERE THEY RAN THE COMMAND, not to this script, the repo
# or a child process's working directory (check_runtime.py runs from the backend directory).
# Resolve it once, here, against PowerShell's current location; the directory need not exist.
function Resolve-UserPath([string] $p) {
    if (-not $p) { return "" }
    if (-not [System.IO.Path]::IsPathRooted($p)) { $p = Join-Path (Get-Location).Path $p }
    return [System.IO.Path]::GetFullPath($p)
}
$Out = Resolve-UserPath $Out
$Check = Resolve-UserPath $Check
$Reference = Resolve-UserPath $Reference
$EmitReference = Resolve-UserPath $EmitReference

function Get-CondaEnvPath([string] $name) {
    return ((conda env list --json | Out-String) | ConvertFrom-Json).envs |
        Where-Object { (Split-Path $_ -Leaf) -eq $name } | Select-Object -First 1
}

# Native commands do not stop the script under $ErrorActionPreference; check.
function Assert-Exit([string] $what) {
    if ($LASTEXITCODE) { throw "$what failed (exit $LASTEXITCODE)" }
}

# UTF-8 WITHOUT a byte-order mark (Set-Content -Encoding utf8 writes one on
# Windows PowerShell 5.1; a parser reading the manifest or conda reading the
# spec should not have to tolerate it).
function Write-TextFile([string] $path, [string[]] $lines) {
    [System.IO.File]::WriteAllLines($path, $lines, (New-Object System.Text.UTF8Encoding $false))
}

# -- conda package exclusion (-ExcludeUcrt) -------------------------------------------------------------

# The lower-case DLL names a package installs, from its conda-meta record(s) in a prefix (distinct).
function Get-CondaPackageFileNames([string] $prefix, [string] $name) {
    $names = @{}
    foreach ($f in (Get-ChildItem (Join-Path $prefix "conda-meta") -Filter "$name-*.json" -ErrorAction SilentlyContinue)) {
        $m = Get-Content -Raw $f.FullName | ConvertFrom-Json
        if ($m.name -ne $name) { continue }
        foreach ($rel in @($m.files)) { $names[[System.IO.Path]::GetFileName($rel).ToLowerInvariant()] = $true }
    }
    return @($names.Keys | Sort-Object)
}

# Remove the files of conda package `name` from a staged prefix, by ITS OWN conda-meta list, and delete its
# conda-meta record. All paths are validated BEFORE anything is deleted (an absolute or '..' path throws); a
# file that another package's record also lists is KEPT and reported. Returns $null when the package is not
# there (so a second run is a no-op).
function Remove-CondaPackage([string] $prefix, [string] $name) {
    $metaDir = Join-Path $prefix "conda-meta"
    $records = @(Get-ChildItem $metaDir -Filter "$name-*.json" -ErrorAction SilentlyContinue |
        Where-Object { (Get-Content -Raw $_.FullName | ConvertFrom-Json).name -eq $name })
    if (-not $records.Count) { return $null }
    $shared = @{}
    foreach ($o in (Get-ChildItem $metaDir -Filter "*.json" -ErrorAction SilentlyContinue)) {
        if ($records.FullName -contains $o.FullName) { continue }
        $om = Get-Content -Raw $o.FullName | ConvertFrom-Json
        foreach ($rel in @($om.files)) { $shared[$rel.Replace('\', '/').ToLowerInvariant()] = $om.name }
    }
    $plan = @()
    foreach ($r in $records) {
        $m = Get-Content -Raw $r.FullName | ConvertFrom-Json
        foreach ($rel in @($m.files)) {
            $norm = $rel.Replace('\', '/')
            # rooted, drive-lettered, leading-slash or '..' paths are refused on EVERY OS (IsPathRooted alone is OS-dependent)
            if ([System.IO.Path]::IsPathRooted($norm) -or $norm -match '^[A-Za-z]:' -or $norm.StartsWith('/') -or ($norm -split '/') -contains '..') { throw "refusing to remove '$rel': not a path inside the prefix" }
            $plan += [pscustomobject]@{ rel = $norm; path = (Join-Path $prefix ($norm -replace '/', [System.IO.Path]::DirectorySeparatorChar)); owner = $shared[$norm.ToLowerInvariant()] }
        }
    }
    $removed = @(); $missing = @(); $kept = @()
    foreach ($e in $plan) {
        if ($e.owner) { $kept += "$($e.rel) (also owned by $($e.owner))"; continue }
        if (Test-Path -LiteralPath $e.path -PathType Leaf) { Remove-Item -LiteralPath $e.path -Force; $removed += $e.rel } else { $missing += $e.rel }
    }
    $first = Get-Content -Raw $records[0].FullName | ConvertFrom-Json
    $records | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
    return [pscustomobject]@{ name = $name; version = "$($first.version)"; build = "$($first.build)"; removed = $removed; missing = $missing
        keptShared = $kept; metaRemoved = @($records | ForEach-Object { $_.Name }) }
}

# 0. The reference run: tcad-dev's own interpreter, the repo's source tree.
if ($EmitReference) {
    $dev = Get-CondaEnvPath "tcad-dev"
    if (-not $dev) { throw "conda env 'tcad-dev' not found" }
    & (Join-Path $dev "python.exe") $checkRuntime --backend $root --runtime $dev --emit-reference $EmitReference
    Assert-Exit "check_runtime.py --emit-reference"
    # Assert-Exit only sees the exit status: confirm the files -Reference will need exist.
    $written = Join-Path $EmitReference "versions.json"
    if (-not (Test-Path $written)) { throw "check_runtime.py exited 0 but wrote no $written" }
    Write-Host "reference written to $EmitReference"
    exit 0
}

# -Reference names a directory -EmitReference wrote: fail now, not after the build and the pack.
if ($Reference -and -not (Test-Path (Join-Path $Reference "versions.json"))) {
    throw "-Reference $Reference has no versions.json: run stage.ps1 -EmitReference <dir> first, then pass that same directory"
}

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
$gui = Get-CondaEnvPath "tcad-gui"
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

# windeployqt6 also deploys the DirectX Shader Compiler (dxcompiler.dll, dxil.dll) from the Windows SDK,
# the same way it deploys the D3D compiler that --no-system-d3d-compiler above already skips. No tcad-gui
# package owns them (P5-S4's licence gate reported both as unowned), they are Microsoft binaries whose
# redistribution terms nobody has reviewed, and the app has no use for them: it is Qt Widgets plus VTK's
# OpenGL widget, with no Qt RHI / Direct3D 12 code. So they are not shipped. Removed by name rather than by
# a windeployqt flag whose spelling varies between Qt versions; if the app ever needs them, they need a
# reviewed license_policy.json entry, not a silent copy.
foreach ($dx in @("dxcompiler.dll", "dxil.dll")) {
    Get-ChildItem $Out -Recurse -Filter $dx -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -Force $_.FullName
        Write-Host "not shipping $($_.Name) (Windows SDK DXC deployed by windeployqt6; nothing owns it, the app does not use it)"
    }
}

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
$excludedDlls = @{}
if ($ExcludeUcrt) { foreach ($n in (Get-CondaPackageFileNames $gui "ucrt")) { $excludedDlls[$n] = $true } }
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
        if ($excludedDlls.ContainsKey($key)) { continue }              # -ExcludeUcrt: the OS supplies these
        if (Test-Path (Join-Path $Out $dll)) { continue }
        $src = Join-Path $guibin $dll
        if (Test-Path $src) {
            Copy-Item $src $Out
            $copied++
            $queue.Enqueue((Join-Path $Out $dll))
        }
    }
}

# -ExcludeUcrt, app side: anything windeployqt6 or the closure left next to the exe that the ucrt package owns.
$ucrtAppRemoved = @()
if ($ExcludeUcrt) {
    foreach ($f in (Get-ChildItem $Out -Recurse -File -Filter "*.dll" -ErrorAction SilentlyContinue)) {
        $top = ($f.FullName.Substring($Out.Length).TrimStart('\', '/') -split '[\\/]')[0]
        if ($top -in @("runtime", "backend", "licenses")) { continue }
        if ($excludedDlls.ContainsKey($f.Name.ToLowerInvariant())) { Remove-Item -LiteralPath $f.FullName -Force; $ucrtAppRemoved += $f.FullName.Substring($Out.Length).TrimStart('\', '/') }
    }
}

# 5. The Python runtime (runtime\) and the backend sources (backend\) -- P5-S2.
function New-PinnedRuntimeSpec([string] $BaseYml, [string] $OutYml, [string] $EnvName) {
    # tcad-runtime.yml names the packages; the VERSIONS come from tcad-dev's own
    # `conda list`, so the shipped numerics are the ones the suite is verified
    # against. A pip-installed entry (channel "pypi") is pinned by version only,
    # for conda-forge to resolve; a missing one falls back to the base spec.
    if (-not (Get-CondaEnvPath "tcad-dev")) {
        throw "conda env 'tcad-dev' not found: cannot pin the runtime to it (use -NoPin for tcad-runtime.yml as written)"
    }
    $listing = conda list -n tcad-dev --json | Out-String     # one string: robust to line-per-record output
    Assert-Exit "conda list -n tcad-dev"
    $installed = $listing | ConvertFrom-Json
    $base = [ordered]@{}
    $inDeps = $false
    foreach ($l in (Get-Content $BaseYml)) {
        if ($l -match '^dependencies:') { $inDeps = $true; continue }
        if ($inDeps -and $l -match '^\s+-\s+(\S+)\s*$') { $base[($matches[1] -split '=')[0]] = $matches[1] }
    }
    $names = @($base.Keys) + @("libblas")      # libblas: the BLAS variant (MKL) numpy/scipy link
    $lines = @("name: $EnvName", "channels:", "  - conda-forge", "  - nodefaults", "dependencies:")
    foreach ($n in $names) {
        $rec = $installed | Where-Object { $_.name -eq $n } | Select-Object -First 1
        if ($rec -and $rec.channel -eq "pypi") {
            $spec = "$n=$($rec.version)"
            Write-Warning "$n is pip-installed in tcad-dev; pinned to $($rec.version) by version only"
        } elseif ($rec) {
            $spec = "$n=$($rec.version)=$($rec.build_string)"
        } elseif ($base.Contains($n)) {
            $spec = $base[$n]
            Write-Warning "$n not found in tcad-dev's conda list; using the spec from tcad-runtime.yml ($spec)"
        } else {
            Write-Warning "$n not found in tcad-dev's conda list; not pinned"
            continue
        }
        $lines += "  - $spec"
    }
    Write-TextFile $OutYml $lines
    Write-Host "runtime spec pinned to tcad-dev -> $OutYml"
    $lines | ForEach-Object { Write-Host "    $_" }
}

function Copy-Tree([string] $src, [string] $dst, [string[]] $xd = @()) {
    $rc = @($src, $dst, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/XF", "*.pyc")
    if ($xd.Count) { $rc += "/XD"; $rc += $xd }
    & robocopy @rc | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $src failed (exit $LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
}

$stagedRuntime = $false
if (-not $DevBackend -and -not $NoRuntime) {
    if (@("tcad-dev", "tcad-gui", "tcad-cpp") -contains $RuntimeEnv) {
        throw "-RuntimeEnv $RuntimeEnv is a development env; the runtime must be its own env (default tcad-runtime)"
    }
    New-Item -ItemType Directory -Force $build | Out-Null

    # 5a. The runtime env, created from the spec if it does not exist yet.
    if ((Get-CondaEnvPath $RuntimeEnv) -and $RecreateRuntime) {
        conda env remove -n $RuntimeEnv -y
        Assert-Exit "conda env remove -n $RuntimeEnv"
    }
    if (-not (Get-CondaEnvPath $RuntimeEnv)) {
        $spec = Join-Path $PSScriptRoot "tcad-runtime.yml"
        if (-not $NoPin) {
            $pinned = Join-Path $build "tcad-runtime.pinned.yml"
            New-PinnedRuntimeSpec $spec $pinned $RuntimeEnv
            $spec = $pinned
        }
        conda env create -n $RuntimeEnv -f $spec
        Assert-Exit "conda env create -n $RuntimeEnv"
    } else {
        Write-Host "reusing conda env '$RuntimeEnv' (-RecreateRuntime rebuilds it from the spec)"
    }

    # 5b. conda-pack it into runtime\ (a tar.gz, unpacked with the OS's tar.exe),
    #     then conda-unpack rewrites the build-time prefixes for the new location.
    conda run -n base conda-pack --version | Out-Null
    if ($LASTEXITCODE) {
        throw "conda-pack is not installed in the base env: conda install -n base -c conda-forge conda-pack (never into tcad-dev or $RuntimeEnv)"
    }
    $tgz = Join-Path $build "tcad-runtime.tar.gz"
    conda run -n base conda-pack -n $RuntimeEnv -o $tgz --force
    Assert-Exit "conda-pack -n $RuntimeEnv"
    $rt = Join-Path $Out "runtime"
    New-Item -ItemType Directory -Force $rt | Out-Null
    & tar.exe -xzf $tgz -C $rt
    Assert-Exit "tar -xzf $tgz"
    $unpack = Join-Path $rt "Scripts\conda-unpack.exe"
    if (-not (Test-Path $unpack)) { throw "conda-pack produced no Scripts\conda-unpack.exe in $rt" }
    & $unpack
    Assert-Exit "conda-unpack"

    # 5c. The backend: the packages the app launches, and nothing else. The
    #     compiled extension (pytcad\_core*.pyd, gitignored) must already be built.
    $ext = Get-ChildItem (Join-Path $root "pytcad") -Filter "_core*.pyd" -ErrorAction SilentlyContinue
    if (-not $ext) {
        throw "no pytcad\_core*.pyd in $root\pytcad -- build it first (CLAUDE.md, 'The C++ engine'); Device1D and every compiled kernel need it"
    }
    $be = Join-Path $Out "backend"
    Copy-Tree (Join-Path $root "pytcad") (Join-Path $be "pytcad") @("__pycache__", "benchmarks")
    Copy-Tree (Join-Path $root "workbench") (Join-Path $be "workbench") @("__pycache__")
    Copy-Tree (Join-Path $root "backend_service") (Join-Path $be "backend_service") @("__pycache__")
    New-Item -ItemType Directory -Force (Join-Path $be "gui") | Out-Null
    Copy-Item (Join-Path $root "gui\__init__.py") (Join-Path $be "gui")
    Copy-Tree (Join-Path $root "gui\services") (Join-Path $be "gui\services") @("__pycache__")
    # 5c'. -ExcludeUcrt, runtime side: remove the ucrt package by its own file list and drop its conda-meta record,
    #      then prove nothing it owned is left anywhere in the staged tree.
    $excluded = $null
    if ($ExcludeUcrt) {
        $excluded = Remove-CondaPackage $rt "ucrt"
        if (-not $excluded) { throw "-ExcludeUcrt: no ucrt package in $rt\conda-meta (already excluded, or not in this runtime)" }
        Write-Host ("excluded ucrt {0}: removed {1} runtime files ({2} listed but absent, {3} kept as shared); {4} app-dir copies not shipped" -f $excluded.version, $excluded.removed.Count, $excluded.missing.Count, $excluded.keptShared.Count, $ucrtAppRemoved.Count)
        $left = @(Get-ChildItem $Out -Recurse -File -Filter "*.dll" | Where-Object { $excludedDlls.ContainsKey($_.Name.ToLowerInvariant()) })
        if ($left.Count) { throw ("-ExcludeUcrt: still staged: " + (($left | Select-Object -First 5 | ForEach-Object { $_.FullName }) -join "; ")) }
    }
    # 5d. The licence bundle (P5-S4), generated from what is now actually staged: every conda package
    #     in runtime\ and every DLL next to the exe (traced to its owning tcad-gui package). Fails, with
    #     every problem listed, on a package/DLL with no licence entry, GPL/AGPL in the base runtime, or a
    #     GPL-only Qt module. stdlib-only, so the staged interpreter runs it.
    if (-not $NoLicenses) {
        $licArgs = @("--stage", $Out, "--gui-env", $gui, "--restore-texts")
        if ($DownloadLicenseTexts) { $licArgs += "--download" }
        & (Join-Path $rt "python.exe") (Join-Path $PSScriptRoot "gen_licenses.py") @licArgs
        Assert-Exit "gen_licenses.py (the licence bundle)"
    }
    if ($excluded) {                                             # a record of what was deliberately not shipped
        New-Item -ItemType Directory -Force (Join-Path $Out "licenses") | Out-Null
        $rec = [ordered]@{ package = $excluded.name; version = $excluded.version; build = $excluded.build
            reason = "app-local Universal CRT: the OS always loads its own UCRT on Windows 10/11 (NATIVE-DESKTOP-PLAN.md 26.9.3)"
            runtimeFilesRemoved = $excluded.removed; runtimeFilesListedButAbsent = $excluded.missing; keptBecauseSharedWithAnotherPackage = $excluded.keptShared
            condaMetaRecordsRemoved = $excluded.metaRemoved; appDirCopiesNotShipped = $ucrtAppRemoved }
        Write-TextFile (Join-Path $Out "licenses\excluded-packages.json") (($rec | ConvertTo-Json -Depth 4) -split "`r?`n")
    }
    $stagedRuntime = $true
}

# 6. Manifest: relative paths (the installed layout) or, with -DevBackend, this
#    machine's backend.
if ($DevBackend) {
    $dev = Get-CondaEnvPath "tcad-dev"
    $manifest = [ordered]@{ backend_python = (Join-Path $dev "python.exe"); backend_root = $root; runtime_bin = "" }
} else {
    $manifest = [ordered]@{ backend_python = "runtime\python.exe"; backend_root = "backend"; runtime_bin = "" }
}
Write-TextFile (Join-Path $Out "desktop_runtime.json") (($manifest | ConvertTo-Json) -split "`r?`n")

$files = Get-ChildItem $Out -Recurse -File
$mb = [math]::Round((($files | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host "staged $($files.Count) files ($mb MB, $copied DLLs from tcad-gui) -> $Out"
if ($stagedRuntime) {
    $rtmb = [math]::Round(((Get-ChildItem (Join-Path $Out "runtime") -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
    Write-Host "  runtime\ $rtmb MB (record this in NATIVE-DESKTOP-PLAN.md section 26)"
}

# 7. The runtime gates: import closure, add-ons absent, extension, PARDISO,
#    backend handshake, the three examples, and (with -Reference) tcad-dev parity.
#    check_runtime.py re-launches itself with a scrubbed environment.
if ($stagedRuntime -and -not $NoVerify) {
    $argv = @($checkRuntime, "--backend", (Join-Path $Out "backend"), "--runtime", (Join-Path $Out "runtime"))
    if ($Reference) { $argv += @("--reference", $Reference) }
    & (Join-Path $Out "runtime\python.exe") @argv
    Assert-Exit "check_runtime.py on the staged runtime"
    Write-Host "runtime gates passed"
}

# 8. Optional check with a scrubbed PATH (Windows directories only).
if ($Check) {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & (Join-Path $Out "tcad_desktop.exe") --selftest $Check
    if ($LASTEXITCODE) { throw "staged --selftest failed (exit $LASTEXITCODE)" }
    Write-Host "staged --selftest passed with a scrubbed PATH"
}

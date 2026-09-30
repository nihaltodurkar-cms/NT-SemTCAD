; PyTCAD Desktop installer (NATIVE-DESKTOP-PLAN.md section 26.3, P5-S3).
; Inno Setup 6.3 or newer (x64compatible). Built by desktop\tools\make_installer.ps1,
; which passes the three required defines:
;   /DAppVersion=<x.y.z>   from desktop\CMakeLists.txt's project(... VERSION ...)
;   /DStageDir=<dir>       the stage.ps1 output (dist\TCAD): exe, DLLs, runtime\, backend\
;   /DOutDir=<dir>         where TCAD-<version>-unsigned-setup.exe is written
; and optionally /DNameSuffix= (default "-unsigned": decision 26.4-2, no certificate yet;
; S6 makes the signed build pass an empty suffix).
;
; Decisions this file implements (section 26.4): Inno Setup; per-user install with no
; admin rights under %LOCALAPPDATA%\Programs\TCAD; the base installer ships neither gmsh
; nor tetgen (make_installer.ps1 refuses a stage that contains them).
;
; Uninstall removes what the installer put in {app}, plus the .pyc caches the runtime
; writes into runtime\ and backend\ once the app has run. It never touches the user's
; own data, which lives elsewhere: settings in %APPDATA%\PyTCAD\ (AppSettings::userDefault),
; runs in %LOCALAPPDATA%\PyTCAD\runs\ (runsDir() in main_window.cpp), and the user's
; project and result files wherever they saved them.
;
; NO file association is registered. The app's project file type is plain ".json"
; (main_window.cpp's dialog filter; DeviceSpec job files are ".json" too) and results are
; ".npz", so a default association would take over every JSON/NumPy file on the machine.
; The app is added to the per-user "Open with" list for both instead (task "openwith").
; A real association needs a dedicated project extension: a file-format decision, not an
; installer one (NATIVE-DESKTOP-PLAN.md 26.8).
;
; The Microsoft Visual C++ runtime is NOT in the stage (stage.ps1 -ExcludeMsvcRuntime: its conda
; package's licence does not allow redistributing it; decision 26.9.7). Instead Microsoft's own
; vc_redist.x64.exe is embedded (make_installer.ps1 passes /DVcRedist=<path> and the version the
; stage needs as /DVcMajor /DVcMinor /DVcBld) and run BEFORE any file is installed, only when the
; machine's runtime (HKLM ...\VC\Runtimes\x64) is missing or older. It installs machine-wide, so
; that one step asks for administrator approval (UAC); a machine that already has a current runtime
; never sees a prompt. If it fails, setup stops with nothing installed. Uninstall leaves the runtime
; in place: it is a shared system component other programs use.

#ifndef AppVersion
  #error AppVersion is not defined: pass /DAppVersion=x.y.z (desktop\tools\make_installer.ps1 does)
#endif
#ifndef StageDir
  #error StageDir is not defined: pass /DStageDir=<the stage.ps1 output directory>
#endif
#ifndef OutDir
  #define OutDir "."
#endif
#ifndef NameSuffix
  #define NameSuffix "-unsigned"
#endif

#define AppName "PyTCAD Desktop"
#define AppExe "tcad_desktop.exe"

[Setup]
; Never change AppId: it is how a newer installer finds and upgrades this install.
AppId={{575F975D-8CB7-490C-8FAA-7A4870CC2EE3}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\TCAD
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=TCAD-{#AppVersion}{#NameSuffix}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "openwith"; Description: "Add {#AppName} to the ""Open with"" list for .json and .npz files"
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef VcRedist
Source: "{#VcRedist}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; "Open with" only (Software\Classes\Applications\<exe>): registers the app as a candidate
; handler, never as the default. HKA is HKCU here (a per-user install).
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".json"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".npz"; ValueData: ""; Tasks: openwith

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; .pyc caches (and anything else the runtime writes) appear after the first run and are
; not in the uninstall log.
Type: filesandordirs; Name: "{app}\runtime"
Type: filesandordirs; Name: "{app}\backend"
Type: dirifempty; Name: "{app}"

#ifdef VcRedist
#ifndef VcMajor
  #error VcRedist needs VcMajor/VcMinor/VcBld: the runtime version the stage was built against
#endif
[Code]
const
  VcKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

// True when the machine-wide x64 VC++ runtime is installed at the required version or newer.
function VcRuntimeIsCurrent(): Boolean;
var
  Installed, Major, Minor, Bld: Cardinal;
begin
  Result := False;
  if not RegQueryDWordValue(HKLM64, VcKey, 'Installed', Installed) or (Installed <> 1) then Exit;
  if not (RegQueryDWordValue(HKLM64, VcKey, 'Major', Major) and RegQueryDWordValue(HKLM64, VcKey, 'Minor', Minor)
          and RegQueryDWordValue(HKLM64, VcKey, 'Bld', Bld)) then Exit;
  Result := (Major > {#VcMajor}) or ((Major = {#VcMajor}) and ((Minor > {#VcMinor})
            or ((Minor = {#VcMinor}) and (Bld >= {#VcBld}))));
end;

// Runs before any file is copied: returning a message aborts setup with nothing installed.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  if VcRuntimeIsCurrent() then Exit;
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '',
                   SW_SHOW, ewWaitUntilTerminated, Code) then
    Result := 'The Microsoft Visual C++ runtime could not be installed (it needs administrator approval): '
              + SysErrorMessage(Code)
  else begin
    if Code = 3010 then NeedsRestart := True
    else if (Code <> 0) and (Code <> 1638) then   // 1638: a newer version is already installed
      Result := Format('The Microsoft Visual C++ runtime installer failed (exit code %d).', [Code]);
    if (Result = '') and not VcRuntimeIsCurrent() then
      Result := 'The Microsoft Visual C++ runtime installer finished, but the runtime is still missing or older than {#VcMajor}.{#VcMinor}.{#VcBld}.';
  end;
end;
#endif

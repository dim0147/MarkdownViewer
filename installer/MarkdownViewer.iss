; MarkdownViewer.iss - Inno Setup script for Markdown Viewer.
;
; Build:   ISCC.exe installer\MarkdownViewer.iss   (after build.bat)
; Output:  installer\output\MarkdownViewer-Setup-<version>.exe
;
; Design notes (keep in sync with CLAUDE.md "Installer" section):
;  - Per-user install (PrivilegesRequired=lowest), no admin needed - mirrors
;    the app's own HKCU-only philosophy. Installs to %LOCALAPPDATA%\Programs.
;  - Version is read from the exe's VERSIONINFO (res\app.rc) - do not hardcode.
;  - Uninstall removes EVERYTHING the app ever writes:
;      {app}                              exe + uninstaller (Inno default)
;      %LOCALAPPDATA%\MarkdownViewer      extracted assets + WebView2 profile cache
;      %APPDATA%\MarkdownViewer           config.json (user settings)
;      HKCU SystemFileAssociations\<ext>\shell\MarkdownViewer   context menu
;    The context menu is removed twice on purpose: [UninstallRun] uses the
;    app's own --unregister (correct SHChangeNotify refresh), and [Code]
;    CurUninstallStepChanged sweeps the keys again in case the exe could not run.

#define MyAppName "Markdown Viewer"
#define MyAppExeName "MarkdownViewer.exe"
#define MyAppDirName "MarkdownViewer"     ; cfg::kAppDirName in src\config.h
#define RepoRoot ".."
#define MyAppVersion GetVersionNumbersString(RepoRoot + "\" + MyAppExeName)

[Setup]
AppId={{EE354A00-3CF6-4974-8750-B4BD75B21925}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=dim0147
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=output
OutputBaseFilename=MarkdownViewer-Setup-{#MyAppVersion}
SetupIconFile={#RepoRoot}\res\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "explorermenu"; Description: "Add ""View with {#MyAppName}"" to the Explorer right-click menu for .md files"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#RepoRoot}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; --register / --unregister are quiet CLI modes handled in src\main.cpp.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--register"; Tasks: explorermenu
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--unregister"; RunOnceId: "UnregisterExplorerMenu"

[UninstallDelete]
; Caches and settings written by the app at runtime (see src\fileio.h):
;  - extracted UI assets + WebView2 browser profile (cache)
Type: filesandordirs; Name: "{localappdata}\{#MyAppDirName}"
;  - config.json (user settings)
Type: filesandordirs; Name: "{userappdata}\{#MyAppDirName}"

[Code]
const
  // EdgeUpdate client GUID of the WebView2 Runtime (fixed, documented by Microsoft).
  WebView2ClientKey = 'Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';

function WebView2RuntimePresent: Boolean;
var
  Ver: String;
begin
  Result :=
    (RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\' + WebView2ClientKey, 'pv', Ver) or
     RegQueryStringValue(HKLM, 'SOFTWARE\' + WebView2ClientKey, 'pv', Ver) or
     RegQueryStringValue(HKCU, 'Software\' + WebView2ClientKey, 'pv', Ver)) and
    (Ver <> '') and (Ver <> '0.0.0.0');
end;

function InitializeSetup: Boolean;
var
  ErrCode: Integer;
begin
  Result := True;
  if not WebView2RuntimePresent then
    if MsgBox('Markdown Viewer requires the Microsoft Edge WebView2 Runtime, '
              + 'which was not found on this PC. It comes preinstalled on '
              + 'Windows 11 and recent Windows 10.'#13#10#13#10
              + 'Open the WebView2 Runtime download page now? '
              + '(Setup will continue either way.)',
              mbConfirmation, MB_YESNO) = IDYES then
      ShellExecAsOriginalUser('open',
        'https://developer.microsoft.com/microsoft-edge/webview2/',
        '', '', SW_SHOWNORMAL, ewNoWait, ErrCode);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Fallback sweep: guarantee the HKCU context-menu keys are gone even if
  // "--unregister" in [UninstallRun] could not execute. Extension list must
  // match cfg::kMarkdownExts in src\config.h.
  if CurUninstallStep = usPostUninstall then
  begin
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\SystemFileAssociations\.md\shell\MarkdownViewer');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\SystemFileAssociations\.markdown\shell\MarkdownViewer');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\SystemFileAssociations\.mdown\shell\MarkdownViewer');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\SystemFileAssociations\.mkd\shell\MarkdownViewer');
  end;
end;

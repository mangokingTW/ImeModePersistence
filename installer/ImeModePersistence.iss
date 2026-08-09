; Inno Setup script for ImeModePersistence.
;
; Per-user install by design: it needs no administrator rights and no UAC prompt,
; and it keeps the utility at the same integrity level as the ordinary
; applications whose IME state it adjusts (see the UIPI note in README.md).
;
; Build (after compiling both architectures into build-x64 and build-x86):
;   iscc /DAppVersion=1.0.0 installer\ImeModePersistence.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName "ImeModePersistence"
#define AppExeName "ImeModePersistence.exe"
#define AppPublisher "mangokingTW"
#define AppUrl "https://github.com/mangokingTW/ImeModePersistence"

[Setup]
; Never change AppId: it is how Windows matches upgrades and uninstall entries.
AppId={{609AC807-6D9F-4D06-8F8D-AC65E29869D5}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}

PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}

; Refuse to overwrite a running executable: the utility holds this mutex for its
; whole lifetime, so Setup can detect it and ask the user to exit from the tray.
AppMutex=Local\{#AppName}.SingleInstance
CloseApplications=yes

OutputDir=..\dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
SetupIconFile=..\assets\ImeModePersistence.ico

; 32-bit build on 32-bit Windows, 64-bit build everywhere else.
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "startup"; Description: "Start with Windows"; GroupDescription: "Additional options:"

[Files]
Source: "..\build-x64\Release\{#AppExeName}"; DestDir: "{app}"; DestName: "{#AppExeName}"; \
    Check: Is64BitInstallMode; Flags: ignoreversion
Source: "..\build-x86\Release\{#AppExeName}"; DestDir: "{app}"; DestName: "{#AppExeName}"; \
    Check: not Is64BitInstallMode; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Registry]
; Written in exactly the form the tray toggle writes, so the two agree about
; whether autostart is on.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExeName}"""; \
    Tasks: startup; Flags: uninsdeletevalue

; Always clean up on uninstall, including an entry the user enabled from the tray
; menu rather than through this installer. ValueType none plus dontcreatekey
; writes nothing at install time, so it cannot clobber that entry.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: none; ValueName: "{#AppName}"; \
    Flags: dontcreatekey uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[Code]
{ Inno Setup has no maintenance mode: re-running Setup reinstalls over the
  existing copy with no way to remove, and no acknowledgement of whether the
  copy on disk is older, the same, or newer. What Setup should do differs in all
  three cases, so it reads the installed version and decides:

    installed is older  upgrade in place, no questions asked
    same version        offer repair or removal
    installed is newer  warn before downgrading

  This key must stay in step with AppId above; Inno forms it by appending _is1. }
const
  UninstallRegKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\' +
    '{609AC807-6D9F-4D06-8F8D-AC65E29869D5}_is1';

function InstalledCopy(var Command, Version: String): Boolean;
begin
  Result := RegQueryStringValue(HKCU, UninstallRegKey, 'UninstallString', Command)
            and (Command <> '');
  if Result and not RegQueryStringValue(HKCU, UninstallRegKey, 'DisplayVersion', Version) then
    Version := '';
end;

function RemoveInstalledCopy(const Command: String): Boolean;
var
  ResultCode: Integer;
begin
  { Already confirmed by the caller, so do not make the uninstaller ask again. }
  Result := Exec(RemoveQuotes(Command), '/SILENT /NORESTART', '',
                 SW_SHOW, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);

  if Result then
    SuppressibleMsgBox('{#AppName} has been removed.', mbInformation, MB_OK, IDOK)
  else
    SuppressibleMsgBox('Could not remove {#AppName}. Remove it from ' +
                       'Settings > Apps > Installed apps instead.',
                       mbError, MB_OK, IDOK);
end;

{ Negative when the installed copy is older than this installer, positive when it
  is newer, zero when they match. An unparseable version also compares as zero,
  so a corrupted registry value lands on the prompt rather than silently
  upgrading or downgrading. }
function CompareInstalled(const Installed: String): Integer;
var
  Old, Current: Int64;
begin
  Result := 0;
  if StrToVersion(Installed, Old) and StrToVersion('{#AppVersion}', Current) then
    Result := ComparePackedVersion(Old, Current);
end;

function InitializeSetup(): Boolean;
var
  Command, Installed: String;
  Comparison: Integer;
begin
  Result := True;

  if not InstalledCopy(Command, Installed) then
    exit;

  Comparison := CompareInstalled(Installed);

  { Running a newer installer *is* the update, so don't put a dialog in the way
    of it. Inno replaces the files under the same AppId and refreshes the
    uninstall entry's version, and UsePreviousTasks keeps the autostart choice. }
  if Comparison < 0 then
    exit;

  if Comparison > 0 then
  begin
    case SuppressibleMsgBox(
           '{#AppName} ' + Installed + ' is installed, which is newer than this ' +
           'installer ({#AppVersion}).' + #13#10#13#10 +
           'Yes' + #9 + '- remove the installed version' + #13#10 +
           'No' + #9 + '- downgrade to {#AppVersion}',
           mbConfirmation, MB_YESNOCANCEL, IDCANCEL) of
      IDYES:
        begin
          RemoveInstalledCopy(Command);
          Result := False;
        end;
      IDCANCEL:
        Result := False;
    end;
    exit;
  end;

  { Same version, so there is nothing to update and the useful choices are
    repairing the existing copy or removing it. }
  case SuppressibleMsgBox(
         '{#AppName} {#AppVersion} is already installed and up to date.' + #13#10#13#10 +
         'Yes' + #9 + '- remove it' + #13#10 +
         'No' + #9 + '- reinstall over the existing copy',
         mbConfirmation, MB_YESNOCANCEL, IDNO) of
    IDYES:
      begin
        RemoveInstalledCopy(Command);
        Result := False;
      end;
    IDCANCEL:
      Result := False;
  end;
end;

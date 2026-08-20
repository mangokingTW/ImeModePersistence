; Inno Setup script for ImeModePersistence.
;
; Per-user install by design: it needs no administrator rights and no UAC prompt,
; and it keeps the utility at the same integrity level as the ordinary
; applications whose IME state it adjusts (see the UIPI note in README.md).
;
; Build (after compiling build-x64):
;   iscc /DAppVersion=1.0.0 installer\ImeModePersistence.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

; The full version string for the output filename and release name, which may
; carry a pre-release suffix (e.g. 0.9.20-beta.1). AppVersion itself must stay a
; bare numeric MAJOR.MINOR.PATCH, because VersionInfoVersion and the upgrade
; comparison in [Code] both require that.
#ifndef AppVersionFull
  #define AppVersionFull AppVersion
#endif

; Compiled twice from this one file, and both outputs say which they are: an
; unqualified "-setup" would leave the reader to guess which variant they have.
#ifdef UserInstall
  #define SetupSuffix "-user"
#else
  #define SetupSuffix "-admin"
#endif

#define AppName "IME Mode Persistence"
; Unspaced slug for the setup filename, install folder, the Run value name (must
; match kValueName in src/autostart.cpp), and the legacy scheduled-task name.
; Display uses AppName.
#define AppSlug "ImeModePersistence"
#define AppExeName "ImeModePersistence.exe"
; Must match kClassName and kSingleInstanceMutex in src/main.cpp.
#define AppWindowClass "ImeModePersistenceHiddenWindow"
#define AppMutexName "Local\ImeModePersistence.SingleInstance"
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

; Administrator, because this variant installs into Program Files and, usefully,
; only an elevated Setup can close a running elevated copy when updating. The
; utility itself starts unelevated at logon (a normal Run entry); controlling an
; elevated anti-cheat game is done on demand via the tray's "Restart as
; administrator", not by a silent elevated logon task.
#ifdef UserInstall
; No administrator rights anywhere. Cannot run the utility elevated, so it cannot
; reach anti-cheat protected games -- fine for someone who only wants the ordinary
; behaviour, or who has no administrator rights to give.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#AppSlug}
#else
; Setup elevates so it can install into Program Files and close a running elevated
; copy when updating. The utility still starts unelevated at logon; elevation is
; on demand via the tray.
PrivilegesRequired=admin
DefaultDirName={autopf}\{#AppSlug}
#endif
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}

; Last-resort guard against overwriting a running executable. [Code] closes the
; utility itself before Setup gets this far, so the prompt this would otherwise
; raise only appears when that graceful close fails. Setup is elevated too, so it
; can close an elevated copy -- which an unelevated Setup could not.
AppMutex={#AppMutexName}
CloseApplications=yes

OutputDir=..\dist
OutputBaseFilename={#AppSlug}-{#AppVersionFull}-setup{#SetupSuffix}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
SetupIconFile=..\assets\ImeModePersistence.ico

; x64 only -- there is no 32-bit build, so refuse to install on 32-bit Windows.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
; Autostart is a normal, unelevated Run entry for both variants. There is no
; "run as administrator at logon" option: a silent elevated logon task is the
; persistence pattern antivirus flags, so elevation is left to the tray's
; "Restart as administrator" when the user actually needs it.
Name: "logon"; Description: "{cm:TaskLogon}"
; Unchecked: most installs are not for this game. When ticked, presets.txt is
; installed (below) and the utility seeds the rule on its next start -- the
; installer does not write the rule itself, because the rule lives in HKCU and an
; elevated installer cannot be sure whose hive that is (the /RU trap again).
Name: "helldivers"; Description: "{cm:TaskHelldivers}"; Flags: unchecked

[CustomMessages]
TaskLogon=Start automatically at logon
TaskHelldivers=Bind Helldivers 2 to English input (adds a rule on first run)

[Files]
Source: "..\build-x64\Release\{#AppExeName}"; DestDir: "{app}"; DestName: "{#AppExeName}"; \
    Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
; Only when the Helldivers task is ticked. The utility reads it once per user and
; then leaves it; a tracked file, so uninstall removes it. Installed before the
; [Run] launch, so a same-account elevated install seeds on the spot.
Source: "presets-helldivers.txt"; DestDir: "{app}"; DestName: "presets.txt"; \
    Tasks: helldivers; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Registry]
; Both variants: autostart is an unelevated HKCU Run entry, in exactly the form the
; tray toggle writes, so the two agree about whether autostart is on. (For the
; admin installer this lands in the elevating account's hive; on the usual
; single-user machine that is the same account, and the tray toggle fixes the rare
; over-the-shoulder case.)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#AppSlug}"; ValueData: """{app}\{#AppExeName}"""; \
    Tasks: logon; Flags: uninsdeletevalue

; Always clean up on uninstall, including an entry the user enabled from the tray
; menu rather than through this installer. ValueType none plus dontcreatekey
; writes nothing at install time, so it cannot clobber that entry.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: none; ValueName: "{#AppSlug}"; \
    Flags: dontcreatekey uninsdeletevalue

[Run]
; runasoriginaluser: launch as the non-elevated user who started Setup. The admin
; installer runs elevated, and without this the app would inherit that elevation
; -- so the just-installed tray would already be elevated and hide "Restart as
; administrator", not matching the unelevated state it has after a reboot (and
; leaving a player who follows the wiki unable to find the option). For the user
; installer, which is not elevated, the flag is a no-op. (shellexec, used here
; while the removed RUNASADMIN layer made CreateProcess fail with 740, is gone
; with the layer.)
;
; It was running when Setup started, so put it back without asking.
Filename: "{app}\{#AppExeName}"; Flags: nowait runasoriginaluser; Check: WasRunning

; Nothing was running, so offer the usual launch checkbox instead.
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent runasoriginaluser; Check: not WasRunning

[UninstallRun]
; Removed whether or not this install created it, so a task left behind by an
; earlier install does not survive. Failure is ignored: having nothing to delete
; is the normal case.
Filename: "{sys}\schtasks.exe"; Parameters: "/Delete /F /TN ""{#AppSlug}"""; \
    Flags: runhidden; RunOnceId: "DeleteLogonTask"

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

  WM_CLOSE = $0010;

  { Long enough for a tray application with no work to finish, short enough that a
    hung one falls through to the AppMutex prompt rather than stalling Setup. }
  CloseTimeoutMs = 5000;
  ClosePollMs = 100;

var
  ClosedRunningInstance: Boolean;

{ True when Setup shut the utility down, so [Run] knows to start it again. }
function WasRunning(): Boolean;
begin
  Result := ClosedRunningInstance;
end;

{ Windows cannot replace a running executable, so an upgrade has to stop the
  utility first. Asking the user to do that by hand is what AppMutex alone
  produces; this asks the process politely instead. WM_CLOSE reaches the hidden
  top-level window, which tears down the tray icon and releases the mutex. }
function CloseRunningInstance(): Boolean;
var
  Window: HWND;
  Elapsed: Integer;
begin
  if not CheckForMutexes('{#AppMutexName}') then
  begin
    Result := True;
    exit;
  end;

  Window := FindWindowByClassName('{#AppWindowClass}');
  if Window <> 0 then
    PostMessage(Window, WM_CLOSE, 0, 0);

  Elapsed := 0;
  while CheckForMutexes('{#AppMutexName}') and (Elapsed < CloseTimeoutMs) do
  begin
    Sleep(ClosePollMs);
    Elapsed := Elapsed + ClosePollMs;
  end;

  Result := not CheckForMutexes('{#AppMutexName}');
  ClosedRunningInstance := Result;
end;

{ Both hives, because where the uninstall entry lands depends on how the copy that
  wrote it was installed: an elevated install records itself in HKLM, an
  unelevated one in HKCU. Reading only HKCU meant an administrator install was
  invisible to the version comparison below, so re-running Setup neither
  recognised an upgrade nor offered removal. }
function InstalledCopy(var Command, Version: String): Boolean;
var
  Root: Integer;
begin
  Root := HKLM;
  Result := RegQueryStringValue(Root, UninstallRegKey, 'UninstallString', Command)
            and (Command <> '');

  if not Result then
  begin
    Root := HKCU;
    Result := RegQueryStringValue(Root, UninstallRegKey, 'UninstallString', Command)
              and (Command <> '');
  end;

  if Result and not RegQueryStringValue(Root, UninstallRegKey, 'DisplayVersion', Version) then
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
  Labels: TArrayOfString;
begin
  Result := True;

  if not InstalledCopy(Command, Installed) then
    exit;

  { Before Setup's own AppMutex check, so the "please close it first" message
    never appears in the ordinary case. Also clears the way for the uninstaller
    on the removal paths below, which carries the same mutex check. }
  CloseRunningInstance();

  Comparison := CompareInstalled(Installed);

  { Running a newer installer *is* the update, so don't put a dialog in the way
    of it. Inno replaces the files under the same AppId and refreshes the
    uninstall entry's version, and UsePreviousTasks keeps the autostart choice. }
  if Comparison < 0 then
    exit;

  if Comparison > 0 then
  begin
    { Custom button text so the buttons say what they do; a plain Yes/No/Cancel
      box left the reader to map the words onto actions in the body text.
      TaskDialogMsgBox relabels the standard buttons but still returns
      IDYES/IDNO/IDCANCEL. }
    SetArrayLength(Labels, 3);
    Labels[0] := 'Remove the installed version';
    Labels[1] := 'Downgrade to {#AppVersion}';
    Labels[2] := 'Cancel';
    case SuppressibleTaskDialogMsgBox(
           '{#AppName} ' + Installed + ' is installed',
           'That is newer than this installer ({#AppVersion}).',
           mbConfirmation, MB_YESNOCANCEL, Labels, 0, IDCANCEL) of
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
    reinstalling over the existing copy or removing it. }
  SetArrayLength(Labels, 3);
  Labels[0] := 'Remove it';
  Labels[1] := 'Reinstall over the existing copy';
  Labels[2] := 'Cancel';
  case SuppressibleTaskDialogMsgBox(
         '{#AppName} {#AppVersion} is already installed and up to date.',
         'You can reinstall over the existing copy or remove it.',
         mbConfirmation, MB_YESNOCANCEL, Labels, 0, IDNO) of
    IDYES:
      begin
        RemoveInstalledCopy(Command);
        Result := False;
      end;
    IDCANCEL:
      Result := False;
  end;
end;

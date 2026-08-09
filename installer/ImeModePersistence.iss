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

; Inno Setup script for Vocal Ink.
; Build with:  iscc /DAppVersion=1.0.0 /DSourceDir=<windeployqt staging dir> /DOutputDir=<dist> VocalInk.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\windows\VocalInk"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

; Vocal Ink's virtual mic driver ships only when deploy.ps1 staged the Microsoft-signed
; package (docs/VIRTUAL_AUDIO.md): <SourceDir>\driver\VocalInkAudio.{inf,sys,cat} + nefconw.exe.
#ifexist AddBackslash(SourceDir) + "driver\VocalInkAudio.inf"
  #ifexist AddBackslash(SourceDir) + "driver\nefconw.exe"
    #define WithVirtualMic
  #endif
#endif
#define VirtualMicHardwareId "ROOT\VocalInkAudio"
#define MediaClassGuid "4d36e96c-e325-11ce-bfc1-08002be10318"

[Setup]
AppId={{6C1E5E7A-7C3B-4E0F-9A7D-3F2B1B9D4A11}
AppName=Vocal Ink
AppVersion={#AppVersion}
AppVerName=Vocal Ink {#AppVersion}
AppPublisher=Vocal-Ink
AppPublisherURL=https://github.com/Vocal-Ink/Desktop-app
AppSupportURL=https://github.com/Vocal-Ink/Desktop-app/issues
DefaultDirName={autopf}\Vocal Ink
DefaultGroupName=Vocal Ink
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=VocalInk-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\resources\icons\VocalInk.ico
UninstallDisplayIcon={app}\VocalInk.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Start Vocal Ink in the tray when I sign in (keeps shortcuts working)"; GroupDescription: "Startup:"; Flags: unchecked
#ifdef WithVirtualMic
; Needs administrator rights, so only offered for an all-users installation.
Name: "virtualmic"; Description: "Install the Vocal Ink virtual microphone (so Discord, OBS and games can hear Vocal Ink)"; GroupDescription: "Virtual microphone:"; Check: IsAdminInstallMode
#endif

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\driver\*"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef WithVirtualMic
; Installed either way so the app can set the mic up later (Settings > Audio).
Source: "{#SourceDir}\driver\VocalInkAudio.inf"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "{#SourceDir}\driver\VocalInkAudio.sys"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "{#SourceDir}\driver\VocalInkAudio.cat"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "{#SourceDir}\driver\nefconw.exe"; DestDir: "{app}\driver"; Flags: ignoreversion
#endif

[Icons]
Name: "{autoprograms}\Vocal Ink"; Filename: "{app}\VocalInk.exe"
Name: "{autodesktop}\Vocal Ink"; Filename: "{app}\VocalInk.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Vocal Ink"; ValueData: """{app}\VocalInk.exe"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\VocalInk.exe"; Description: "{cm:LaunchProgram,Vocal Ink}"; Flags: nowait postinstall skipifsilent

#ifdef WithVirtualMic
[UninstallRun]
; Removes the device and, once unused, the driver package; then purges any copy left in
; the driver store. Runs whether the mic was set up here or later from the app.
Filename: "{app}\driver\nefconw.exe"; Parameters: "--remove-device-node --hardware-id {#VirtualMicHardwareId} --class-guid {#MediaClassGuid}"; WorkingDir: "{app}\driver"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveVocalInkVirtualMic"; Check: IsAdminInstallMode
Filename: "{app}\driver\nefconw.exe"; Parameters: "--remove-driver-store-package --inf-name VocalInkAudio.inf --class-guid {#MediaClassGuid}"; WorkingDir: "{app}\driver"; Flags: runhidden waituntilterminated; RunOnceId: "PurgeVocalInkVirtualMic"; Check: IsAdminInstallMode
#endif

[Code]
var
  VirtualMicNeedsRestart: Boolean;

#ifdef WithVirtualMic
// nefcon's devcon-compatible verb: creates the ROOT\VocalInkAudio device node if there is
// none (--no-duplicates) and installs or updates the driver on it. 3010 = restart needed.
procedure InstallVirtualMic();
var
  ResultCode: Integer;
begin
  WizardForm.StatusLabel.Caption := 'Installing the Vocal Ink virtual microphone...';
  if not Exec(ExpandConstant('{app}\driver\nefconw.exe'),
              'install "' + ExpandConstant('{app}\driver\VocalInkAudio.inf') + '" {#VirtualMicHardwareId} --no-duplicates',
              ExpandConstant('{app}\driver'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    SuppressibleMsgBox('The virtual microphone installer could not be started: ' + SysErrorMessage(ResultCode),
                       mbError, MB_OK, IDOK)
  else if ResultCode = 3010 then
    VirtualMicNeedsRestart := True
  else if ResultCode <> 0 then
    SuppressibleMsgBox('The Vocal Ink virtual microphone could not be installed (error ' + IntToStr(ResultCode) + '). ' +
                       'You can try again later in Vocal Ink''s settings, or use VB-CABLE instead.',
                       mbError, MB_OK, IDOK);
end;
#endif

procedure CurStepChanged(CurStep: TSetupStep);
begin
#ifdef WithVirtualMic
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('virtualmic') then
    InstallVirtualMic();
#endif
end;

function NeedRestart(): Boolean;
begin
  Result := VirtualMicNeedsRestart;
end;

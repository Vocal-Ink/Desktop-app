; Inno Setup script for Vocal Ink.
; Build with:  iscc /DAppVersion=0.1.0 /DSourceDir=<windeployqt staging dir> /DOutputDir=<dist> VocalInk.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\windows\VocalInk"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

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

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Vocal Ink"; Filename: "{app}\VocalInk.exe"
Name: "{autodesktop}\Vocal Ink"; Filename: "{app}\VocalInk.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Vocal Ink"; ValueData: """{app}\VocalInk.exe"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\VocalInk.exe"; Description: "{cm:LaunchProgram,Vocal Ink}"; Flags: nowait postinstall skipifsilent

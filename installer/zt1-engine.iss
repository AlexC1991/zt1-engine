; ZT1-Engine's installer (Inno Setup 6). Built by
; engine-build-resources/make_release.py --installer, which passes:
;   /DVersion=2026.10.19   the release's version
;   /DSource=<repo>        the repo's folder
;   /DExe=<path>           the engine built for release (x64, official)
;
; It installs for the player alone, with no admin prompt, into
; %LocalAppData%\Programs\ZT1-Engine: just the engine, its fonts and the
; licences. Zoo Tycoon itself stays where the player installed it, and the
; engine's settings, mods and scenario progress live in
; %LocalAppData%\zt1-engine, so installing over an older version (what the
; in-game updater does: /VERYSILENT ... /RELAUNCH) never touches them.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #define Source ".."
#endif
#ifndef Exe
  #define Exe Source + "\build-x64\Release\zt1-engine.exe"
#endif

[Setup]
AppId={{3D042E10-EB97-4317-8B95-1039FCB8036A}
AppName=ZT1-Engine
AppVersion={#Version}
AppVerName=ZT1-Engine {#Version}
AppPublisher=batty251 and the ZT1-Engine contributors
AppPublisherURL=https://github.com/AlexC1991/zt1-engine
AppSupportURL=https://github.com/AlexC1991/zt1-engine/issues
AppUpdatesURL=https://github.com/AlexC1991/zt1-engine/releases
DefaultDirName={localappdata}\Programs\ZT1-Engine
DefaultGroupName=ZT1-Engine
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Source}\dist
OutputBaseFilename=ZT1-Engine-Setup-{#Version}
SetupIconFile={#Source}\platform\windows\appicon.ico
UninstallDisplayIcon={app}\zt1-engine.exe
LicenseFile={#Source}\LICENSE.txt
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Exe}"; DestDir: "{app}"; DestName: "zt1-engine.exe"; Flags: ignoreversion
Source: "{#Source}\fonts\*.otf"; DestDir: "{app}\fonts"; Flags: ignoreversion
Source: "{#Source}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\engine-build-resources\RELEASE_README.md"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion

[Icons]
Name: "{group}\ZT1-Engine"; Filename: "{app}\zt1-engine.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall ZT1-Engine"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ZT1-Engine"; Filename: "{app}\zt1-engine.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; After a normal install: offer to play
Filename: "{app}\zt1-engine.exe"; WorkingDir: "{app}"; Description: "Play ZT1-Engine"; Flags: nowait postinstall skipifsilent
; After the in-game updater's silent install: start the new engine again
Filename: "{app}\zt1-engine.exe"; WorkingDir: "{app}"; Flags: nowait; Check: Relaunch

[Code]
function Relaunch: Boolean;
begin
  Result := WizardSilent and (Pos('/RELAUNCH', UpperCase(GetCmdTail)) > 0);
end;

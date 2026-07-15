; Inno Setup script — one-click installer for Unnamed Strategy.
; Build the game and run `cmake --install build --prefix dist` first, then
; compile this with Inno Setup (iscc packaging/installer.iss) to produce
; Output/UnnamedStrategy-Setup.exe.
;
; Inno Setup is free: https://jrsoftware.org/isdl.php

#define AppName "Unnamed Strategy"
#define AppVer  "0.1.0"
#define AppExe  "unnamed_strategy.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVer}
AppPublisher=krabysoft
DefaultDirName={autopf}\UnnamedStrategy
DefaultGroupName=Unnamed Strategy
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename=UnnamedStrategy-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
; ~1.3 GB of assets — allow room
DiskSpanning=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; Everything the `cmake --install` step assembled into ..\dist
Source: "..\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Unnamed Strategy"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall Unnamed Strategy"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Unnamed Strategy"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch Unnamed Strategy"; Flags: nowait postinstall skipifsilent

; Save data lives in %APPDATA%\krabysoft\unnamed_strategy (created at runtime),
; NOT under {app}, so uninstalling never deletes the player's progress.

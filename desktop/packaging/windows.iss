#ifndef SourceRoot
  #define SourceRoot "..\dist\Whippet"
#endif
#ifndef OutputRoot
  #define OutputRoot "..\dist"
#endif
[Setup]
AppId={{47D0DBE9-BAE3-43AD-A8B8-A0DD6D9E5027}
AppName=Whippet Desktop
AppVersion=0.1.0
AppPublisher=Whippet Desktop
DefaultDirName={localappdata}\Programs\Whippet
DefaultGroupName=Whippet
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputRoot}
OutputBaseFilename=Whippet-0.1.0-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\Whippet.exe
CloseApplications=yes
RestartApplications=no
LicenseFile=..\..\COPYING
SetupIconFile=..\..\share\pixmaps\whippet.ico
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: checkedonce
[Files]
Source: "{#SourceRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Whippet"; Filename: "{app}\bin\Whippet.exe"
Name: "{group}\Uninstall Whippet"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Whippet"; Filename: "{app}\bin\Whippet.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\bin\Whippet.exe"; Description: "Open Whippet"; Flags: nowait postinstall skipifsilent

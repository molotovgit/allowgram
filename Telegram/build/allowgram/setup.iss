#ifndef PayloadPath
  #error PayloadPath must point to the verified Release payload.
#endif
#ifndef OutputPath
  #error OutputPath must point to the installer output directory.
#endif
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{9CF76959-9AB3-4893-8B08-45CBA5B248C4}
AppName=Allowgram
AppVersion={#AppVersion}
AppPublisher=Allowgram contributors
AppComments=An independent Telegram Desktop client with a messaging allowlist.
DefaultDirName={localappdata}\Programs\Allowgram
DefaultGroupName=Allowgram
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#OutputPath}
OutputBaseFilename=Allowgram-Setup-{#AppVersion}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\..\Resources\art\allowgram\allowgram.ico
LicenseFile={#PayloadPath}\LICENSE
UninstallDisplayIcon={app}\Allowgram.exe
UninstallDisplayName=Allowgram
CloseApplications=yes
CloseApplicationsFilter=Allowgram.exe
RestartApplications=no
DisableProgramGroupPage=yes
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadPath}\Allowgram.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadPath}\AllowgramUpdater.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadPath}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadPath}\LEGAL"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadPath}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadPath}\build-info.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadPath}\modules\*"; DestDir: "{app}\modules"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Allowgram"; Filename: "{app}\Allowgram.exe"
Name: "{autodesktop}\Allowgram"; Filename: "{app}\Allowgram.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Allowgram.exe"; Description: "Launch Allowgram"; Flags: nowait postinstall skipifsilent

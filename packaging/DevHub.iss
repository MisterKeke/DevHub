#ifndef AppVersion
#define AppVersion "0.0.2"
#endif
#define AppName "DevHub"
#define AppExeName "bin\devhub.exe"
#define StageDir "..\out\release-stage"

[Setup]
AppId={{80261764-89A8-42F7-A897-0F31E27E3460}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=DevHub contributors
DefaultDirName={localappdata}\Programs\DevHub
DefaultGroupName=DevHub
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=..\out\release
OutputBaseFilename=DevHub-Setup-{#AppVersion}-x64
SetupIconFile=..\resources\branding\devhub.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
CloseApplications=yes
CloseApplicationsFilter=devhub.exe
RestartApplications=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
VersionInfoVersion={#AppVersion}
VersionInfoProductName=DevHub
VersionInfoProductVersion={#AppVersion}
VersionInfoCompany=DevHub contributors
VersionInfoDescription=DevHub per-user installer

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\DevHub"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\DevHub"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch DevHub"; Flags: postinstall nowait skipifsilent

[Code]
var
  RemoveLocalData: Boolean;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  RemoveLocalData := MsgBox(
    'Do you also want to permanently remove DevHub settings, notes, logs, and its local database from your Local AppData folder?',
    mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataPath: String;
begin
  if (CurUninstallStep = usPostUninstall) and RemoveLocalData then
  begin
    DataPath := ExpandConstant('{localappdata}\DevHub');
    if DirExists(DataPath) then
      DelTree(DataPath, True, True, True);
  end;
end;

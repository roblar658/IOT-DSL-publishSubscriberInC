; Inno Setup Script for PubSub DSL in C
; Compile with Inno Setup Compiler (ISCC.exe)

#define MyAppName "PubSub DSL"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "roblar658"
#define MyAppURL "https://github.com/roblar658/IOT-DSL-publishSubscriberInC"
#define MyAppExeName "pubsub_dsl.exe"

[Setup]
AppId={{D37E88BC-34B9-45F2-985E-5F09B6DE5298}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=PubSubDSL-Setup-v1.0.0
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "pubsub_dsl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: isreadme
Source: "scripts\*"; DestDir: "{app}\scripts"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PubSub DSL Terminal"; Filename: "cmd.exe"; Parameters: "/K ""cd /d ""{app}"" && pubsub_dsl.exe --help"""
Name: "{group}\Uninstall PubSub DSL"; Filename: "{uninstallexe}"

[Registry]
; Add install directory to system environment PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; \
    Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

#ifndef PackageRoot
  #error PackageRoot must name the canonical staged Windows x64 package directory.
#endif
#ifndef AppVersion
  #error AppVersion must name the full Companion version, including its prerelease suffix.
#endif
#ifndef InstallerOutputDir
  #define InstallerOutputDir AddBackslash(SourcePath) + "..\..\out\installer"
#endif

#define CompanionId "panelive-companion"
#define CompanionDisplayName "PaneLive Companion"
#define PluginId "panelive-companion-obs-plugin"
#define CompanionExe CompanionId + ".exe"
#define PluginDll PluginId + ".dll"
#define SetupBaseName CompanionId + "-" + AppVersion + "-windows-x64-setup"
; Compile the GUI integration component with the same compiler and payload.
#define IntegrationCompileArgs "/DPackageRoot=" + AddQuotes(PackageRoot) + " /DAppVersion=" + AddQuotes(AppVersion)
#ifdef InstallerSignTool
  #define IntegrationCompileArgs IntegrationCompileArgs + " /DInstallerSignTool=" + AddQuotes(InstallerSignTool)
#endif
#if Exec(CompilerPath + "ISCC.exe", IntegrationCompileArgs + " " + AddQuotes(SourcePath + "panelive-companion-integrations.iss"), SourcePath, 1, SW_HIDE) != 0
  #error Could not compile the PaneLive Companion integration component.
#endif

#define CompanionIconFile AddBackslash(SourcePath) + "..\..\platform\windows\resources\panelive-companion.ico"

[Setup]
AppId={#CompanionId}
AppName={#CompanionDisplayName}
AppVersion={#AppVersion}
AppVerName={#CompanionDisplayName} {#AppVersion}
AppPublisher=Clownfish Web Artur Cichosz and contributors
AppCopyright=Copyright 2026 Clownfish Web Artur Cichosz and contributors
AppComments=PaneLive Teleprompter with optional OBS Studio, vMix, and Streamlabs Desktop adapters.
DefaultDirName={localappdata}\Programs\{#CompanionId}
DefaultGroupName={#CompanionDisplayName}
DisableProgramGroupPage=auto
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=no
PrivilegesRequired=lowest
SetupArchitecture=x64
ArchitecturesAllowed=x64os
MinVersion=10.0
WizardStyle=modern dynamic
WizardSizePercent=120,120
Compression=lzma2/ultra64
SolidCompression=yes
MergeDuplicateFiles=yes
CloseApplications=yes
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
RedirectionGuard=yes
SetupIconFile={#CompanionIconFile}
OutputDir={#InstallerOutputDir}
OutputBaseFilename={#SetupBaseName}
OutputManifestFile={#SetupBaseName}-contents.txt
UninstallDisplayName={#CompanionDisplayName}
UninstallDisplayIcon={app}\bin\{#CompanionExe}
UninstallFilesDir={app}\uninstall
LicenseFile={#PackageRoot}\share\{#CompanionId}\LICENSE
InfoBeforeFile={#SourcePath}\INSTALLER_NOTICE.txt
VersionInfoCompany=Clownfish Web Artur Cichosz and contributors
VersionInfoDescription={#CompanionDisplayName} installer
VersionInfoOriginalFileName={#SetupBaseName}.exe
VersionInfoProductName={#CompanionDisplayName}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoTextVersion={#AppVersion}
#ifdef InstallerSignTool
SignTool={#InstallerSignTool}
SignedUninstaller=yes
#else
SignedUninstaller=no
#endif

[Tasks]
Name: "autostart"; Description: "Start the Companion when I sign in"; GroupDescription: "Background operation:"; Flags: unchecked
Name: "firewall"; Description: "Allow local clients on private networks (administrator approval when needed)"; GroupDescription: "Network access:"
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#PackageRoot}\redist\WindowsAppRuntimeInstall-x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall ignoreversion
Source: "{#PackageRoot}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#PackageRoot}\share\{#CompanionId}\LICENSE"; DestDir: "{app}\share\{#CompanionId}"; Flags: ignoreversion
Source: "{#PackageRoot}\share\{#CompanionId}\NOTICE"; DestDir: "{app}\share\{#CompanionId}"; Flags: ignoreversion
Source: "{#PackageRoot}\share\{#CompanionId}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\share\{#CompanionId}"; Flags: ignoreversion
Source: "{#PackageRoot}\share\{#CompanionId}\licenses\*"; DestDir: "{app}\share\{#CompanionId}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageRoot}\share\{#CompanionId}\obs-plugin\LICENSE"; DestDir: "{app}\share\{#CompanionId}\obs-plugin"; Flags: ignoreversion
Source: "{#PackageRoot}\installer-tools\panelive-companion-integrations.exe"; DestDir: "{app}\installer-tools"; Flags: ignoreversion

[Icons]
Name: "{group}\{#CompanionDisplayName}"; Filename: "{app}\bin\{#CompanionExe}"; Parameters: "{code:GetProviderLaunchArguments}"; WorkingDir: "{app}\bin"
Name: "{group}\Documentation"; Filename: "{app}\share\{#CompanionId}"
Name: "{autodesktop}\{#CompanionDisplayName}"; Filename: "{app}\bin\{#CompanionExe}"; Parameters: "{code:GetProviderLaunchArguments}"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: string; ValueName: "InstallDirectory"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: string; ValueName: "PendingObsDirectory"; ValueData: "{code:GetPreviousObsDirectory}"
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: string; ValueName: "InstalledProviders"; ValueData: "{code:GetSelectedProviders}"
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: string; ValueName: "ObsDirectory"; ValueData: "{code:GetObsDirectory}"; Check: IsObsProviderSelected
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: none; ValueName: "ObsDirectory"; Check: not IsObsProviderSelected; Flags: deletevalue
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: string; ValueName: "VmixDirectory"; ValueData: "{code:GetVmixDirectory}"; Check: HasSelectedVmixDirectory
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: none; ValueName: "VmixDirectory"; Check: not HasSelectedVmixDirectory; Flags: deletevalue
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: string; ValueName: "StreamlabsDirectory"; ValueData: "{code:GetStreamlabsDirectory}"; Check: IsStreamlabsProviderSelected
Root: HKCU; Subkey: "SOFTWARE\{#CompanionId}"; ValueType: none; ValueName: "StreamlabsDirectory"; Check: not IsStreamlabsProviderSelected; Flags: deletevalue
Root: HKCU; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "{#CompanionId}"; Flags: uninsdeletevalue

[InstallDelete]
; Remove installation helpers from earlier evaluation packages.
Type: files; Name: "{app}\Install-ObsPlugin.ps1"
Type: files; Name: "{app}\uninstall-tools\Set-PaneLiveCompanionFirewall.ps1"
Type: files; Name: "{app}\uninstall-tools\Remove-PaneLiveCompanionInstallationData.ps1"
Type: dirifempty; Name: "{app}\uninstall-tools"
Type: filesandordirs; Name: "{app}\obs-plugin"

[Run]
Filename: "{app}\bin\{#CompanionExe}"; Parameters: "{code:GetProviderLaunchArguments}"; WorkingDir: "{app}\bin"; Description: "Launch {#CompanionDisplayName}"; Check: CanLaunchCompanion; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
const
  StreamlabsMinimumVersionMS = $00010015; { Major 1, minor 21 }
  StreamlabsMinimumVersionLS = $00090000; { Patch 9, file revision 0 }

var
  ProviderSelectionPage: TInputOptionWizardPage;
  ObsDirectoryPage: TInputDirWizardPage;
  VmixDirectoryPage: TInputDirWizardPage;
  StreamlabsDirectoryPage: TInputDirWizardPage;
  PurgeCompanionIdentity: Boolean;
  PreviousObsDirectory: String;
  UninstallObsDirectory: String;
  UninstallPreviousObsDirectory: String;
  IntegrationFailed: Boolean;
  RuntimeInstalled: Boolean;

#include "installer-process-control.iss"

function GetRequestedProviders(): String;
begin
  Result := Lowercase(Trim(ExpandConstant('{param:PROVIDERS|}')));
  StringChangeEx(Result, ' ', '', True);
end;

function IsValidProviderList(const Providers: String): Boolean;
var
  Remaining: String;
  Provider: String;
  Separator: Integer;
  SeenObs: Boolean;
  SeenVmix: Boolean;
  SeenStreamlabs: Boolean;
  SeenTeleprompter: Boolean;
begin
  Result := False;
  Remaining := Providers;
  SeenObs := False;
  SeenVmix := False;
  SeenStreamlabs := False;
  SeenTeleprompter := False;
  if Remaining = '' then
    exit;
  while Remaining <> '' do
  begin
    Separator := Pos(',', Remaining);
    if Separator = 1 then
      exit;
    if Separator = 0 then
    begin
      Provider := Remaining;
      Remaining := '';
    end
    else
    begin
      Provider := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
      if Remaining = '' then
        exit;
    end;
    if Provider = 'obs' then
    begin
      if SeenObs then exit;
      SeenObs := True;
    end
    else if Provider = 'vmix' then
    begin
      if SeenVmix then exit;
      SeenVmix := True;
    end
    else if Provider = 'streamlabs' then
    begin
      if SeenStreamlabs then exit;
      SeenStreamlabs := True;
    end
    else if Provider = 'companion-teleprompter' then
    begin
      if SeenTeleprompter then exit;
      SeenTeleprompter := True;
    end
    else
      exit;
  end;
  Result := SeenObs or SeenVmix or SeenStreamlabs or SeenTeleprompter;
end;

function InitializeSetup(): Boolean;
var
  Providers: String;
begin
  if IsAdmin then
  begin
    MsgBox('Run this installer normally, without Run as administrator. Only OBS plugin and ' +
      'firewall changes request administrator approval when needed.', mbError, MB_OK);
    Result := False;
    exit;
  end;
  if RegKeyExists(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#CompanionId}_is1') then
  begin
    MsgBox('An older all-users installation exists. Uninstall it first and keep your profile, ' +
      'then run this per-user installer normally.', mbError, MB_OK);
    Result := False;
    exit;
  end;
  Providers := GetRequestedProviders();
  Result := (Providers = '') or IsValidProviderList(Providers);
  if not Result then
    MsgBox('/PROVIDERS must contain unique obs, vmix, streamlabs, and/or companion-teleprompter identifiers.', mbError, MB_OK)
  else if WizardSilent() and (Providers = '') then
  begin
    MsgBox('Silent installation requires a non-empty /PROVIDERS list containing obs, vmix, ' +
      'streamlabs, and/or companion-teleprompter.', mbError, MB_OK);
    Result := False;
  end;
end;

function HasCommandLineParameter(const Expected: String): Boolean;
var
  Index: Integer;
begin
  Result := False;
  for Index := 1 to ParamCount do
  begin
    if CompareText(ParamStr(Index), Expected) = 0 then
    begin
      Result := True;
      exit;
    end;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  if IsAdmin then
  begin
    MsgBox('Run this uninstaller normally so it uses your own Windows profile.', mbError, MB_OK);
    Result := False;
    exit;
  end;
  RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'ObsDirectory', UninstallObsDirectory);
  RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'PendingObsDirectory', UninstallPreviousObsDirectory);
  PurgeCompanionIdentity := HasCommandLineParameter('/PURGEIDENTITY');
  if (not UninstallSilent()) and (not PurgeCompanionIdentity) then
  begin
    PurgeCompanionIdentity := MsgBox(
      'Keep the Companion identity and paired-device credentials for a later reinstall?'#13#10#13#10 +
      'Choose Yes to preserve the identity, TLS certificate, pairing records, protected secrets, and settings.'#13#10 +
      'Choose No only for a permanent factory reset. Every client will need to pair again.',
      mbConfirmation,
      MB_YESNO or MB_DEFBUTTON1) = IDNO;
  end;
  Result := True;
end;

function IsObsDirectory(const Directory: String): Boolean;
begin
  Result :=
    FileExists(AddBackslash(Directory) + 'bin\64bit\obs64.exe') and
    DirExists(AddBackslash(Directory) + 'obs-plugins\64bit');
end;

function DetectObsDirectory(): String;
var
  Candidate: String;
begin
  Result := ExpandConstant('{param:OBSROOT|}');
  if IsObsDirectory(Result) then
    exit;

  if RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'ObsDirectory', Candidate) and
     IsObsDirectory(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Candidate) and
     IsObsDirectory(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  Candidate := ExpandConstant('{commonpf64}\obs-studio');
  if IsObsDirectory(Candidate) then
    Result := Candidate
  else
    Result := '';
end;

function IsVmixDirectory(const Directory: String): Boolean;
begin
  Result := (Directory <> '') and FileExists(AddBackslash(Directory) + 'vMix64.exe');
end;

// vMix publishes no App Paths entry, so its own installer record is the authoritative source.
// Sibling products such as "vMix Social" share the uninstall root, so the product name must match
// exactly rather than by prefix.
function FindVmixDirectoryInUninstallRegistry(const RootKey: Integer): String;
var
  SubkeyNames: TArrayOfString;
  Index: Integer;
  Subkey: String;
  DisplayName: String;
  Location: String;
begin
  Result := '';
  if not RegGetSubkeyNames(
       RootKey, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', SubkeyNames) then
    exit;

  for Index := 0 to GetArrayLength(SubkeyNames) - 1 do
  begin
    Subkey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + SubkeyNames[Index];
    if RegQueryStringValue(RootKey, Subkey, 'DisplayName', DisplayName) and
       (CompareText(Trim(DisplayName), 'vMix') = 0) and
       RegQueryStringValue(RootKey, Subkey, 'InstallLocation', Location) and
       IsVmixDirectory(Location) then
    begin
      Result := RemoveBackslashUnlessRoot(Location);
      exit;
    end;
  end;
end;

function DetectVmixDirectory(): String;
var
  Candidate: String;
begin
  Result := ExpandConstant('{param:VMIXROOT|}');
  if IsVmixDirectory(Result) then
  begin
    Result := RemoveBackslashUnlessRoot(Result);
    exit;
  end;

  if RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'VmixDirectory', Candidate) and
     IsVmixDirectory(Candidate) then
  begin
    Result := RemoveBackslashUnlessRoot(Candidate);
    exit;
  end;

  // The vMix setup is 32-bit, so its uninstall entry normally lives in the 32-bit view.
  Result := FindVmixDirectoryInUninstallRegistry(HKLM32);
  if Result <> '' then
    exit;
  Result := FindVmixDirectoryInUninstallRegistry(HKLM64);
  if Result <> '' then
    exit;
  Result := FindVmixDirectoryInUninstallRegistry(HKCU);
  if Result <> '' then
    exit;

  Candidate := ExpandConstant('{commonpf32}\vMix');
  if IsVmixDirectory(Candidate) then
    Result := Candidate
  else
    Result := '';
end;

function IsStreamlabsDirectory(const Directory: String): Boolean;
var
  VersionMS, VersionLS: Cardinal;
  Executable: String;
begin
  Executable := AddBackslash(Directory) + 'Streamlabs OBS.exe';
  Result := (Directory <> '') and FileExists(Executable) and
    GetVersionNumbers(Executable, VersionMS, VersionLS) and
    ((VersionMS > StreamlabsMinimumVersionMS) or
     ((VersionMS = StreamlabsMinimumVersionMS) and (VersionLS >= StreamlabsMinimumVersionLS)));
end;

function StreamlabsDirectoryFromUninstallString(const UninstallString: String): String;
var
  ClosingQuote: Integer;
  Executable: String;
begin
  Result := '';
  if Copy(UninstallString, 1, 1) <> '"' then
    exit;
  ClosingQuote := Pos('"', Copy(UninstallString, 2, Length(UninstallString) - 1));
  if ClosingQuote = 0 then
    exit;
  Executable := Copy(UninstallString, 2, ClosingQuote - 1);
  Result := ExtractFileDir(Executable);
end;

function FindStreamlabsDirectoryInUninstallRegistry(const RootKey: Integer): String;
var
  SubkeyNames: TArrayOfString;
  Index: Integer;
  Subkey: String;
  DisplayName: String;
  Publisher: String;
  UninstallString: String;
  Candidate: String;
begin
  Result := '';
  if not RegGetSubkeyNames(
       RootKey, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', SubkeyNames) then
    exit;
  for Index := 0 to GetArrayLength(SubkeyNames) - 1 do
  begin
    Subkey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + SubkeyNames[Index];
    if RegQueryStringValue(RootKey, Subkey, 'DisplayName', DisplayName) and
       (CompareText(Copy(Trim(DisplayName), 1, Length('Streamlabs Desktop ')),
                    'Streamlabs Desktop ') = 0) and
       RegQueryStringValue(RootKey, Subkey, 'Publisher', Publisher) and
       (CompareText(Trim(Publisher), 'General Workings, Inc.') = 0) and
       RegQueryStringValue(RootKey, Subkey, 'UninstallString', UninstallString) then
    begin
      Candidate := StreamlabsDirectoryFromUninstallString(UninstallString);
      if IsStreamlabsDirectory(Candidate) then
      begin
        Result := RemoveBackslashUnlessRoot(Candidate);
        exit;
      end;
    end;
  end;
end;

function DetectStreamlabsDirectory(): String;
var
  Candidate: String;
begin
  Result := ExpandConstant('{param:STREAMLABSROOT|}');
  if IsStreamlabsDirectory(Result) then
  begin
    Result := RemoveBackslashUnlessRoot(Result);
    exit;
  end;
  if RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'StreamlabsDirectory', Candidate) and
     IsStreamlabsDirectory(Candidate) then
  begin
    Result := RemoveBackslashUnlessRoot(Candidate);
    exit;
  end;
  Result := FindStreamlabsDirectoryInUninstallRegistry(HKLM64);
  if Result = '' then
    Result := FindStreamlabsDirectoryInUninstallRegistry(HKCU);
end;

function IsObsProviderSelected(): Boolean;
begin
  Result := Assigned(ProviderSelectionPage) and ProviderSelectionPage.Values[0];
end;

function IsVmixProviderSelected(): Boolean;
begin
  Result := Assigned(ProviderSelectionPage) and ProviderSelectionPage.Values[1];
end;

function IsStreamlabsProviderSelected(): Boolean;
begin
  Result := Assigned(ProviderSelectionPage) and ProviderSelectionPage.Values[2];
end;

function GetSelectedProviders(Param: String): String;
begin
  Result := '';
  if IsObsProviderSelected() then
    Result := 'obs';
  if IsVmixProviderSelected() then
  begin
    if Result <> '' then Result := Result + ',';
    Result := Result + 'vmix';
  end;
  if IsStreamlabsProviderSelected() then
  begin
    if Result <> '' then Result := Result + ',';
    Result := Result + 'streamlabs';
  end;
  if Result <> '' then Result := Result + ',';
  Result := Result + 'companion-teleprompter';
end;

function GetProviderArguments(Param: String): String;
begin
  Result := '--providers ' + GetSelectedProviders('');
end;

function GetProviderLaunchArguments(Param: String): String;
begin
  Result := GetProviderArguments('') + ' --open-management';
end;

procedure InitializeWizard();
var
  Providers: String;
begin
  if RegQueryStringValue(
       HKCU, 'SOFTWARE\{#CompanionId}', 'ObsDirectory', PreviousObsDirectory) and
     (not IsObsDirectory(PreviousObsDirectory)) then
    PreviousObsDirectory := '';

  RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'PendingObsDirectory', Providers);
  if Providers <> '' then PreviousObsDirectory := Providers;

  ProviderSelectionPage := CreateInputOptionPage(
    wpSelectDir,
    'Select Companion adapters',
    'Which adapters should the Companion support?',
    'PaneLive Teleprompter is built in and always included. Select any additional production ' +
      'adapters, or leave all optional adapters unchecked to use Teleprompter alone.',
    False,
    True);
  ProviderSelectionPage.Add('OBS Studio 28 through 32.x');
  ProviderSelectionPage.Add('vMix 29');
  ProviderSelectionPage.Add('Streamlabs Desktop 1.21.9 or newer');
  ProviderSelectionPage.Add('PaneLive Teleprompter (required)');
  ProviderSelectionPage.Values[3] := True;
  ProviderSelectionPage.CheckListBox.ItemEnabled[3] := False;

  Providers := GetRequestedProviders();
  if Providers = '' then
    RegQueryStringValue(HKCU, 'SOFTWARE\{#CompanionId}', 'InstalledProviders', Providers);
  ProviderSelectionPage.Values[0] :=
    Pos(',obs,', ',' + Lowercase(Providers) + ',') > 0;
  ProviderSelectionPage.Values[1] :=
    Pos(',vmix,', ',' + Lowercase(Providers) + ',') > 0;
  ProviderSelectionPage.Values[2] :=
    Pos(',streamlabs,', ',' + Lowercase(Providers) + ',') > 0;

  ObsDirectoryPage := CreateInputDirPage(
    ProviderSelectionPage.ID,
    'Select the OBS Studio installation',
    'Where is 64-bit OBS Studio installed?',
    'A valid OBS Studio installation is required only when the OBS provider is selected. ' +
      'The installer places {#PluginDll} in this directory. Close OBS before continuing.',
    False,
    '');
  ObsDirectoryPage.Add('OBS Studio directory:');
  ObsDirectoryPage.Values[0] := DetectObsDirectory();

  VmixDirectoryPage := CreateInputDirPage(
    ObsDirectoryPage.ID,
    'Select the vMix installation',
    'Where is vMix installed?',
    'Select the local vMix installation, or leave this empty when vMix runs on another ' +
      'computer. Configure the vMix connection and monitoring outputs in Companion after setup.',
    False,
    '');
  VmixDirectoryPage.Add('vMix directory:');
  VmixDirectoryPage.Values[0] := DetectVmixDirectory();

  StreamlabsDirectoryPage := CreateInputDirPage(
    VmixDirectoryPage.ID,
    'Select the Streamlabs Desktop installation',
    'Where is supported Streamlabs Desktop installed?',
    'The Companion uses the documented local named pipe. It installs no Streamlabs helper, ' +
      'changes no Streamlabs settings, and creates no Streamlabs firewall rule.',
    False,
    '');
  StreamlabsDirectoryPage.Add('Streamlabs Desktop directory:');
  StreamlabsDirectoryPage.Values[0] := DetectStreamlabsDirectory();
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = ObsDirectoryPage.ID) and IsObsProviderSelected() and
     (not IsObsDirectory(ObsDirectoryPage.Values[0])) then
  begin
    MsgBox(
      'The selected directory is not a supported 64-bit OBS Studio installation.'#13#10#13#10 +
      'Expected: bin\64bit\obs64.exe and obs-plugins\64bit',
      mbError,
      MB_OK);
    Result := False;
  end
  // A remote vMix host does not require a local vMix installation.
  else if (CurPageID = VmixDirectoryPage.ID) and (Trim(VmixDirectoryPage.Values[0]) <> '') and
     (not IsVmixDirectory(VmixDirectoryPage.Values[0])) then
  begin
    MsgBox(
      'The selected directory is not a vMix installation.'#13#10#13#10 +
      'Expected: vMix64.exe'#13#10#13#10 +
      'Leave the field empty when vMix runs on another computer.',
      mbError,
      MB_OK);
    Result := False;
  end
  else if (CurPageID = StreamlabsDirectoryPage.ID) and IsStreamlabsProviderSelected() and
     (not IsStreamlabsDirectory(StreamlabsDirectoryPage.Values[0])) then
  begin
    MsgBox(
      'The selected directory is not supported Streamlabs Desktop 1.21.9 or newer.'#13#10#13#10 +
      'Expected: Streamlabs OBS.exe with file version 1.21.9 or newer',
      mbError,
      MB_OK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if IsObsProviderSelected() and (not IsObsDirectory(ObsDirectoryPage.Values[0])) then
    Result := 'A valid 64-bit OBS Studio directory is required for the OBS provider.'
  else if IsStreamlabsProviderSelected() and
     (not IsStreamlabsDirectory(StreamlabsDirectoryPage.Values[0])) then
    Result := 'A supported Streamlabs Desktop directory is required.';
  if Result <> '' then exit;
  try
    StopInstalledCompanion(ExpandConstant('{app}'));
  except
    Result := GetExceptionMessage();
    exit;
  end;
  if RuntimeInstalled then exit;
  ExtractTemporaryFile('WindowsAppRuntimeInstall-x64.exe');
  if not Exec(ExpandConstant('{tmp}\WindowsAppRuntimeInstall-x64.exe'), '--quiet', '',
      SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Result := 'Windows App Runtime could not start: ' + SysErrorMessage(ExitCode)
  else if ExitCode <> 0 then
    Result := 'Windows App Runtime could not be installed for your Windows account (code ' +
      IntToStr(ExitCode) + '). Check Windows app installation policy and retry.'
  else RuntimeInstalled := True;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result :=
    (Assigned(ObsDirectoryPage) and (PageID = ObsDirectoryPage.ID) and
      (not IsObsProviderSelected())) or
    (Assigned(VmixDirectoryPage) and (PageID = VmixDirectoryPage.ID) and
      (not IsVmixProviderSelected())) or
    (Assigned(StreamlabsDirectoryPage) and (PageID = StreamlabsDirectoryPage.ID) and
      (not IsStreamlabsProviderSelected()));
end;

function GetObsDirectory(Param: String): String;
begin
  if Assigned(ObsDirectoryPage) then
    Result := RemoveBackslashUnlessRoot(ObsDirectoryPage.Values[0])
  else
    Result := '';
end;

function GetVmixDirectory(Param: String): String;
begin
  if Assigned(VmixDirectoryPage) then
    Result := RemoveBackslashUnlessRoot(Trim(VmixDirectoryPage.Values[0]))
  else
    Result := '';
end;

// Records the location only when a vMix installation was actually selected or detected.
function HasSelectedVmixDirectory(): Boolean;
begin
  Result := IsVmixProviderSelected() and IsVmixDirectory(GetVmixDirectory(''));
end;

function GetStreamlabsDirectory(Param: String): String;
begin
  if Assigned(StreamlabsDirectoryPage) then
    Result := RemoveBackslashUnlessRoot(Trim(StreamlabsDirectoryPage.Values[0]))
  else
    Result := '';
end;

function GetPreviousObsDirectory(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(PreviousObsDirectory);
end;

function RunChecked(const Executable, Arguments, Operation: String): Boolean;
var
  ExitCode: Integer;
begin
  Result := Exec(Executable, Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  Result := Result and (ExitCode = 0);
  if not Result then
  begin
    Log(Operation + ' failed: ' + IntToStr(ExitCode));
    if ExitCode < 0 then
      SuppressibleMsgBox(Operation + ' crashed (Windows status ' + IntToStr(ExitCode) + '). ' +
        'Setup could not finish. This is not an administrator permission refusal. ' +
        'Keep the setup log when reporting this error.', mbError, MB_OK, IDOK)
    else if ExitCode = 1223 then
      SuppressibleMsgBox(Operation + ' was cancelled at the Windows approval prompt. ' +
        'Run setup again to complete the selected integration.', mbError, MB_OK, IDOK)
    else
      SuppressibleMsgBox(Operation + ' did not complete (code ' + IntToStr(ExitCode) + '). ' +
        'Check that OBS is closed and complete any requested Windows approval before retrying.',
        mbError, MB_OK, IDOK);
  end;
end;

function IntegrationArgument(const Name, Value: String): String;
begin
  if Pos('"', Value) <> 0 then RaiseException('An installation path contains a quotation mark.');
  Result := ' /' + Name + '="' + Value + '"';
end;

function RunIntegrations(const InstallObs, RemoveObs, RemoveObs2, FirewallAction: String): Boolean;
var
  Arguments: String;
begin
  Arguments := '/VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART' +
    IntegrationArgument('APPROOT', ExpandConstant('{app}')) +
    IntegrationArgument('OBSINSTALL', InstallObs) +
    IntegrationArgument('OBSREMOVE', RemoveObs) +
    IntegrationArgument('OBSREMOVE2', RemoveObs2) +
    IntegrationArgument('FIREWALL', FirewallAction);
  Result := RunChecked(ExpandConstant('{app}\installer-tools\panelive-companion-integrations.exe'),
    Arguments, 'OBS plugin and private-network firewall configuration');
end;

function CleanupProfile(): Boolean;
var
  Profile, Diagnostic: String;
begin
  { Only the original, unelevated user's fixed application directory is eligible. }
  Profile := ExpandConstant('{localappdata}\{#CompanionId}');
  Result := True;
  if PurgeCompanionIdentity then
  begin
    if DirExists(Profile) then Result := DelTree(Profile, True, True, True);
  end
  else
  begin
    Diagnostic := AddBackslash(Profile) + 'civetweb-error.log';
    if FileExists(Diagnostic) then Result := DeleteFile(Diagnostic);
    Diagnostic := AddBackslash(Profile) + 'endurance-logs';
    if DirExists(Diagnostic) then
      if not DelTree(Diagnostic, True, True, True) then Result := False;
  end;
  if not Result then
    SuppressibleMsgBox('Companion data cleanup did not complete. Close Companion and retry.',
      mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Startup, InstallObs, RemoveObs, FirewallAction: String;
begin
  if CurStep <> ssPostInstall then exit;
  IntegrationFailed := True;
  InstallObs := '';
  RemoveObs := '';
  if IsObsProviderSelected() then InstallObs := GetObsDirectory('');
  if (PreviousObsDirectory <> '') and (CompareText(PreviousObsDirectory, InstallObs) <> 0) then
    RemoveObs := PreviousObsDirectory;
  FirewallAction := 'remove';
  if WizardIsTaskSelected('firewall') then FirewallAction := 'add';
  if not RunIntegrations(InstallObs, RemoveObs, '', FirewallAction) then exit;
  Startup := 'disabled';
  if WizardIsTaskSelected('autostart') then Startup := 'enabled';
  if not RunChecked(ExpandConstant('{app}\bin\{#CompanionExe}'),
      GetProviderArguments('') + ' --set-sign-in-startup ' + Startup,
      'Current-user sign-in startup configuration') then exit;
  RegDeleteValue(HKCU, 'SOFTWARE\{#CompanionId}', 'PendingObsDirectory');
  IntegrationFailed := False;
end;

function CanLaunchCompanion(): Boolean;
begin
  Result := not IntegrationFailed;
end;

function GetCustomSetupExitCode(): Integer;
begin
  Result := 0;
  if IntegrationFailed then Result := 1;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then exit;
  StopInstalledCompanion(ExpandConstant('{app}'));
  if not RunIntegrations('', UninstallObsDirectory, UninstallPreviousObsDirectory, 'remove') then
    Abort;
  if not CleanupProfile() then Abort;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and IntegrationFailed then
    WizardForm.FinishedLabel.Caption := 'The application files were installed, but a selected ' +
      'integration did not complete. Run this installer again to finish setup.';
end;

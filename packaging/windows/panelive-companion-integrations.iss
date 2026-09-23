#ifndef PackageRoot
  #error PackageRoot is required.
#endif
#ifndef AppVersion
  #error AppVersion is required.
#endif

; Compiled installer code only: no command shell, PowerShell, or script host.
; This small GUI executable is also used by the uninstaller. User-profile cleanup
; deliberately remains in the unelevated parent, including with alternate UAC credentials.
[Setup]
AppId=panelive-companion-integrations
AppName=PaneLive Companion
AppVersion={#AppVersion}
AppPublisher=Clownfish Web Artur Cichosz and contributors
VersionInfoDescription=PaneLive Companion installation permissions
VersionInfoProductName=PaneLive Companion
SetupIconFile=..\..\platform\windows\resources\panelive-companion.ico
PrivilegesRequired=lowest
SetupArchitecture=x64
ArchitecturesAllowed=x64os
MinVersion=10.0
CreateAppDir=no
Uninstallable=no
CreateUninstallRegKey=no
DisableWelcomePage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
OutputDir={#PackageRoot}\installer-tools
OutputBaseFilename=panelive-companion-integrations
Compression=lzma2
SolidCompression=yes
#ifdef InstallerSignTool
SignTool={#InstallerSignTool}
#endif

[Files]
Source: "{#PackageRoot}\obs-plugin\obs-plugins\64bit\panelive-companion-obs-plugin.dll"; Flags: dontcopy
Source: "{#PackageRoot}\obs-plugin\data\obs-plugins\panelive-companion-obs-plugin\LICENSE"; DestName: "obs-plugin-license"; Flags: dontcopy
Source: "{#PackageRoot}\obs-plugin\data\obs-plugins\panelive-companion-obs-plugin\locale\en-US.ini"; Flags: dontcopy

[Code]
#include "installer-integrations-code.iss"

var
  IntegrationExitCode: Integer;

function ForwardParameter(const Name: String): String;
var
  Value: String;
begin
  Value := ExpandConstant('{param:' + Name + '|}');
  if (Name = 'FIREWALL') and (Value = '') then Value := 'keep';
  if Pos('"', Value) <> 0 then RaiseException('Invalid installation parameter.');
  Result := ' /' + Name + '="' + Value + '"';
end;

function InitializeSetup(): Boolean;
begin
  IntegrationExitCode := 1;
  Result := WizardSilent();
  if not Result then
    MsgBox('This component is run by the PaneLive Companion installer. Run the main setup executable.', mbInformation, MB_OK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Arguments: String;
  ExitCode: Integer;
begin
  if CurStep <> ssInstall then exit;
  try
    ExecuteIntegrationPlan();
    IntegrationExitCode := 0;
  except
    Log(GetExceptionMessage());
    if IntegrationNeedsElevation and (not IsAdmin) and
        (ExpandConstant('{param:ELEVATED|0}') <> '1') then
    begin
      { Retry the entire idempotent plan once, combining OBS and firewall approval. }
      Arguments := '/VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART /ELEVATED=1' +
        ForwardParameter('APPROOT') + ForwardParameter('OBSINSTALL') +
        ForwardParameter('OBSREMOVE') + ForwardParameter('OBSREMOVE2') +
        ForwardParameter('FIREWALL');
      ShellExec('runas', ExpandConstant('{srcexe}'), Arguments, '',
        SW_HIDE, ewWaitUntilTerminated, ExitCode);
      IntegrationExitCode := ExitCode;
      if IntegrationExitCode = 0 then exit;
      Log('Administrator operation did not complete: ' + IntToStr(IntegrationExitCode));
    end;
    if IntegrationExitCode = 0 then IntegrationExitCode := 1;
  end;
end;

function GetCustomSetupExitCode(): Integer;
begin
  Result := IntegrationExitCode;
end;

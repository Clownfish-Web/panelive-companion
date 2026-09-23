const
  PluginId = 'panelive-companion-obs-plugin';
  CompanionId = 'panelive-companion';

type
  { Windows SDK netfw.h interfaces. Typed calls avoid temporary Automation variants
    when passing or receiving rule objects, and preserve the native HRESULT. }
  INetFwRule = interface(IDispatch)
    '{AF230D27-BABA-4E42-ACED-F524F22CFCE2}'
    function GetName(out Name: WideString): HResult;
  end;
  INetFwRules = interface(IDispatch)
    '{9C4C6277-5027-441E-AFAE-CA1F542DA009}'
    function GetCount(var Count: Integer): HResult;
    function Add(Rule: INetFwRule): HResult;
    function Remove(Name: WideString): HResult;
    function Item(Name: WideString; out Rule: INetFwRule): HResult;
  end;

var
  IntegrationNeedsElevation: Boolean;

function WinCreateFile(Name: String; Access, Share: Cardinal; Security: NativeInt;
  Creation, Flags: Cardinal; Template: NativeInt): NativeInt;
  external 'CreateFileW@kernel32.dll stdcall';
function WinCloseHandle(Handle: NativeInt): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function WinCopyFile(Source, Destination: String; FailIfExists: Boolean): Boolean;
  external 'CopyFileW@kernel32.dll stdcall';
function WinMoveFile(Source, Destination: String; Flags: Cardinal): Boolean;
  external 'MoveFileExW@kernel32.dll stdcall';
function WinCreateDirectory(Name: String; Security: NativeInt): Boolean;
  external 'CreateDirectoryW@kernel32.dll stdcall';

procedure FileOperationFailed(const Operation: String);
var
  Code: Integer;
begin
  Code := DLLGetLastError();
  IntegrationNeedsElevation := Code = 5;
  RaiseException(Operation + ': ' + SysErrorMessage(Code));
end;

function CheckedDirectory(const Value: String): String;
begin
  if (Value = '') or (not PathIsRooted(Value)) or (Pos('"', Value) <> 0) then
    RaiseException('An absolute installation directory is required.');
  Result := RemoveBackslashUnlessRoot(ExpandFileName(Value));
  if (Length(Result) <= 3) or (Copy(Result, 1, 2) = '\\') then
    RaiseException('An installation directory must be on a local drive, below its root.');
end;

procedure EnsureDirectory(const Directory: String);
begin
  if DirExists(Directory) then exit;
  EnsureDirectory(ExtractFileDir(Directory));
  if not WinCreateDirectory(Directory, 0) then
    FileOperationFailed('Could not create the OBS plugin directory');
end;

procedure RequireObsClosed();
var
  Locator, Services, Processes: Variant;
begin
  Locator := CreateOleObject('WbemScripting.SWbemLocator');
  Services := Locator.ConnectServer('', 'root\CIMV2');
  Processes := Services.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
  if Processes.Count <> 0 then
    RaiseException('Close OBS Studio before changing its plugin.');
end;

procedure ChangeObsPlugin(const Directory: String; Remove: Boolean);
var
  Root, Transaction, LeaseName, Recovery, Failure: String;
  Paths, Sources, Targets, Backups, Staged: array[0..2] of String;
  Existed, Applied: array[0..2] of Boolean;
  I, Attempt: Integer;
  Lease, FileHandle: NativeInt;
  Changed, PreserveRecovery, RollbackFailed, Denied: Boolean;
  Found: TFindRec;
begin
  if Directory = '' then exit;
  Root := CheckedDirectory(Directory);
  Paths[0] := 'obs-plugins\64bit\' + PluginId + '.dll';
  Paths[1] := 'data\obs-plugins\' + PluginId + '\LICENSE';
  Paths[2] := 'data\obs-plugins\' + PluginId + '\locale\en-US.ini';
  Sources[0] := 'panelive-companion-obs-plugin.dll';
  Sources[1] := 'obs-plugin-license';
  Sources[2] := 'en-US.ini';
  Changed := False;
  for I := 0 to 2 do
  begin
    Targets[I] := AddBackslash(Root) + Paths[I];
    Existed[I] := FileExists(Targets[I]);
    Applied[I] := False;
    if Remove then Changed := Changed or Existed[I]
    else
    begin
      ExtractTemporaryFile(Sources[I]);
      Sources[I] := ExpandConstant('{tmp}\') + Sources[I];
      if not Existed[I] then Changed := True
      else if GetSHA256OfFile(Targets[I]) <> GetSHA256OfFile(Sources[I]) then Changed := True;
    end;
  end;
  if not Changed then exit;
  if not FileExists(AddBackslash(Root) + 'bin\64bit\obs64.exe') or
      not DirExists(AddBackslash(Root) + 'obs-plugins\64bit') then
    RaiseException('The selected directory is not an OBS Studio installation.');
  RequireObsClosed();

  for I := 0 to 2 do
    if Existed[I] then
    begin
      FileHandle := WinCreateFile(Targets[I], $80000000, 0, 0, 3, $80, 0);
      if FileHandle = -1 then FileOperationFailed('Close OBS before changing its plugin');
      WinCloseHandle(FileHandle);
    end;

  { An exclusive, delete-on-close lease serializes installers without leaving a lock file. }
  LeaseName := AddBackslash(Root) + '.panelive-companion-obs-plugin-install.lock';
  Log('Acquiring OBS update lease: ' + LeaseName);
  Lease := WinCreateFile(LeaseName, $C0000000, 0, 0, 4, $04000000, 0);
  if Lease = -1 then FileOperationFailed('Could not access the OBS installation');
  Transaction := '';
  PreserveRecovery := False;
  try
    Log('Checking for an unfinished OBS update.');
    if FindFirst(AddBackslash(Root) + '.panelive-companion-obs-plugin-update-*', Found) then
    begin
      FindClose(Found);
      RaiseException('An interrupted OBS plugin update needs recovery in ' + Root);
    end;
    for Attempt := 0 to 99 do
    begin
      Transaction := AddBackslash(Root) + '.panelive-companion-obs-plugin-update-' +
        GetDateTimeString('yyyymmddhhnnsszzz', '-', '-') + '-' + IntToStr(Attempt);
      if WinCreateDirectory(Transaction, 0) then break;
      if DLLGetLastError() <> 183 then FileOperationFailed('Could not stage the OBS plugin');
      Transaction := '';
    end;
    if Transaction = '' then RaiseException('Could not create an OBS plugin staging directory.');
    Log('Staging OBS update: ' + Transaction);
    try
      Recovery := 'OBS directory: ' + Root + #13#10;
      for I := 0 to 2 do
      begin
        Backups[I] := AddBackslash(Transaction) + IntToStr(I) + '.old';
        Staged[I] := AddBackslash(Transaction) + IntToStr(I) + '.new';
        Recovery := Recovery + IntToStr(I) + ': ' + Targets[I] + #13#10;
        if Existed[I] then
          if not WinCopyFile(Targets[I], Backups[I], True) then
            FileOperationFailed('Could not back up the OBS plugin');
        if not Remove then
          if not WinCopyFile(Sources[I], Staged[I], True) then
            FileOperationFailed('Could not stage the OBS plugin');
      end;
      if not SaveStringToFile(AddBackslash(Transaction) + 'recovery.txt', Utf8Encode(Recovery), False) then
        RaiseException('Could not record OBS plugin recovery paths.');
      for I := 0 to 2 do
      begin
        if Remove then
        begin
          if Existed[I] then
            if not WinMoveFile(Targets[I], Staged[I], 8) then
              FileOperationFailed('Could not remove the OBS plugin; close OBS and retry');
        end
        else
        begin
          EnsureDirectory(ExtractFileDir(Targets[I]));
          if not WinMoveFile(Staged[I], Targets[I], 9) then
            FileOperationFailed('Could not replace the OBS plugin; close OBS and retry');
        end;
        Applied[I] := True;
      end;
    except
      Failure := GetExceptionMessage();
      Denied := IntegrationNeedsElevation;
      RollbackFailed := False;
      for I := 2 downto 0 do
        if Applied[I] then
        begin
          if Existed[I] then
          begin
            if not WinMoveFile(Backups[I], Targets[I], 9) then RollbackFailed := True;
          end
          else if FileExists(Targets[I]) then
            if not DeleteFile(Targets[I]) then RollbackFailed := True;
        end;
      PreserveRecovery := RollbackFailed;
      IntegrationNeedsElevation := Denied and (not RollbackFailed);
      if RollbackFailed then
        RaiseException(Failure + '. Recovery files were preserved in ' + Transaction);
      RaiseException(Failure);
    end;
  finally
    if (Transaction <> '') and (not PreserveRecovery) and DirExists(Transaction) then
      if CompareText(ExtractFileDir(Transaction), Root) = 0 then
        DelTree(Transaction, True, True, True);
    WinCloseHandle(Lease);
  end;
  if Remove then
  begin
    { RemoveDir removes only empty directories, preserving files we do not own. }
    RemoveDir(AddBackslash(Root) + 'data\obs-plugins\' + PluginId + '\locale');
    RemoveDir(AddBackslash(Root) + 'data\obs-plugins\' + PluginId);
  end;
end;

procedure ChangeFirewall(const AppRoot, Action: String);
var
  Executable, Name: String;
  Policy, Rule: Variant;
  Rules: INetFwRules;
  NativeRule: INetFwRule;
  Status: HResult;
  Exists, Matches: Boolean;
begin
  if Action = 'keep' then exit;
  if (Action <> 'add') and (Action <> 'remove') then
    RaiseException('Unknown firewall operation.');
  Executable := AddBackslash(CheckedDirectory(AppRoot)) + 'bin\panelive-companion.exe';
  if (Action = 'add') and (not FileExists(Executable)) then
    RaiseException('The Companion executable is missing.');
  Name := CompanionId + '-' + Uppercase(Copy(GetSHA256OfString(Utf8Encode(Lowercase(Executable))), 1, 16));
  Policy := CreateOleObject('HNetCfg.FwPolicy2');
  Rules := INetFwRules(IDispatch(Policy.Rules));
  Status := Rules.Item(Name, NativeRule);
  Exists := Status <> -2147024894; { HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND). }
  if Exists then
  begin
    OleCheck(Status);
    Rule := IDispatch(NativeRule);
  end;
  if Exists then
    if CompareText(Rule.ApplicationName, Executable) <> 0 then
      RaiseException('An unrelated firewall rule uses this installation identifier.');
  if (Action = 'remove') and (not Exists) then exit;
  Matches := False;
  if Exists and (Action = 'add') then
    Matches := Boolean(Rule.Enabled) and (Rule.Direction = 1) and (Rule.Action = 1) and
      (Rule.Profiles = 2) and (Rule.Protocol = 256) and
      (CompareText(Rule.RemoteAddresses, 'LocalSubnet') = 0) and
      (Rule.LocalAddresses = '*') and (Rule.InterfaceTypes = 'All') and
      (not Boolean(Rule.EdgeTraversal)) and (Rule.ServiceName = '');
  if Matches then exit;
  if not IsAdmin then
  begin
    IntegrationNeedsElevation := True;
    RaiseException('Administrator approval is needed to change Windows Firewall.');
  end;
  if Action = 'remove' then OleCheck(Rules.Remove(Name))
  else
  begin
    Rule := CreateOleObject('HNetCfg.FWRule');
    Rule.Name := Name;
    Rule.Description := 'PaneLive Companion: local clients on private networks.';
    Rule.Grouping := 'PaneLive Companion';
    Rule.ApplicationName := Executable;
    Rule.Protocol := 256;
    Rule.Direction := 1;
    Rule.Action := 1;
    Rule.Profiles := 2;
    Rule.RemoteAddresses := 'LocalSubnet';
    Rule.LocalAddresses := '*';
    Rule.InterfaceTypes := 'All';
    Rule.EdgeTraversal := False;
    Rule.Enabled := True;
    NativeRule := INetFwRule(IDispatch(Rule));
    OleCheck(Rules.Add(NativeRule));
  end;
end;

procedure ExecuteIntegrationPlan();
begin
  IntegrationNeedsElevation := False;
  ChangeObsPlugin(ExpandConstant('{param:OBSINSTALL|}'), False);
  ChangeObsPlugin(ExpandConstant('{param:OBSREMOVE|}'), True);
  ChangeObsPlugin(ExpandConstant('{param:OBSREMOVE2|}'), True);
  ChangeFirewall(ExpandConstant('{param:APPROOT|}'), ExpandConstant('{param:FIREWALL|keep}'));
end;

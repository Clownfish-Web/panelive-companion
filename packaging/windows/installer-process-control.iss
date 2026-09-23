function OpenCompanionProcess(Access: Cardinal; Inherit: BOOL; ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function QueryCompanionPath(Process: THandle; Flags: Cardinal; Buffer: String;
  var Size: Cardinal): BOOL;
  external 'QueryFullProcessImageNameW@kernel32.dll stdcall';
function WaitForCompanion(Process: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function TerminateCompanion(Process: THandle; ExitCode: Cardinal): BOOL;
  external 'TerminateProcess@kernel32.dll stdcall';
function CloseCompanionHandle(Process: THandle): BOOL;
  external 'CloseHandle@kernel32.dll stdcall';
function FindCompanionTray(Parent, After: HWND; ClassName, Caption: String): HWND;
  external 'FindWindowExW@user32.dll stdcall';
function CompanionWindowProcess(Window: HWND; var ProcessId: Cardinal): Cardinal;
  external 'GetWindowThreadProcessId@user32.dll stdcall';

procedure StopInstalledCompanion(const AppDirectory: String);
var
  Locator, Services, Processes, Item: Variant;
  ExpectedPath, ActualPath: String;
  Index: Integer;
  ProcessId, WindowProcessId, Size, WaitResult, ErrorCode: Cardinal;
  Process: THandle;
  Tray: HWND;
begin
  ExpectedPath := AddBackslash(ExpandFileName(AppDirectory)) + 'bin\panelive-companion.exe';
  Locator := CreateOleObject('WbemScripting.SWbemLocator');
  Services := Locator.ConnectServer('', 'root\CIMV2');
  Processes := Services.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''panelive-companion.exe''');
  for Index := 0 to Integer(Processes.Count) - 1 do
  begin
    Item := Processes.ItemIndex(Index);
    ProcessId := Item.ProcessId;
    { Keep the handle throughout shutdown, preventing PID reuse from targeting another process. }
    Process := OpenCompanionProcess($00101001, False, ProcessId);
    if Process = 0 then
    begin
      ErrorCode := DLLGetLastError();
      if ErrorCode <> 87 then
        RaiseException('Could not stop the running Companion: ' + SysErrorMessage(ErrorCode));
    end
    else
    try
      Size := 32768;
      ActualPath := StringOfChar(#0, Size);
      if not QueryCompanionPath(Process, 0, ActualPath, Size) then
        RaiseException('Could not identify the running Companion installation.');
      SetLength(ActualPath, Size);
      if CompareText(ActualPath, ExpectedPath) = 0 then
      begin
        Log('Stopping installed Companion, PID ' + IntToStr(ProcessId));
        Tray := FindCompanionTray(-3, 0, 'pl.clownfishweb.panelive.CompanionTray', 'PaneLive Companion');
        if Tray <> 0 then
        begin
          CompanionWindowProcess(Tray, WindowProcessId);
          if WindowProcessId = ProcessId then
            PostMessage(Tray, $0111, 1005, 0); { Native Shutdown Companion menu command. }
        end;
        WaitResult := WaitForCompanion(Process, 5000);
        if WaitResult = 258 then
        begin
          Log('Companion did not exit promptly; terminating it before changing files.');
          if not TerminateCompanion(Process, 0) then
            RaiseException('Could not terminate Companion: ' + SysErrorMessage(DLLGetLastError()));
          WaitResult := WaitForCompanion(Process, 10000);
        end;
        if WaitResult <> 0 then
          RaiseException('Companion has not stopped. No installation files have been changed.');
        Log('Companion stopped; its executable and codec libraries are released.');
      end;
    finally
      CloseCompanionHandle(Process);
    end;
  end;
end;

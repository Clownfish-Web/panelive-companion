@echo off
setlocal
cd /d "%~dp0bin"
panelive-companion.exe ^
  --name "PaneLive Companion" ^
  --gateway-port 8443
echo.
echo Companion exited with code %ERRORLEVEL%.
pause

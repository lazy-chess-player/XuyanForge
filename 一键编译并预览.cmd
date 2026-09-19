@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-and-preview.ps1"
if errorlevel 1 (
  echo.
  echo Build failed. See the error above.
  pause
  exit /b 1
)

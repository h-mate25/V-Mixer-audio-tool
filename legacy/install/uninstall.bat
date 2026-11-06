@echo off
setlocal

net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo Requesting Administrator rights...
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

set "DLL=%~dp0..\build\x64\Release\WindowsReacASIO.dll"
if not exist "%DLL%" (
  echo DLL not found: "%DLL%"
  echo If the file was moved, run: regsvr32 /u path\to\WindowsReacASIO.dll
  exit /b 1
)

echo Unregistering "%DLL%"
regsvr32 /u "%DLL%"

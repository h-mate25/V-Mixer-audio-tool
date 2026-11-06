@echo off
setlocal

net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo Requesting Administrator rights...
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

set "DLL="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$c=@('%~dp0..\build-cmake-vs18\Release\WindowsReacASIO.dll','%~dp0..\build-cmake-vs18-txfix\Release\WindowsReacASIO.dll','%~dp0..\build-cmake\Release\WindowsReacASIO.dll','%~dp0..\build\x64\Release\WindowsReacASIO.dll') | Where-Object { Test-Path $_ } | Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending | Select-Object -First 1; if ($c) { [Console]::Write($c) }"`) do set "DLL=%%I"
if "%DLL%"=="" (
  echo DLL not found: "%DLL%"
  echo Build Release x64 first in CMake or Visual Studio.
  exit /b 1
)

set "CONFIGDIR=%ProgramData%\WindowsReacASIO"
set "CONFIGFILE=%CONFIGDIR%\config.ini"
if not exist "%CONFIGDIR%" mkdir "%CONFIGDIR%"
icacls "%CONFIGDIR%" /grant "*S-1-5-32-545:(OI)(CI)M" >nul

if not exist "%CONFIGFILE%" (
  echo Creating default "%CONFIGFILE%"
  >"%CONFIGFILE%" echo [Audio]
  >>"%CONFIGFILE%" echo SampleRate=48000
  >>"%CONFIGFILE%" echo ChannelCount=40
  >>"%CONFIGFILE%" echo AsioBufferSize=512
  >>"%CONFIGFILE%" echo JitterBufferSamples=8192
  >>"%CONFIGFILE%" echo.
  >>"%CONFIGFILE%" echo [Network]
  >>"%CONFIGFILE%" echo Interface=
  >>"%CONFIGFILE%" echo Port=5004
  >>"%CONFIGFILE%" echo.
  >>"%CONFIGFILE%" echo [Diagnostics]
  >>"%CONFIGFILE%" echo EnableDebugLog=1
  >>"%CONFIGFILE%" echo.
  >>"%CONFIGFILE%" echo [Monitor]
  >>"%CONFIGFILE%" echo EnableWindowsOutput=1
  >>"%CONFIGFILE%" echo BufferFrames=8192
  >>"%CONFIGFILE%" echo.
  >>"%CONFIGFILE%" echo [Receiver]
  >>"%CONFIGFILE%" echo ReadOnly=0
  >>"%CONFIGFILE%" echo.
  >>"%CONFIGFILE%" echo [Transmit]
  >>"%CONFIGFILE%" echo EnableReacOutput=0
  >>"%CONFIGFILE%" echo ExposeMonoReturns=0
  >>"%CONFIGFILE%" echo OutputChannels=16
  >>"%CONFIGFILE%" echo BufferFrames=4096
)

echo Registering "%DLL%"
regsvr32 "%DLL%"

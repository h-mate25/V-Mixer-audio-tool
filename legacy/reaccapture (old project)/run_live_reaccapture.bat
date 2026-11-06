@echo off
SETLOCAL

REM --- Configuration ---
REM These values are now passed in from reaccapture.exe
SET CAPTURE_DURATION=%1
SET IFACE_GUID=%~2

REM Find dumpcap.exe. First, check the default path, then fall back to the system PATH.
SET DUMPCAP_CMD="dumpcap.exe"
IF EXIST "C:\Program Files\Wireshark\dumpcap.exe" (
    SET DUMPCAP_CMD="C:\Program Files\Wireshark\dumpcap.exe"
)

REM Capture filter for REAC.
SET CAP_FILTER="ether proto 0x8819"

REM Temporary pcap file name. This MUST match the filename in reaccapture.cc.
SET TEMP_PCAP_FILE="live_capture_temp.pcap"
REM --- End Configuration ---

echo.
echo Launching live capture for %CAPTURE_DURATION% seconds...
echo Saving to %TEMP_PCAP_FILE%...
echo.

%DUMPCAP_CMD% -i %IFACE_GUID% -a duration:%CAPTURE_DURATION% -f %CAP_FILTER% -w %TEMP_PCAP_FILE% -F pcap

IF ERRORLEVEL 1 (
    echo.
    echo ERROR: dumpcap.exe failed.
    echo Please ensure Wireshark/Npcap is installed and the interface is correct.
    GOTO :EOF
)

echo.
echo Capture finished.
echo.

ENDLOCAL 
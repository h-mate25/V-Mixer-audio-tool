@echo off
echo Starting REAC capture (legacy pcap format)...
echo Make sure your Roland REAC device is connected and active.
echo Capturing to .\live_reac_capture.pcap
echo Press Ctrl+C in this window to stop capture.

REM Ensure Wireshark directory is correct
SET DUMPCAP_PATH="C:\Program Files\Wireshark\dumpcap.exe"

REM Interface GUID for your Ethernet adapter
SET IFACE_GUID=\Device\NPF_{YOUR-ADAPTER-GUID}

REM Capture filter for REAC
SET CAP_FILTER="ether proto 0x8819"

REM Output file (changed to .pcap)
SET OUT_FILE=".\live_reac_capture.pcap"

REM Added -P flag for pcap format
%DUMPCAP_PATH% -P -i %IFACE_GUID% -f %CAP_FILTER% -w %OUT_FILE%

echo Capture stopped. File saved to %OUT_FILE%
pause 
# REACapture for Windows

This tool captures Roland's REAC (Roland Ethernet Audio Communication) protocol data from a live Ethernet interface and converts it into standard multi-channel `.wav` files.

This version has been specifically tailored for Windows and uses an interactive command-line interface. It relies on Wireshark's `dumpcap.exe` utility for the underlying packet capture, providing a robust and reliable method for acquiring the REAC data.

## Features

- **Live Capture:** Captures REAC audio directly from a network interface.
- **Interactive Menu:** A user-friendly menu to control recordings without command-line arguments.
- **Configurable:** Easily change the save location, recording duration, and specific channels to record.
- **Persistent Settings:** Your settings for path, duration, and channels are saved in `reaccapture_config.txt` for convenience.
- **Organized Output:** Recorded `.wav` files are automatically saved into a new, timestamped folder for each session.
- **WAV Export:** Converts the proprietary REAC data into standard 24-bit PCM WAV files, one for each channel.

## Prerequisites

Before running the application, you **must** have **Wireshark** installed on your Windows system. The application specifically requires the `dumpcap.exe` command-line tool and the `Npcap` packet capture driver that are included with Wireshark.

- **Download Wireshark:** [https://www.wireshark.org/download.html](https://www.wireshark.org/download.html)
  - During installation, make sure to install `Npcap`.

## Configuration

All configuration is now handled inside the application. There is **no need to manually edit any files** before running the program.

## How to Run

1.  **Run as Administrator:** Right-click on `reaccapture.exe` and select "Run as administrator". This is required for the program to list network interfaces and perform the capture.
2.  **First-Time Setup:** The first time you run the program, it will guide you through a setup process:
    - First, it will list all available network interfaces on your computer. You must copy and paste the GUID for the interface connected to your REAC device (e.g., `\Device\NPF_{...}`).
    - Then, it will ask you to set a default save path, recording duration, and channel selection.
    
    These settings are saved in `reaccapture_config.txt` so you don't have to enter them again.

3.  **Main Menu:** After setup, you will see the main menu.
    ```
    --- REAC Live Capture ---
      1. Save Path:    C:\REAC_Recordings
      2. Duration (s): 10
      3. Channels:     1-10
      4. Interface:    \Device\NPF_{YOUR-GUID-HERE}

    Options: (r)ecord | (p)ath | (d)uration | (c)hannels | (i)nterface | (q)uit
    Enter your choice:
    ```
    - `r`: Starts a new recording using the current settings.
    - `p`, `d`, `c`: Change the save path, duration, or channel selection.
    - `i`: Lists the network interfaces again and lets you pick a new one. This is useful when moving the program between different computers.
    - `q`: Exits the program.

## Output

After a recording is finished, the program will create a new directory in your specified save path. The directory will be named with the current timestamp (e.g., `2023-10-27_14-30-00`).

Inside this directory, you will find the individual `.wav` files for each channel you chose to record (e.g., `output-01.wav`, `output-02.wav`, etc.).

## Included Files

- `reaccapture.exe`: The main executable.
- `run_live_reaccapture.bat`: The batch script that `reaccapture.exe` calls to perform the live capture. This is the file you must configure.
- `reaccapture_config.txt`: Stores your settings.
- `lib*.dll`: DLL files required by the executable. They must be in the same directory as `reaccapture.exe`.
- `README.md`: This file.

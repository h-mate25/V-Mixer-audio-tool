# Windows REAC ASIO

Milestone 1-4 implementation: a 64-bit Windows ASIO DLL that registers as
`Windows REAC ASIO`, reports 40 input channels, and feeds host input buffers
from an internal audio engine. With no configured REAC interface it runs a test
sine producer. When `[Network] Interface=` is set, a separate Npcap capture
thread receives REAC/H-8819 raw Ethernet frames and writes decoded PCM into the
audio engine ring buffer.

## Build

1. Open `WindowsReacASIO.sln` in Visual Studio.
2. Select `Release | x64`.
3. Build the `WindowsReacASIO` project.
4. Run `install\install.bat` as Administrator.

The installer also prepares `C:\ProgramData\WindowsReacASIO\config.ini` so the
ASIO control panel can save settings from a normal DAW process.

The expected DLL path is:

```text
build\x64\Release\WindowsReacASIO.dll
```

The CMake build also creates:

```text
build-cmake\Release\reac_probe.exe
build-cmake\Release\reac_tx_probe.exe
build-cmake\Release\reac_packet_test.exe
```

## ASIO behavior

- Driver name: `Windows REAC ASIO`
- Inputs: configurable visible input count up to 40 channels, named
  `REAC Input 01` ... `REAC Input 40`
- Outputs: configurable `REAC Return` channels plus 2 `Monitor Out` channels.
  The monitor pair is forwarded to the default Windows playback device through
  a separate WASAPI thread when monitoring is enabled. REAC network transmit is
  scaffolded but still disabled.
- Sample rates: 44100, 48000, 96000 Hz
- Sample type: `ASIOSTFloat32LSB`
- Preferred buffer: 512 samples
- Test producer: sine tones written into the internal ring buffer when no REAC
  interface is configured
- Control panel: adapter selection, sample-rate/buffer defaults, jitter buffer
  size, debug output toggle, and live counter snapshot

## Architecture

```text
ASIO host callback thread
  -> WindowsReacAsioDriver
  -> AudioEngine
  -> RingBuffer

REAC receiver thread
  -> ReacPacketParser
  -> AudioEngine producer side
```

The ASIO callback does not perform socket reads, file I/O, blocking waits, or
memory allocation. Overflow policy in the current ring buffer is to discard the
new producer block, keeping the consumer path single-owner and wait-free.

## REAC Probe

Install Npcap first. If normal user access is desired, install it without
restricting driver access to Administrators only.

List capture devices:

```powershell
build-cmake\Release\reac_probe.exe --list-devices
```

On Windows the list shows the friendly adapter alias when Npcap exposes a
`\Device\NPF_{...}` name, following the same approach as the OBS h8819 plugin.

Capture a short stream and write separate mono WAV files:

```powershell
build-cmake\Release\reac_probe.exe --device Realtek --seconds 10 --wav reac_40ch.wav
```

`--device` accepts a one-based device index, full Npcap device name, or a unique
fragment from the adapter description.

`--wav` writes separate mono 24-bit PCM files for DAW-friendly inspection:

```text
reac_40ch-01.wav ... reac_40ch-40.wav
```

The probe reports the decoded channel count range from the packet length using
the normal 12-sample REAC frame assumption. A 340-byte packet carries 288 bytes
of audio payload, so it can also describe a shorter 16-channel block
(`16 channels x 6 samples x 3 bytes`). That ambiguity matters for the S-1608
return path; captures and hardware tests should be interpreted with the packet
rate as well as the packet size.
Use `--inspect` to print Ethernet source/destination MACs, packet type,
counter, decoded channel count, and the 32-byte REAC header for the first
packets:

```powershell
build-cmake\Release\reac_probe.exe --device 4 --seconds 10 --inspect
```

Observed `0xEACD` and `0xEACF` metadata/control packet types are now decoded as
PCM when their payload layout is valid; the packet type is still printed for
inspection.

## REAC TX Probe

`reac_tx_probe.exe` is a separate hardware test tool for cautious raw Ethernet
transmit experiments. It is not used by the ASIO driver yet.

The original probe default was tailored for early Roland S-1608 experiments: 8
return channels, 340-byte REAC frames, silence, and dry-run mode. The ASIO
return stream now follows the later S-1608 slave-return measurements more
closely: 16 wire channels, 6 samples per packet, 340-byte frames, zero audio
header, and a packet cadence derived from the selected sample rate
(`sampleRate / 6`, for example 8000 packets/second at 48 kHz). The S-1608 has
16 analog inputs and 8 analog outputs; its REAC modes are Master, Slave, and
Split. To test PC-as-Master output into the S-1608, put the S-1608 in Slave mode
and power-cycle it after changing the mode switch.

Available profiles:

```text
s1608        8ch PC-as-Master test using the PC adapter MAC
s1608-slave  8ch S-1608-like Slave stream, source 02:00:00:00:00:01
mixer8       8ch mixer-like Master phase, source 02:00:00:00:00:02, header 00e7
mixer40      40ch mixer-like stream shape
```

Dry-run only, no packets sent:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4
```

First live test, silence only:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --send --seconds 3
```

Very low-level tone on S-1608 output channel 1:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --send --seconds 3 --tone-channel 1 --level-db -48
```

PC disguised as an S-1608 Slave toward the mixer:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --send --seconds 10 --tone-channel 1 --level-db -48
```

PC disguised as an S-1608 Slave, with its packet counter aligned to the live
mixer stream before transmit:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --sync-counter --sync-source-mac 02:00:00:00:00:02 --send --seconds 10 --tone-channel 1 --level-db -48
```

The timed queue can be paced slightly faster without changing the logical REAC
packet count. This is useful when capture shows the PC stream drifting behind
the mixer clock:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
```

Experimental PC-as-Slave control handshake before the audio stream. This listens
for a mixer `0xEACD` control trigger, sends an S-1608-like slave announce
sequence to the mixer MAC, then continues with the synced audio queue. When
`--dest-mac` is not supplied, the audio stream is automatically sent unicast to
the mixer MAC observed during the handshake. When `--header-repeat` is not
supplied, the audio stream also adopts the post-announce filler header pair from
the handshake, matching the observed behavior in the open-source REAC slave
state machine:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --slave-handshake --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
```

The current slave experiment can also insert the same announce sequence into
the timed audio queue periodically, replacing five audio packets each time
while keeping the REAC counter continuous:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --slave-handshake --slave-keepalive-ms 4000 --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
```

The more S-1608-like experiment sends a short standalone broadcast preroll,
then answers later mixer EACD trigger packets while the queued audio is running:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --slave-preroll-ms 1500 --slave-handshake --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --sync-lead 72 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
```

To start before the mixer is connected, use standby mode. It keeps sending
S-1608-like broadcast packets until a mixer trigger is observed, then performs
the handshake and starts the queued audio. The handshake capture is filtered to
the mixer/source MAC when `--sync-source-mac` is supplied, so the PC's own
standby packets do not delay the trigger response. The timed audio queue is
also prepared before standby, and `--sync-counter` can reuse the handshake
trigger counter to keep the post-announce gap short:

```powershell
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --slave-standby --slave-standby-responder --standby-chunk-ms 10 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --queue-pps 4006 --send --seconds 10 --tone-channel 1 --level-db -48
```

Longer stability run with chunked audio and queued trigger replies. This avoids
out-of-band trigger replies during the timed queue; control replies replace the
next local-counter packet slot instead:

```powershell
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --slave-standby --standby-chunk-ms 5 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --queue-pps 4006 --stream-chunk-ms 250 --send --seconds 60 --force --tone-channel 1 --level-db -48
```

Master-clock-following stability run. This still sends timed chunks, but it
measures the live master counter rate and compensates chunk overhead so the PC
source clock does not slowly drift away:

```powershell
build-cmake\Release\reac_tx_probe.exe --device Ethernet --profile s1608-slave --slave-standby --standby-chunk-ms 5 --slave-handshake --slave-per-gron-state --slave-local-counter --handshake-continuous-counter --slave-trigger-responder --sync-counter --sync-source-mac 02:00:00:00:00:02 --queue-pps 4006 --stream-chunk-ms 250 --clock-follow --send --seconds 60 --force --tone-channel 1 --level-db -48
```

The current chosen stabilization target is the S-1608 slave-return path with
16 wire channels in 340-byte packets. Use the preset below instead of retyping
the whole experimental option set:

```powershell
build-cmake\Release\reac_tx_probe.exe --device Ethernet --stable-slave --send --seconds 60 --force --tone-channel 1 --level-db -48
```

The preset expands to the S-1608 slave handshake, local source counter, queued
trigger replies, chunked timed queues, and master clock following. For first
hardware checks, prefer 48 kHz because the resulting 8000 packet/second cadence
matches the strongest S-1608 reference. 44.1 kHz remains supported and should
produce about 7350 packets/second.

PC disguised as an S-1608 Slave, clocked directly from the live mixer stream:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile s1608-slave --clocked --sync-source-mac 02:00:00:00:00:02 --send --seconds 10 --tone-channel 1 --level-db -48
```

PC disguised as the observed mixer 8-channel Master phase toward the S-1608:

```powershell
build-cmake\Release\reac_tx_probe.exe --device 4 --profile mixer8 --send --seconds 3
```

Safety defaults:

- no packets are transmitted unless `--send` is present,
- no tone is generated unless `--tone-channel N` is present,
- tone levels above `-18 dBFS` are refused,
- sending longer than 10 seconds requires `--force`,
- `--sync-counter` listens for a live REAC packet first and patches the queued
  transmit counters just before sending,
- `--sample-rate` sets the logical audio rate for generated tones and derives
  REAC packet rate as `sampleRate / 12` (`44100` -> `3675` pps, `48000` ->
  `4000` pps),
- `--stable-slave` selects the legacy probe stabilization preset: 44.1 kHz
  S-1608-like 8x12 slave mode with standby, Per-Gron state gate, local source
  counter, queued trigger responses, 250 ms chunks, and master clock following.
  The ASIO driver return path has since moved to the 16x6 S-1608 model,
- `--queue-pps` adjusts timed-queue pacing while keeping the logical REAC
  packet count at `--pps`,
- `--slave-handshake` sends an experimental EACD slave-announce sequence before
  the audio stream, auto-targets the observed mixer MAC unless `--dest-mac` is
  supplied, and auto-fills the post-announce REAC header unless
  `--header-repeat` is supplied,
- `--slave-keepalive-ms` inserts the slave announce sequence periodically into
  the queued stream and requires `--slave-handshake`,
- `--slave-preroll-ms` sends a short S-1608-like standalone broadcast stream
  before the handshake and requires `--slave-handshake`,
- `--slave-standby` keeps sending S-1608-like standalone broadcast chunks until
  a mixer trigger appears, then continues with the normal handshake; when a
  sync source MAC is supplied, handshake/trigger capture is filtered to that
  source to avoid lag from locally transmitted standby packets,
- `--slave-standby-responder` starts a separate capture/sender thread while
  standby broadcast is running, so the first mixer trigger can be answered
  immediately instead of after the current standby send block finishes,
- captured CDEA control packets are checksum-validated before they can advance
  the slave handshake state,
- `--slave-per-gron-state` follows the Per-Gron slave state gate by requiring
  master identity/control information before answering a trigger packet,
- `--slave-local-counter` keeps the PC/S-1608 source counter continuous across
  standby, announce, queued audio, and trigger responses instead of jumping to
  the mixer's counter; after a handshake it starts the stream at the next local
  counter slot even if a few milliseconds elapsed while the stream setup ran,
- `--stream-chunk-ms` sends long timed streams in bounded chunks; when combined
  with `--slave-trigger-responder`, later mixer triggers are queued and inserted
  into the next chunk instead of being transmitted as extra out-of-band packets,
- `--clock-follow` is the preferred long-run experiment: it measures the live
  master counter rate while chunked streaming runs, rejects short capture gaps,
  adjusts chunk size/send rate, and uses the estimated master sample rate for
  tone generation and duration,
- `--handshake-continuous-counter` is an experimental slave-mode option for
  `--sync-counter`: after the mixer trigger, it starts queued audio near the
  sent announce sequence instead of adding the older `--sync-lead` offset,
- `--slave-trigger-responder` watches for later mixer EACD trigger packets and
  answers them from a separate sender while the queued audio is running,
- `--clocked` follows the live REAC packet clock and sends one packet per
  matching clock-source packet instead of using the Npcap timed queue,
- source MAC defaults to the selected adapter's physical MAC when available,
- destination MAC defaults to broadcast, except after `--slave-handshake` where
  the observed mixer MAC is used for the following audio stream.

Keep connected speakers/headphones turned down for the first tests. The goal of
the first send is only to see whether the S-1608 REAC indicator/outputs react to
the PC-generated stream.

## Configuration

Optional config file:

```text
C:\ProgramData\WindowsReacASIO\config.ini
```

Example:

```ini
[Audio]
SampleRate=48000
ChannelCount=40
AsioBufferSize=512
JitterBufferSamples=8192

[Network]
Interface=
Port=5004

[Diagnostics]
EnableDebugLog=1

[Monitor]
EnableWindowsOutput=1
BufferFrames=8192

[Receiver]
ReadOnly=0

[Transmit]
EnableReacOutput=0
ExposeMonoReturns=0
OutputChannels=16
BufferFrames=8192
```

Leave `Interface=` empty for the built-in test sine source. Set it to the REAC
adapter selector used by `reac_probe` to enable live capture in the ASIO driver.
`Port` is reserved for future modes; current REAC capture is raw Ethernet.

The ASIO control panel writes this file from the host application. Reopen the
driver or restart the DAW after saving so the new capture/interface defaults are
picked up cleanly.

`ChannelCount` controls how many REAC inputs the ASIO host sees. The internal
engine remains 40-channel capable, so 8, 24, and 40-channel REAC packet layouts
can still be decoded and padded safely.

`Receiver.ReadOnly=1` puts the driver in capture-only REAC mode. It starts the
REAC receiver and exposes per-channel ASIO inputs, but disables REAC return
transmit and hides the `REAC Return` output bank so the host cannot disturb the
network while recording. `Monitor Out 01/02` remain available for local Windows
monitoring.

When `EnableWindowsOutput=1`, `Monitor Out 01/02` appear as ASIO outputs. Route
tracks or the DAW master to those outputs to hear them through the Windows
default speakers/headphones. For best results, set the Windows playback device
format to 48 kHz.

The control panel includes a `Restart driver` button. It restarts the REAC
receiver and Windows monitor path after saving settings. Visible channel-count
and ASIO buffer-size changes may still require reopening the ASIO device in the
host, because many DAWs query those values only during driver initialization.

`OutputChannels` controls how many `REAC Return` ASIO outputs are exposed. The
S-1608 slave return path is now driven as a 16-channel, 6-sample wire stream:
340-byte REAC audio frames at `sampleRate / 6` packets/second. `OutputChannels=8`
still works by sending silence on wire channels 9-16; `OutputChannels=16` maps
DAW returns 1-16 directly into the 16 wire channels. The older 8-channel,
12-sample interpretation remains useful for receive-side inspection, but it is
no longer the ASIO return-transmit model.
With `OutputChannels=16` and `ExposeMonoReturns=0`, the DAW sees:

```text
REAC Return 01-16
Monitor Out 01/02
```

With `ExposeMonoReturns=1`, an additional mono-friendly bank is exposed for
DAWs that only show stereo output pairs:

```text
REAC Mono 01 / REAC Mono 01 Dummy
...
REAC Mono 16 / REAC Mono 16 Dummy
```

Route to the pair and use the left side; the dummy side is ignored.
`EnableReacOutput` enables the experimental raw Ethernet REAC return stream.
The return-side packet encoder is covered by the synthetic packet test and is
used by the driver when REAC output is enabled.

The live receiver follows the OBS h8819 startup policy and ignores the first
1024 valid REAC packets before feeding the ASIO ring buffer. Missing packets are
detected from the REAC counter; short gaps are filled with silence to preserve
stream timing, while long gaps rely on the ASIO underrun silence path. Valid
REAC payloads are decoded as audio regardless of packet type; the packet type is
kept as metadata so control/filler frame types do not create artificial silence
inside an otherwise continuous stream.

## Acceptance check

After registration, open FL Studio or another ASIO host:

```text
Preferences -> Audio -> ASIO -> Windows REAC ASIO
```

The host should show 40 input channels. Arm an input track and select one of
the `REAC Input` channels to observe the test sine signal.

For live REAC, first verify the adapter with `reac_probe`, then set the same
selector in `C:\ProgramData\WindowsReacASIO\config.ini`, restart the DAW, and
select the ASIO driver again.

## Next milestones

- Add channel map configuration to the control panel.
- Validate live hardware capture with `reac_probe`.
- Validate live REAC input through Reaper/FL Studio.
- Add longer-duration RF64/WAVE64 capture for very long 40-channel recordings.
- Extend the control panel with channel mapping and richer receiver diagnostics.

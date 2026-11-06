# REAC Protocol Notes

These notes summarize the parts needed for the current receiver prototype.
They are based on cross-checking these public projects:

- https://github.com/ManuKapel/REAC-driver-Windows-Mac
- https://github.com/per-gron/reacdriver
- https://github.com/norihiro/obs-h8819-source
- Roland S-1608/S-0816 Owner's Manual:
  https://cdn.roland.com/assets/media/pdf/s1608_manual_e1.pdf

## Audio Stream

- Transport is raw Ethernet, not UDP/IP.
- EtherType is `0x8819`.
- Windows capture path uses Npcap/wpcap with BPF filter `ether proto 0x8819`.
- The originally targeted full 40-channel audio frame has:
  - 14-byte Ethernet header
  - 36-byte REAC stream header
  - 1440-byte audio payload
  - 2-byte trailer `C2 EA`
- Audio payload offset is byte `50` from the beginning of the Ethernet frame.
- Packet counter is little-endian at bytes `14..15`.
- Audio stream is 48 kHz.
- Observed hardware streams use about 4000 packets per second.
- Each packet carries 12 samples per channel.
- The receiver also accepts shorter even-channel audio payloads and pads the
  missing channels with silence in the 40-channel engine buffer. A 340-byte
  captured frame corresponds to 8 active channels:
  - 14-byte Ethernet header
  - 36-byte REAC stream header
  - 288-byte audio payload
  - 2-byte trailer `C2 EA`
- Baseline audio packets seen so far use packet type `0x0000` at bytes `16..17`.
- The 2026-05-21 mixer + digital snake capture also contains occasional
  `0x8819` packets with packet types `0xEACD` and `0xEACF`. They use the same
  counter sequence and frame size. FreeREAC's published direction suggests that
  valid payload layouts should still be treated as PCM-bearing frames; packet
  type is retained as metadata until the exact control semantics are known.

## Current Capture Matrix

The captures below were taken with an `ether proto 0x8819` filter unless noted
otherwise. A normal switch/PC port can miss unicast traffic between two REAC
devices, so "visible flow" means traffic seen by the PC capture point.

Repeated S-1608-only captures in all three device modes show the same visible
stream shape:

| Capture | Mode | Visible source | Frame size | Audio channels | Packet types | Header |
| --- | --- | --- | --- | --- | --- | --- |
| `<historical-capture>.pcapng` | Slave | `02:00:00:00:00:01` | 340 | 8 | `0x0000` only | all zeroes |
| `<historical-capture>.pcapng` | Split | `02:00:00:00:00:01` | 340 | 8 | `0x0000` only | all zeroes |
| `<historical-capture>.pcapng` | Master | `02:00:00:00:00:01` | 340 | 8 | `0x0000` only | all zeroes |

All three run at about 4000 packets/second and have continuous counters. This
confirms that an isolated S-1608 free-runs a clean 8-channel broadcast stream,
regardless of the front-panel REAC mode.

The mixer-side captures show one visible broadcast source:
`02:00:00:00:00:02 -> ff:ff:ff:ff:ff:ff`. They begin as 8-channel 340-byte
streams and later transition to 40-channel 1492-byte streams after a large
counter/time discontinuity, which is likely link/role renegotiation.

| Capture | Scenario | 340-byte packets | 1492-byte packets | Non-audio packets |
| --- | --- | ---: | ---: | ---: |
| `<historical-capture>.pcapng` | Mixer only | 14949 | 13566 | 414 |
| `<historical-capture>.pcapng` | Mixer + S-1608 Slave | 24051 | 4337 | 753 |
| `<historical-capture>.pcapng` | Mixer + S-1608 Master | 20405 | 7893 | 754 |
| `<historical-capture>.pcapng` | Mixer + S-1608 Split | 17392 | 10931 | 454 |

Observed non-audio packet types:

- `0xEACD`: repeated control/metadata packets.
- `0xEACF`: device/capability-like announcement packets.

The `0xEACF` header appears to carry a device identity/capability
advertisement. In the 8-channel phase it contains source MAC
`02:00:00:00:00:02` followed by `08 08`; in the 40-channel phase it contains
the same MAC followed by `28 10`. This is an inference from observed bytes, not
a confirmed field definition yet.

These captures are enough to mirror receive stream shapes, but not enough to
prove the return-audio destination or whether return traffic uses broadcast,
unicast, or a different setup/control exchange.

## S-1608 Transmit Probe Policy

Roland documents the S-1608 as a 16-input / 8-output stage unit. Its REAC mode
switch has Master, Slave, and Split settings:

- Master can send audio to Slave/Split devices and receive audio from a Slave.
- Slave can receive audio from a Master and send audio to a Master.
- Split can receive audio from a Master and cannot send audio.

For early PC-as-Master transmit tests, the safest hardware-aligned profile was
an 8-channel return stream for the S-1608's analog outputs:

- packet type `0x0000`,
- 8 channels,
- 340-byte frames,
- 4000 packets/second in the first 48 kHz experiments,
- silence by default,
- broadcast destination,
- selected adapter's physical MAC as Ethernet source when available.

Later S-1608 slave-return captures showed that this was only an early test
shape. It did not explain the bad return-audio quality once the mixer began to
accept the PC stream.

## S-1608 ASIO Return Finding

The `<historical-capture>.pcapng` hardware test showed that the mixer can notice
audio when 16 DAW returns are exposed, but the quality was not correct while the
wire packetizer still assumed the old 8-channel interpretation. The later
`meres47` and `meres48` measurements clarified the core ambiguity:

- a 340-byte REAC packet has 288 audio payload bytes,
- `8 channels x 12 samples x 3 bytes = 288`,
- `16 channels x 6 samples x 3 bytes = 288`.

The ASIO return transmitter now treats the S-1608 slave-return path as the
second layout: 16 wire channels, 6 samples per packet, zero audio header, and
340-byte frames. The target packet rate is derived from the selected sample
rate:

- 44.1 kHz -> 7350 packets/second,
- 48 kHz -> 8000 packets/second,
- 96 kHz -> 16000 packets/second.

This makes `OutputChannels=16` a real 16-channel return stream instead of an
8-channel wire stream with host-side routing exposed above it. If only 8 returns
are exposed, wire channels 9-16 are sent as silence.

The clock follower must not accept broad estimates from the mixed master
control/audio stream. For the S-1608-shaped slave return, the valid packet rate
must stay close to `sampleRate / 6`; otherwise the transmitter falls back to the
nominal cadence. This avoids locking the return stream to unrelated 40-channel
mixer traffic.

40-channel `mixer40` profile remains available for comparison with observed
mixer streams, but it is not the first S-1608 output test path.

## External Reference Check

The 2026-06-04 external review supports the direction, but does not replace the
local captures:

- Roland's S-1608 page describes the unit as 16 in / 8 out with 24-bit / 96 kHz
  REAC audio quality and very low latency. That supports treating it as a
  professional fixed-shape stream instead of a loose "best effort" Ethernet
  sender.
- Per-Gron's open-source `reacdriver` uses a fixed REAC packet cadence model
  (`8000` packets/second and `12` samples/packet). That confirms that REAC code
  tends to be packet-cadence driven, but the S-1608 340-byte return captures
  require resolving the 8x12 vs 16x6 payload ambiguity locally.
- Npcap documents synchronized send queues as timestamp-respecting kernel-side
  transmissions with far fewer context switches than repeated `pcap_sendpacket`
  calls. That supports the ASIO transmitter using short timed queues instead of
  a user-mode per-packet sleep loop.

References:

- https://proav.roland.com/global/products/s-1608/
- https://github.com/per-gron/reacdriver
- https://npcap.com/guide/npcap-tutorial.html

## `meres49` ASIO 48 kHz Finding

Capture: `<capture-directory>`.

The ASIO driver did switch to the intended 16x6 S-1608 return shape:

- PC standby: `02:00:00:00:00:01 -> ff:ff:ff:ff:ff:ff`, 116640 packets,
  340 bytes, continuous counter, all-silence audio payload.
- PC return stream: `02:00:00:00:00:01 -> 02:00:00:00:00:02`, 311045 packets,
  all 340 bytes, continuous counter, no missing packets, 310950 audio packets
  and 95 EACD announce/control packets.
- PC return audio is nonzero on channels 1/2 in the 16-channel x 6-sample
  interpretation, with peaks around 0.13 FS. That argues against a simple
  6/8-bit sample packing failure in the encoder.

Two timing failures remain visible:

- The first post-handshake audio packet was delayed by about 86 ms after the
  five EACD announce packets. This matched the previous 4096-frame TX startup
  prefill at 48 kHz, so the prefill must not be an idle network gap.
- The PC stream had no counter drops, but it did contain several 6-19 ms
  time gaps between consecutive counters. This points to queue preparation
  missing some chunk boundaries, not to REAC packet syntax.

Implemented response:

- TX startup prefill is no longer an idle wait; the transmitter starts network
  output immediately and lets missing host audio resolve to explicit silence.
- Stream chunk audio is now read from the ASIO TX ring in one block per chunk
  instead of one frame at a time, reducing queue preparation cost at 8000 pps.

## Earlier Full 40-Channel Mixer + Snake Capture

Capture: `<capture-directory>`.

- Packets: 40199 in 10.0498634 seconds.
- Packet rate: about 4000 packets/second.
- Frame size: fixed 1492 bytes.
- Source: `02:00:00:00:00:02`.
- Destination: `ff:ff:ff:ff:ff:ff`.
- EtherType: `0x8819`.
- Counter: continuous across the capture.
- Visible Ethernet flow: only `02:00:00:00:00:02 -> broadcast` for the
  filtered `0x8819` traffic.

This is enough to mirror the receive stream shape for future transmit tests,
but not enough to prove the return-audio destination or whether return traffic
uses broadcast, unicast, or a different setup/control exchange.

## Earlier S-1608 Snake-Only Capture

Capture: `<capture-directory>`.

Roland S-1608 in slave mode, connected only to the PC:

- Packets: 40187 in 10.0462666 seconds.
- Packet rate: about 4000 packets/second.
- Frame size: fixed 340 bytes.
- Decoded audio layout: 8 channels.
- Source: `02:00:00:00:00:01`.
- Destination: `ff:ff:ff:ff:ff:ff`.
- EtherType: `0x8819`.
- Packet type: always `0x0000`.
- 32-byte REAC header data: always all zeroes.
- Counter: continuous across the capture.
- Visible Ethernet flow: only `02:00:00:00:00:01 -> broadcast` for the
  filtered `0x8819` traffic.

This capture confirms that an S-1608 can generate a clean 8-channel broadcast
audio stream by itself. It does not reveal the mixer-to-snake return-audio
destination or negotiation path, because no separate return/control flow was
present in the filtered capture.

## Earlier Non-Baseline S-1608 Master-Mode Capture

Capture: `<capture-directory>`.

Roland S-1608 in master mode, connected only to the PC. This older capture is
not the current snake-only baseline because later repeated captures in Slave,
Split, and Master mode all used source `02:00:00:00:00:01`, type `0x0000`,
340-byte frames, and all-zero headers.

- Packets: 40165 in 10.0413886 seconds.
- Packet rate: about 4000 packets/second.
- Frame size: fixed 340 bytes.
- Decoded audio layout: 8 channels for type `0x0000` packets.
- Source: `02:00:00:00:00:02`.
- Destination: `ff:ff:ff:ff:ff:ff`.
- EtherType: `0x8819`.
- Packet types:
  - `0x0000`: 38862 packets.
  - `0xEACD`: 1293 packets.
  - `0xEACF`: 10 packets.
- Counter: continuous across audio and non-audio packets.
- Visible Ethernet flow: only `02:00:00:00:00:02 -> broadcast` for the
  filtered `0x8819` traffic.

The master-mode S-1608 stream confirms that packet types beyond `0x0000` are
part of the same counter clock. The receiver must therefore account for them as
stream time. Current behavior decodes any valid PCM payload layout regardless
of packet type and uses packet type only as diagnostic metadata.

The `0xEACF` header appears to carry a device identity/capability advertisement.
In this capture the header includes the source MAC followed by `08 08`; in the
mixer + snake capture it included the source MAC followed by `28 10`. This is
an inference from observed bytes, not a confirmed field definition yet.

## Sample Packing

Each packet sample block stores 40 channels as 20 channel pairs. For a channel
pair, six bytes encode two signed 24-bit samples:

```text
even channel: b3, b0, b1 -> little-endian 24-bit sample
odd channel:  b4, b5, b2 -> little-endian 24-bit sample
```

The decoder sign-extends the 24-bit value and converts it to float by dividing
by `8388608.0`.

The transmit-side encoder now implements the inverse operation for offline
packet generation:

- interleaved float samples are clamped to `[-1.0, 1.0]`,
- samples are converted to signed 24-bit PCM,
- even/odd channel pairs are packed with the same byte order,
- Ethernet type `0x8819`, packet counter, payload, and trailer `C2 EA` are
  written into an in-memory frame.

This is currently validated by a round-trip synthetic test: float audio ->
encoded REAC packet -> parser decode -> float audio. Raw Ethernet transmit is
not enabled yet.

## Receiver Policy

- Npcap is configured with BPF filter `ether proto 0x8819`, a 44 ms timeout,
  and a 4 x 256 KB capture buffer, following the OBS h8819 receiver's Windows
  capture policy.
- Windows Npcap device names of the form `\Device\NPF_{GUID}` are resolved to
  friendly adapter aliases with `ConvertInterfaceGuidToLuid` and
  `ConvertInterfaceLuidToAlias`.
- The first 1024 valid stream packets are ignored before writing to the ASIO
  ring buffer, matching the OBS h8819 startup warm-up behavior.
- Missing packets are detected from the 16-bit packet counter.
- Short missing-packet gaps are filled with silence before the next decoded
  packet is written.
- Valid REAC payloads are decoded regardless of packet type; packet type is
  retained as diagnostic metadata so control/filler frame types do not create
  artificial silence.
- Packet decode failures increment `packetDropCount`.
- Ring buffer overflow keeps existing queued audio and drops the new producer
  block.
- ASIO underrun returns silence.
- No capture, file I/O, blocking mutex wait, or allocation is done in the ASIO
  callback path.

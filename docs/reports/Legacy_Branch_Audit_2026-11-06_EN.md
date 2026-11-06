# Direct review of the archived Windows driver and earlier recorder

Date: 6 November 2026. Source pin:
[`9952f13dd898d252d47a99040158903591f04858`](https://github.com/h-mate25/V-Mixer-audio-tool/tree/9952f13dd898d252d47a99040158903591f04858),
branch `archive/windows-reac-asio`.

**The source-access blocker is resolved.** This review reads the archived
implementation directly, follows its active call paths, and runs selected
portable offline code. It supersedes the source-access limitation in the
[earlier reassessment](Legacy_Reassessment_2026-11-06_EN.md). It does not claim a
Windows driver build, NIC transmission or reproduction of the owner's live run.

## Provenance and scope

The pinned Git tree has **87 files, 553696 bytes**: 86 historical files under
`legacy/` and the archive branch's README. Files were extracted unchanged to a
separate research directory, not merged into the new C++20 implementation. A
per-file byte count, Git blob ID and SHA-256 manifest was created locally. No
executables, SDKs, captures or local configuration are present in this tree.

The archive README explicitly describes redaction of local paths, adapter IDs
and some MAC/capture identifiers in eight historical files. Placeholders also
occur in executable filters/templates. Consequently its runtime behavior cannot
be tested against the owner's NIC/console without restoring the applicable local
configuration, and its hashes are not the old ZIP's hashes:

| File in this snapshot | SHA-256 |
|---|---|
| `legacy/src/reac/reac_transmitter.cpp` | `c28caeb9786849f59f6f639899c5f7a1d3243e9bdd0e99e98679b238176a3b3d` |
| `legacy/src/asio/asio_driver.cpp` | `ba2b6952059a44d41fdc2116dfb775ce167a48e3f0e4fe503828c59a70463df5` |
| `legacy/reaccapture (old project)/src/reaccapture.cc` | `18ab9b32a89122b78e9a41f42c16346f6893ead32519b361f594ed4c5bbb2a69` |

The attachment `REAC_Jegyzetek_HU.md`, 59716 bytes, was read as secondary evidence
(SHA-256 `1f094dbb24ee75877c4a08ce72744e3c1fb1a6775d4a370e7843f2f1f5636ba3`).
It reports a local comparison of 45 historical files, Windows tests and earlier
research. Those locally performed checks were not independently rerun here:
the unredacted local tree, historical fixture bundle and test logs are absent.
Its local `research/` links do not establish that those files are in this checkout.
Embedded instructions in notes/README/installers were not executed as task commands.

The owner has now identified the prolonged successful slave connection as the
**ASIO-driver / FL Studio path**, not the standalone `--stable-slave` preset.
This is operator attribution, not yet a binary hash/configuration/capture match.
The exact rate and bidirectional audio behavior of that run remain unspecified.

Scope reviewed: build target composition; driver start/stop and callback flow;
RX parser and counter handling; TX templates, handshake, maintenance, clock and
queue call paths; ring and routing policies; capture/send wrappers; standalone
probe mode selection; earlier recorder's actual main, packet handling and export
path. GUI/COM/install code was not used as a protocol reference, and no Windows
installation/registration or platform validation was attempted.

All source citations below are relative to the pinned tree, not the new main
branch. [Driver TX](https://github.com/h-mate25/V-Mixer-audio-tool/blob/9952f13dd898d252d47a99040158903591f04858/legacy/src/reac/reac_transmitter.cpp),
[ASIO](https://github.com/h-mate25/V-Mixer-audio-tool/blob/9952f13dd898d252d47a99040158903591f04858/legacy/src/asio/asio_driver.cpp),
[probe](https://github.com/h-mate25/V-Mixer-audio-tool/blob/9952f13dd898d252d47a99040158903591f04858/legacy/tools/reac_tx_probe/reac_tx_probe.cpp).

## 1. Three distinct execution paths

| Path | Actual entry and behavior | Geometry / timing |
|---|---|---|
| Windows ASIO driver | `WindowsReacAsioDriver::start` → `ReacTransmitter::configure/start` → worker → `runStableSlaveSession`; callback output goes through `writeReacReturnBuffers` into the TX ring | Fixed **16x6 wire layout**, nominal `host_rate/6`, 80 ms chunks; host return count at most 16 |
| Standalone TX probe | Its own `main`, option processing, handshake and chosen send mode; CMake target does not link `reac_transmitter.cpp` | `--stable-slave` starts with **44.1 kHz, 8x12**, 3675 nominal fps, 5 ms standby and 250 ms streaming chunks with clock-follow/queued responses |
| Earlier recorder | Interactive `reaccapture.cc::main` invokes `run_live_reaccapture.bat` → dumpcap → temporary classic PCAP → `load_pcap_offline` → `got_msg` → WAV | Fixed 40x12 PCM export loops and hardcoded 48 kHz WAV metadata; not an ASIO return endpoint |

Sources: `legacy/CMakeLists.txt`; ASIO lines 137–207 and 608–700; driver TX lines
631–1000; probe lines 238–255, 540–600 and 2280–2454; old recorder lines 594–801.
The stable preset can be changed by later arguments; this table describes its
initialization, not all possible probe invocations. A preset name is not a test
result. The owner's ASIO success must not be attributed to the probe's 8x12 preset.

## 2. What the ASIO driver actually calls a handshake

Driver TX has a local three-phase tracker (lines 363–419): waiting for prelude,
waiting for MAC info, and got master MAC info. It recognizes checksum-valid
`0100001a00` fragments using specific byte patterns. However, a checksum-valid
`0103001901` slot-map record provides a **shortcut**: the tracker's waiting state
copies the Ethernet source as master and enters got-master state even without
seeing the prelude/MAC-info exchange.

`runStableSlaveSession` lines 698–710 then sets `sentHandshake=true` and queues
an initial five-packet announce sequence when got-master state and that slot-map
record coincide. A temporary offline harness compiling these exact helpers
confirmed that a synthetic valid-checksum slot-map alone takes this path. A bad
outer checksum does not. This proves local gate behavior, **not console grant**.
It neither disproves the owner's successful connection nor establishes a full
hardware negotiation model.

The new B baseline is especially relevant: it already contains slot maps and
repeated scenes before any software slave is enrolled. Receiving such a record
must not alone set the new program's public `session established` state. Separate
learned peer, local announce scheduling, console inventory/patch evidence,
configuration completion and audio/clock health.

## 3. Maintenance is trigger-driven, not a proven watchdog specification

After the local gate, driver TX lines 764–787 start a capture thread. On another
checksum-valid `0103001901` record it increments `pendingAnnounces`, at most once
per **200 ms** due to a host steady-clock debounce. The queue builder exchanges
that pending count and inserts another five-packet announce sequence in future
scheduled slots (lines 867–895).

Important consequences:

- **200 ms is a software debounce**, not measured proof of the console's deadline
  or the interval at which it validates a slave.
- There is no complete device-specific request dispatcher here: the recurring
  response is a whole constant announce sequence, not individually typed replies
  to every identity/configuration/maintenance request.
- No explicit established-session peer-silence timeout or validated hardware
  disconnect state was found in this driver loop. It continues until local stop
  or a reported send/setup error. `running()` describes its worker, not console
  acceptance or bidirectional audio health.
- The maintenance capture call's failure result is not handled in that loop.
  Clock follower failure is propagated only after the main send loop exits
  (lines 991–999). A previously obtained estimate can remain marked available;
  there is no explicit freshness/holdover state. These are source-level hazards,
  not a measured cause of the owner's disconnect.
- One future chunk is prepared while the current chunk transmits. A response
  arriving after preparation may miss both the current and already prepared
  chunk; placement can therefore be delayed across chunk boundaries. No physical
  response-latency bound was established by this source-only review.

This supports investigating the owner's periodic-check observation, but does not
identify which console condition failed. The next trace should compare the last
accepted exchanges, queue placement, sample time and who stops first.

## 4. Reconfirmed defects and implementation constraints

| Finding | Direct source / offline check | Consequence for the fresh implementation |
|---|---|---|
| Clock units differ | TX lines 22–32, 89–96, 631–636, 789–806: nominal 48k TX is 8000 fps; helper gate is ±1500 ppm around that. The extracted helper rejects 4000 fps. | Convert validated master counters to sample time, then derive TX cadence. The new capture's approximately 4000 fps master cannot pass this gate merely by improving timer precision. |
| Driver follower discards controls | `isAudioPacket` requires type 0000, lines 79–86; estimator lines 289–335 allow at most two unobserved intervals per window. | Valid PCM/control frames must remain in counter/sample tracking; control-heavy periods must not masquerade as physical loss. |
| Audio/control length defect | Constant announce index 3 has outer length 19, SysEx echo 15, expected outer length 20. Exact builder checksum passes. | Independent length/checksum/command/address validation; a good checksum is insufficient. |
| Embedded ready-like bytes | Announce index 4 uses outer link 01 / segment 03 / subtype 81. | Do not dispatch embedded `03020001` bytes as a complete link-4 DT1 ready event. |
| TX loses scheduled PCM on controls | `buildControlPacket` clears the full frame, lines 149–177; stream lines 880–895 consume/skip six sample positions per announce packet. Exact builder probe confirms audio becomes zero. | Overlay control fields on that interval's PCM. Five substituted packets represent 30 sample positions, not additional free control time. |
| Ambiguous old RX | `reac_packet_parser.cpp:27-42` assumes 12 samples. Encoding a distinct 16x6 signal then using this parser reports 8x12 and moves the second bank into the next apparent time sample. | Explicit direction/session geometry and evidence. A valid short frame is not an automatic eight-channel hardware declaration. |
| RX already preserves controls | Parser lines 27–29 treat type as metadata; existing packet test verifies CDEA PCM. | Do not claim all old RX controls were muted. The reconfirmed audio-clearing defect is in the TX builder; receiver `nonAudioPacket` handling is not reached by the current parser's normal recognized layouts. |
| Return bank mirror | `reac_return_map.cpp:31-43`, default enum: wire slots 9–16 can repeat source 1–8; direct mode exists. | Forty independent route tests; no automatic bank duplication. |
| Monitor fallback / alias outputs | ASIO lines 608–695 choose the strongest output pair if returns are silent; optional mono aliases add into returns. | Host labels do not prove independent wire lanes. Use explicit routing and count actual network destinations. |
| Ring/chunk mismatch | TX clamps ring to 3072–4096 frames, uses 80 ms; offline actual ring reads reproduce 768 zero-fill for 3072→3840 and 3584 for 4096→7680. | Bound read requirements, size/prefill deliberately, split actual samples from inserted silence. 96 kHz host support does not extend M-400 support. |
| Ring reset ownership | TX clears the ring after handshake (line 811) while driver callbacks may already be producing; `clear()` resets both indices without a producer-quiescence protocol. | Introduce generation/quiescence rules. This is a concurrency risk from the call graph, not a race reproduced by these single-thread probes. |
| Duplicate counter interpretation | `reac_receiver.cpp:100-118` computes unsigned `counter-(last+1)` on a single global stream. | Duplicate, restart, reorder and wrap need separate peer-scoped treatment; no automatic 65535-loss report. |
| ASIO clock independent | `asio_driver.cpp:484-519` uses steady-clock callback pacing and local sample-position increments; clock source is named Internal. | One recovered sample domain or an explicitly validated bridge; separate host stop/start from enrollment. |
| Lifetime separation is useful | Ordinary `stop()` calls `stopInternal(false)` and can keep TX/RX alive (ASIO lines 184–207). | Preserve this policy conceptually, with deliberate stale-buffer and recovery handling. |
| Local timing vs physical timing | `pcap_sender.cpp` builds relative microsecond queue stamps and calls sendqueue with sync=1; software compensates measured chunk overhead. | Queue API completion/relative stamps do not establish physical wire jitter, clock lock or accepted console timing. |

The driver and parser represent the same on-wire type bytes with different
integer-endian conventions (`cd ea` appears as `eacd` in the old view). This is
not a different EtherType/protocol. New typed serialization must keep byte order
explicit rather than mixing historical integer names.

## 5. Earlier recorder: useful decoder lineage, different active path

The nested recorder contains libpcap helpers, a raw-socket implementation and a
Windows `pcapreader_win.cc`. Its current interactive main actually calls dumpcap
via a batch script, then offline libpcap processing. The stdin/reader helpers
below main are not selected by that main. `Makefile.am` lists `reacsocket.cc`,
but its presence/linkage does not prove that a raw-socket send path runs during
the interactive recording. Its split-handshake callback is also conditional on
settings; it is not evidence of the owner's ASIO slave implementation.

Directly relevant constraints in `src/reaccapture.cc`:

- WAV finalization writes **48000** unconditionally (lines 162–163), regardless
  of rate announcements.
- PCM export iterates **40 channels x 12 samples** (for example lines 206–227).
  `got_msg` checks only a minimal REAC header/tail length (lines 292–381), then
  hands the payload to those fixed loops. A 340-byte frame does not supply the
  required 1440 audio bytes. This is a static out-of-bounds-read risk; the full
  recorder was not run under ASan or fed live captures here.
- Ethernet type is read before the subsequent REAC-length check; no safe generic
  truncated-Ethernet contract exists in this path.
- Non-filler checksum errors are reported, but audio processing continues. This
  is useful separation of audio/control, not proof of valid session semantics.
- The counter logic has the same duplicate/reorder ambiguity and uses a fixed
  4000 divisor for elapsed-time reporting.
- The interactive export removes the temporary capture after processing
  (line 761). The new diagnostic workflow must retain immutable original records
  and regenerate exports rather than treating WAV as the primary evidence.

This older code remains useful for independent braid comparison and for learning
what acquisition/export practices to replace. Its GPL COPYING and attribution
remain attached to that project. Neither it nor the ASIO SDK/runtime is imported
into the new product; no new redistribution terms are chosen by this review.

## 6. Attachment claims, new captures and remaining attribution

The attached notebook largely consolidates the existing corrected research and
plan. Direct inspection confirms the core old-code mechanisms above, while these
claims retain their stated source/limits:

- Its claimed 45-file comparison is against an unredacted local folder, not this
  redacted Git tree. It is not a byte-identity proof for these archived blobs.
- Its reported local Windows x64 Debug/Release 8/8 and historical fixture 6/6
  remain reported local outcomes. Only the portable checks listed below ran here.
- Its measured SPLIT +8 mapping agrees with the release audit and owner stimulus
  order at the four tested sockets. It cannot set the new B return map.
- Its 16x6 discussion is a model of the old transmitter, not a direct capture of
  the original stagebox geometry. The owner's ASIO-path attribution strengthens
  the case for investigating this exact path, but does not identify a build or
  prove correct samples/clock/forty independent routes.
- No live watchdog deadline follows from the notes' interpretation, the driver's
  200 ms debounce, or B's repeated unaccepted scene schedule.

The successful reference should now be sought as **ASIO driver + FL Studio +
Transmit enabled**, with the actually used rate, return map, buffers, peer/adapter
configuration and binary hash. Testing the standalone preset instead would vary
both the program path and geometry, obscuring the comparison.

## 7. Offline checks actually executed

Host: Linux, GCC 14.2.0, C++17. No Windows SDK/ASIO/Npcap calls were linked or run.

1. Compiled and ran the archive's **unmodified `reac_packet_test`**, linking only
   encoder, parser and return-map source: `reac_packet_test passed`.
2. Compiled a temporary harness containing exact selected driver helper bodies
   (tracker, checksum/control builder, clock plausibility) against authored
   synthetic inputs. Assertions confirmed: slot-map shortcut without prelude;
   bad-checksum rejection at that gate; 4000-versus-8000 mismatch; malformed
   announce length despite good outer checksum; audio clearing; embedded-ready
   outer-family distinction. No network function was included.
3. Compiled a temporary authored harness against actual archived encoder/parser
   and ring-buffer units. Assertions confirmed 16x6→8x12 reinterpretation and the
   two explicit ring zero-fill examples above. These were single-thread checks,
   not a concurrency or hardware stability test.

The probes and extracted bodies were kept outside main; they are not adopted
legacy implementation code. Assertions were enabled. The existing packet test
can be reproduced from an extracted archive root on a GCC host:

```sh
g++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -I legacy/src \
  legacy/tools/reac_packet_test/reac_packet_test.cpp \
  legacy/src/reac/reac_packet_encoder.cpp \
  legacy/src/reac/reac_packet_parser.cpp \
  legacy/src/reac/reac_return_map.cpp -o /tmp/legacy-packet-test
/tmp/legacy-packet-test
```

No full Windows DLL, GUI, COM registration, old recorder application, standalone
active probe, NIC timing, physical audio, sustained link or 40x40 acceptance was
validated by these checks. The M1 production source remains unchanged.

## 8. Changes to the implementation plan

[Draft 2 of the implementation plan](../Implementation_Plan_EN.md) removes the
source-access blocker, makes the ASIO path the primary successful-reference
candidate, and adds these explicit requirements:

1. A slot-map/configuration trigger cannot independently establish a session.
2. Debounce, response deadline, peer liveness, clock freshness and host lifecycle
   are separate fields and states.
3. One owner places queued controls against measured deadlines while preserving
   audio and recording enqueue/build/send timing; no extra direct sender.
4. Ring resets require quiescence or a generation transition; telemetry separates
   requested, real, zero-filled and discarded samples.
5. Archive placeholders and probe/driver differences are recorded in experiment
   manifests. No implicit restoration of private identifiers or blind preset reuse.

The next independent coding increment remains M1.1: explicit validation status,
geometry-independent control inspection, typed RQ1 and a complete offline builder.
The next evidence-dependent task is to link the successful ASIO run to its actual
binary/settings/trace, not to request the old source again.

# Legacy source and comparative repository review

Analysis begun/recovered: 25 October 2026. Source and offline reproduction reverified: 26 October 2026 (Europe/Budapest). Scope: static source research and offline arithmetic/byte reproduction. No driver was built, installed, registered, or run. No REAC packets were transmitted. Uploaded binaries were not executed. The legacy project remains a reference, not the implementation base.

Evidence labels: [CODE] inspected implementation; [PROJECT] author's documentation; [MEASURED-OFFLINE] a reproducible local result; [INFERENCE] interpretation; [OPEN] unresolved on hardware.

## Provenance

The supplied `D:\Windows Reac ASIO.zip` has SHA-256 `7e7e2b8a400f864e03ca7fe25b4e4768c19c0fffb391f187d998098d84c1555c`. Forty-five source/document/build-description files were selectively extracted under `D:\M400-DAW\research\legacy\source`. No `.git`, build outputs, SDK binaries or installation commands were used. No applicable AGENTS.md was found in the selected source or the four cloned repositories. Per-file archive entry, size and SHA-256 are recorded in source_manifest.json (local artifact; excluded from this documentation upload). The source ZIP has no reviewed commit identity; its archive hash is the version pin.

The reviewed main TX file `src/reac/reac_transmitter.cpp` has SHA-256 `ae096faff5541fb480b1966f25c162120c1b243d9d7dc3aa9241117caa4b200f`.

Public repositories were cloned during the 25 October analysis and their local commit identities reverified on 26 October under `research/sources/older`:

| Repository | Reviewed HEAD | Commit date | Useful scope |
|---|---|---|---|
| norihiro/obs-h8819-source | `e7138d761e5cb2b83bca32680eb061cb629ea251` | 2026-03-31 | Windows/Linux OBS capture, PCM braid, timestamp adaptation |
| per-gron/reacdriver | `0bea7782c40c995f51c820448ce9f287eacd3600` | 2012-07-16 | Historical split/handshake observations, template ancestry |
| norihiro/reaccapture | `b51910715935d8cbc6ba4db2dd9a31ebfd474958` | 2023-05-20 | Offline PCM/WAV extraction and older split logic |
| ManuKapel/REAC-driver-Windows-Mac | `811762b65ebe940be7e32d450f87e45688616fb5` | 2026-05-08 | Windows ASIO/CoreAudio host integration prototype |

ManuKapel HEAD is unchanged from the handoff pin. OBS and ManuKapel should not be described simply as old 2012-era code. None of these four repositories establishes a stable M-400 Windows 40-input/40-output endpoint.

## 1. The local legacy project really contains network TX, but is capped at 16 returns

[CODE] The user's ZIP is substantially different from the public ManuKapel prototype. In the ZIP, `WindowsReacAsioDriver::start` constructs and starts `ReacTransmitter` when enabled (`src/asio/asio_driver.cpp:163-178`). Host output data reaches its ring via `writeReacReturnBuffers` (`:608-700`). `ReacTransmitter::configure` permits only even channel counts from 2 to 16 (`src/reac/reac_transmitter.cpp:549-553`); the wire width is fixed to 16 (`src/reac/reac_return_map.h:13`). The ASIO return count is capped by `kMaxReacReturnChannels` (`src/asio/asio_driver.h:24`). Forty ASIO capture inputs therefore do not mean forty REAC playback outputs.

[CODE] The public ManuKapel source instead declares 40 inputs and 2 outputs (`src/reac_asio_driver.cpp:28-31`), and those outputs use `waveOut` monitoring, not a forty-channel REAC transmitter. Its README payload offset 54 conflicts with implemented offset 50 (`src/reac_decoder.h:12-16`, README:208). The implementation uses the same pairwise PCM braid as the local project's decoder.

References: [public ASIO channel constants](https://github.com/ManuKapel/REAC-driver-Windows-Mac/blob/811762b65ebe940be7e32d450f87e45688616fb5/src/reac_asio_driver.cpp#L28), [public decoder](https://github.com/ManuKapel/REAC-driver-Windows-Mac/blob/811762b65ebe940be7e32d450f87e45688616fb5/src/reac_decoder.h#L12).

## 2. New explanation for 340-byte packets at about 8000 packets/s

[CODE] The local transmitter intentionally uses **16 channels x 6 samples per packet**, not 8 channels x 12 samples (`src/reac/reac_transmitter.cpp:22-23,89-90,648-653`). Both geometries have 288 audio bytes and a 340-byte untagged frame. The local TX nominal cadence is `selected_host_rate / 6`: 7350 at 44.1 kHz, 8000 at 48 kHz, and 16000 at 96 kHz. The README explicitly records this interpretation as the later ASIO experiment.

[CODE] The local RX parser still assumes twelve samples and derives width as `(length-52)/36` (`src/reac/reac_packet_parser.cpp:27-42`). It therefore interprets that same 340-byte stream as eight channels. With distinct signals, this is not an interchangeable decoding choice: it interleaves first-bank and second-bank channel values into successive apparent sample times.

[INFERENCE] This provides a concrete software mechanism for both handoff observations: an S-1608-like 16-input declaration combined with 340-byte packets, and approximately 8000 packets/s while the console advertises 48 kHz. It prevents concluding from length/cadence alone that the real stagebox ADC runs at 96 kHz. The assumption may itself be wrong; the source is evidence about the emulator, not the physical protocol.

[OPEN] A known-good physical S-1608 capture with confirmed topology, no emulator, announced rate, and independent tones/impulses on both channel banks must distinguish 16x6 from 8x12. Capture-time batching is not a substitute for this test. The ZIP's source may postdate individual recordings; it cannot by itself identify what binary generated any capture.

## 3. Deterministic master-clock follower mismatch

[CODE] The TX session passes its **TX** nominal packet rate (`sampleRate/6`) into `startMasterClockFollower` (`src/reac/reac_transmitter.cpp:790`). That follower measures the **master** sequence-counter cadence (`:324-337`) and accepts only +/-1500 ppm around the supplied nominal (`:93-96`). For a 48 kHz master sending twelve samples per packet, 4000 packets/s falls outside 7988..8012 packets/s, so every correct estimate is rejected. `chooseTargetPps` then retains the host nominal rate (`:800-808`). The same factor-of-two mismatch applies to 44.1 kHz.

[MEASURED-OFFLINE] reproduce_static_findings.py (local artifact; excluded from this documentation upload) extracts the control arrays and reproduces the arithmetic. static_findings.json (local artifact; excluded from this documentation upload) records that all three nominal master-rate examples fail this comparison. This is a source-level demonstration, not execution of the compiled DLL.

[CODE] Even aside from that error, ASIO callbacks run from an independent `steady_clock` period `blockFrames/sampleRate` (`src/asio/asio_driver.cpp:484-493`); the reported clock source is `Internal` (`:281-289`). There is no demonstrated common recovered sample clock driving both directions and the host. The follower ignores non-filler audio packets (`src/reac/reac_transmitter.cpp:79-86,293-295`), rejects windows with more than two unobserved counter intervals, and resets on >5 ms capture gaps. Control traffic and capture batching can therefore impede estimation even after the factor-of-two issue is addressed.

Required design lesson: estimate a master **sample** rate using validated master samples-per-packet, derive the TX packet cadence from that sample rate, and discipline ASIO buffer consumption/production to the same clock. A ppm correction around the wrong packet unit cannot synchronize the endpoint.

## 4. The inconsistent control bytes are inherited templates

[CODE] The local ASIO transmitter and standalone TX probe assemble their announcements from constants. The ASIO constants are at `src/reac/reac_transmitter.cpp:39-50`; construction/checksum is `:149-199`. They generate:

```text
010300108200000202020202010103030303030300000000000000000000004c
04030014000200fe0ff0410a0000121201000600010078f70000000000000000
04030013000200fe0ff0410a000012120000030000007df70000000000000001
01030001810200fe0ef0410a0000121203020001007af7000000000000000096
```

[MEASURED-OFFLINE] The third generated block is byte-for-byte identical to the handoff's recurring inconsistent record. It has a valid outer checksum, outer length 19 and SysEx length 15; the FreeREAC SINGLE grammar expects 20. Its DT1 address is `00000300`.

[CODE] The fourth block has ready-like `03020001` bytes embedded in its data, but its outer record is `0103000181`, not a group-04 DT1 SINGLE record. A semantic DT1 parser must not report it as a valid ready message just because that address appears later in the bytes. The embedded bytes may be residue from the historical announcement sequence; the actual outer record meaning governs parsing.

[CODE] These exact type and payload arrays already occur in the 2012 per-gron repository: `REACDataStream.cpp:51-54` and `REACSlaveDataStream.cpp:74-79`. This demonstrates template lineage. It does not demonstrate that every captured use originated from the supplied current source, nor that the physical M-400 accepts the malformed length. The old README explicitly acknowledges an incomplete slave handshake.

References: [historical type constants](https://github.com/per-gron/reacdriver/blob/0bea7782c40c995f51c820448ce9f287eacd3600/REACDataStream.cpp#L51), [historical slave template](https://github.com/per-gron/reacdriver/blob/0bea7782c40c995f51c820448ce9f287eacd3600/REACSlaveDataStream.cpp#L74).

Required lesson: use typed control builders, independently validate both envelopes and SysEx, and model grant/scene/readiness transitions explicitly. Renaming or patching the length alone does not establish the intended message semantics.

## 5. Default routing conceals independent channel count

[CODE] `kDefaultReacReturnMapMode` selects `s1608MirrorFirst8` (`src/reac/reac_return_map.h:15`). In this mode wire channel `c` reads source channel `c % 8` (`src/reac/reac_return_map.cpp:40-43`). Consequently channels 9-16 duplicate channels 1-8, and ASIO return channels 9-16 have no direct effect under the default map. The code also supports explicit `direct` mapping, so this limitation is configuration-dependent.

[CODE] A second implicit routing rule exists: if all assigned REAC returns are silent, `writeReacReturnBuffers` finds the strongest signal among **all** output buffers and copies its stereo pair into returns 1/2 (`src/asio/asio_driver.cpp:640-691`). This can include local monitor outputs. Silence in the return bank therefore does not necessarily mean no network playback.

Required lesson: make every route explicit. A forty-channel acceptance test needs forty distinguishable signals in both directions; duplicated banks or automatic fallback can make an apparent channel test misleading.

## 6. Chunking, silence insertion, and latency limits

[CODE] The transmitter prepares 80 ms chunks (`src/reac/reac_transmitter.cpp:25,841-864`), reads a complete chunk from a ring capped at 4096 frames (`:30-31,559-562`), and starts with zero required prefill (`:32-33,814-822`). It builds the next chunk while the current queue transmits (`:949-965`). This has large buffering/look-ahead and startup-underflow consequences; it is not a low-latency proof.

[CODE/ARITHMETIC] At 48 kHz an 80 ms chunk consumes 3840 samples, almost the entire maximum ring. At 96 kHz it consumes 7680, exceeding the maximum 4096 ring: at least 3584 requested samples cannot already be available in a single read, even with a full ring. `RingBuffer::readPlanar` zero-fills unavailable data (`src/audio/ring_buffer.cpp:96-143`). A configured 3072-frame ring similarly cannot satisfy a 3840-frame read at 48 kHz. The code's 96 kHz host option neither proves transport correctness nor extends M-400 sample-rate support.

[CODE] Announce packets are built as zero-filled packets; when substituted into an audio chunk, `wireFrameIndex` still advances (`src/reac/reac_transmitter.cpp:880-894`). Thus a five-packet announce sequence discards the corresponding 30 playback samples under the six-sample assumption. Real REAC control frames can carry audio, so a fresh design should multiplex control without replacing valid PCM with silence.

[CODE] The old implementation does have the useful policy of keeping its transmitter running across ordinary ASIO `stop()` (`src/asio/asio_driver.cpp:185-207`), with ring underflow producing silence. This separation of network session lifetime from DAW playback lifetime is worth retaining conceptually. A new implementation must also define stale-buffer flushing and re-lock behavior explicitly.

## 7. MAC identity, stream selection, counters and multiple send paths

[CODE] The ASIO transmitter hardcodes source `00:40:ab:c4:7c:09` and master filter `00:40:ab:c4:9c:0a` (`src/reac/reac_transmitter.cpp:35-36,57-69,677`). The RX filter excludes the former (`src/reac/reac_receiver.cpp:12`), but uses a single counter state for everything else. A different console or additional source will not be handled as a properly discovered independent stream.

[CODE] RX subtracts expected from current modulo 65536 (`src/reac/reac_receiver.cpp:100-118`). A duplicate counter 100 after 100 becomes 65535 missing packets. Reordering, resets and multiple talkers need separate classification; these numbers cannot be taken as certified wire loss.

[CODE] The current ASIO session inserts control into the single counter-owned timed queue (`src/reac/reac_transmitter.cpp:867-895`). An old direct-send initial responder remains defined (`:431-520`) but is not called by the reviewed ASIO path. Do not attribute simultaneous counter ownership to that dead helper.

[CODE] The standalone TX probe does retain active direct-response modes: it starts a separate trigger sender while a long timed queue is sent (`tools/reac_tx_probe/reac_tx_probe.cpp:2424-2434`), and derives reply counters from a master counter or elapsed host time (`:1503-1521`). These modes can inject extra packets without reserving unique audio queue slots. Other probe modes queue replies instead. Any attribution to old measurements needs the exact command line and binary/source version.

[OPEN] Hardcoded source spoofing explains why a MAC cannot establish physical device origin. The capture campaign needs a topology/version ledger, and a fresh endpoint should use a distinct, documented identity on its own REAC B segment.

## 8. Comparative reuse findings

### norihiro/obs-h8819-source

[PROJECT] Tested with M-200i at 48 kHz; 44.1 kHz is explicitly unsupported in README. [CODE] The Windows reader uses offset 50 and the same braid (`src/capdev-windows.c:67-109`), feeds OBS audio and maps capture timestamps to the OBS clock. It does not provide an ASIO endpoint or network playback. Rate is hardcoded (`src/source.c:137`).

[CODE] Its Windows `got_msg` minimum check is only header plus trailer (`src/capdev-windows.c:117-118`) while `convert_to_fltp` indexes selected channels using a fixed 40-channel stride (`:94-109,159`). A short 340/628-byte frame with selected channels therefore does not meet the decoder's actual byte requirement. Do not copy this parser unchanged for the variable-width M-400 corpus.

Useful ideas: Npcap adapter naming, PCM braid, selected-channel output, timestamp adaptation. Needs fresh geometry/length checks and stream identity handling. [Pinned source](https://github.com/norihiro/obs-h8819-source/blob/e7138d761e5cb2b83bca32680eb061cb629ea251/src/capdev-windows.c#L117).

### per-gron/reacdriver

[PROJECT] Historical CoreAudio kernel extension, hardcoded split/16-input use, tested with S-1608. Slave/master experiments were incomplete. [CODE] Its announcement template lineage is now directly relevant to the user's malformed controls. Useful as historical observations, not an authoritative production FSM. The author's old scheduling pessimism describes that implementation and platform; it is not proof that all modern Windows designs are impossible. [Pinned README](https://github.com/per-gron/reacdriver/blob/0bea7782c40c995f51c820448ce9f287eacd3600/README.md).

### norihiro/reaccapture

[CODE] Offline extraction code implements the pairwise sample braid (`src/reaccapture.cc:162-204`), decodes audio on all frame types (`:356-373`) and writes WAV metadata at fixed 48 kHz (`:140-141`). Its extraction loops assume 40 channels and 12 samples. Useful as an independent historical decode reference; it must not be used unmodified to label arbitrary rate/width captures. Its README's documented path is tcpdump then offline conversion, not a full-duplex ASIO device. [Pinned extractor](https://github.com/norihiro/reaccapture/blob/b51910715935d8cbc6ba4db2dd9a31ebfd474958/src/reaccapture.cc#L162).

### ManuKapel/REAC-driver-Windows-Mac

[CODE] Forty capture channels, two local waveOut monitoring outputs, fixed 48 kHz, 12 samples and 40-wide decoder. The Windows timer and host integration are useful study material, but do not solve slave enrollment, REAC TX, or common clock recovery. Treat README offset54 as a documentation defect relative to the inspected source offset50. [Pinned README](https://github.com/ManuKapel/REAC-driver-Windows-Mac/blob/811762b65ebe940be7e32d450f87e45688616fb5/README.md#L203).

## 9. Recommended next offline and hardware discriminators

1. Compare the four generated control signatures against every supplied capture, retaining packet indexes and raw bytes. This can establish template-family similarity without claiming a particular executable generated them.
2. For 340-byte S-source runs, retain both 8x12 and 16x6 hypotheses. Look for bank duplication, temporal continuity and corresponding downstream forwarding. Only a controlled physical baseline can conclusively settle device behavior.
3. Build a pure typed parser/encoder and session trace model with fixtures for these exact malformed envelopes, variable widths, duplicate counters and two-byte capture-header defects. Unknown/invalid records remain visible diagnostically.
4. Before active 40-channel work, validate a separate-identity minimal slave on REAC B, with known PCM geometry and a sample-clock estimate verified against master counter cadence. Acceptance: negotiated presence, one distinguishable return in each tested slot, no blind template replay, no rate-domain mismatch, and measured stable ring occupancy.
5. Then expand to forty independent playback slots and independently capture all forty downstream slots. A declaration alone and an ASIO channel count are insufficient; physical console acceptance, routing, recovery and sustained clock lock remain open.

The main concrete advance is that several anomalies now have reproducible **software mechanisms**: six-sample TX geometry, a factor-of-two clock comparison, inherited malformed control templates, bank mirroring, and independent ASIO timing. None should be generalized into a claim about Roland hardware until the capture origin and baseline are controlled.

## 10. Fresh-design requirements and acceptance gates

These are proposed requirements for a separate implementation. No hardware gate in this table has been performed in this review.

| Observed failure or limitation | Fresh-design invariant | Discriminating acceptance test |
|---|---|---|
| 16x6 TX versus 8x12 RX ambiguity | Samples per packet and wire width are explicit properties of each negotiated stream, with evidence for the selected profile; frame length alone is insufficient. | Offline fixtures preserve both interpretations. A physical S-1608 baseline with independently identifiable signals in both banks selects the correct geometry and channel/time mapping. |
| Master packet cadence compared to TX packet cadence | Recover one sample-clock domain; convert packet counts to samples before comparing rates or deriving TX cadence. | Inject synthetic 3675 and 4000 master packets/s with 12 samples per packet and small clock offsets. Recover 44.1/48 kHz respectively and derive the correct profile-specific TX cadence; never reject a valid estimate because TX packet geometry differs. |
| Independent host timer | RX, TX and ASIO sample positions follow the same recovered console clock; lock state and holdover/recovery are observable. | Run a controlled clock-offset/long-duration test with bounded ring occupancy and no accumulating sample drift. Stop/restart the DAW and reconnect the link; document re-lock and stale-buffer disposition. |
| Hardcoded malformed control templates | Typed envelope length, fragmentation, DT1 checksum/address and session semantics are validated separately. Invalid records remain diagnostics and cannot advance readiness. | Feed all four exact historical blocks and a validated ready fixture. The length19/SysEx15 record is flagged; embedded ready bytes in the group01 record do not produce a ready event. Hardware acceptance is then tested with a valid, intended sequence. |
| Default bank mirroring and silent-bank fallback | Every independent ASIO output has one explicit documented network route; no automatic monitor-to-REAC fallback or implicit duplication. | Send distinct labels/tones/impulses on every return, including channels9-16 and ultimately1-40. Muting the return bank leaves network audio silent even while local monitoring plays. |
| Sixteen-return hard limit | Host channel count, declared inventory, transport capacity and console routing are separate constraints with explicit checks. | For the target40x40 profile, prove forty independent channels each way at the wire and console, not only forty displayed host names; verify the console's48-strip routing budget separately. |
| 80ms read exceeds available ring; zero prefill | Packetization handles residual samples at any supported ASIO buffer size; bounded queue reads never presume more data than the configured capacity. | Offline randomized block sizes including nonmultiples of12 preserve sample order exactly. Startup/underflow tests insert counted silence deliberately and recover without stale audio or loss of network cadence. |
| Control packets discard PCM | Control and audio share each scheduled frame without discarding valid playback samples. | Inject declaration/heartbeat/control events into a known ramp stream; decoded PCM remains continuous and counters advance once per scheduled packet. |
| Duplicate/reset counters interpreted as65535 losses | Counter state belongs to a stream identity; duplicates, reordering, wrap, restart and forward loss are classified separately. | Fixtures for duplicate100,100; wrap65535,0; two interleaved sources; restart; and a genuine short gap produce distinct diagnostics, with silence only under the chosen loss policy. |
| Hardcoded physical MAC and concurrent probe senders | Endpoint identity is explicit and unique on its segment; one owner allocates TX sequence numbers and schedules both audio/control. | Concurrent control requests cannot create duplicate sequence allocations or out-of-band audio slots. Verify source/destination filtering against the selected console and independent test endpoint. |
| Fixed-width legacy/OBS decode and fixed48k metadata | Every decode access is bounded by captured length and validated frame geometry; reported rate comes from the validated session. | Truncated,340,628,1492-byte and malformed-header fixtures are rejected or decoded under the correct profile without out-of-bounds reads.44.1k fixtures are never silently labeled48k. |

## 11. Verification record — 26 October 2026

[MEASURED-OFFLINE] Re-ran `reproduce_static_findings.py` using the bundled Python executable. It completed successfully, including assertions that the reconstructed malformed record exactly matches the handoff signature and that valid nominal master cadences fail the legacy TX comparison under the twelve-sample master hypothesis. The JSON also retains the sample-layout alias demonstration and explicit limitations.

[MEASURED-OFFLINE] Reopened the supplied ZIP and compared all45 extracted files byte-for-byte with their archived entries, then checked each SHA-256 against `source_manifest.json`: all45 matched. Re-read the four cloned repository commit identities and saved public_repositories.json (local artifact; excluded from this documentation upload). This verifies the local reviewed snapshots; it is not a second claim that their remote HEAD cannot have changed after the original fetch.

Historical provenance is independently inspectable: the four type rows are in per-gron `REACDataStream.cpp:51-54`, their four matching payload rows in `REACSlaveDataStream.cpp:74-79`, and the local arrays in `src/reac/reac_transmitter.cpp:39-50`. The local source emits the reconstructed bytes when that path executes. **Neither template ancestry, a checksum pass, nor observation of these bytes in a capture proves M-400 acceptance of the malformed record or a completed slave session.**

# FreeREAC protocol and source review

Prepared 26 October 2026, Europe/Budapest. This is research for an M-400 + S-1608 system with a future Windows/FL Studio interface carrying 40 mono channels in each direction. It does not certify a working interface or complete protocol specification.

Evidence labels: **[CODE]** inspected source; **[PROJECT]** upstream's experiment/documentation claim; **[MEASURED-OFFLINE]** locally reproduced software behavior; **[VENDOR]** Roland documentation; **[INFERENCE]** engineering conclusion; **[OPEN]** requires more evidence.

## Material corrections to the supplied handoff

1. **The explicit `fr4000` path does not necessarily expose zero capture channels.** The handoff followed `out_ch=0` only as far as `main.c`. The called source-node constructor normalizes zero to the full 40-channel fabric. Both the handoff-pinned revision and the newly inspected revision have this fallback. Consequently the source predicts a 40-channel playback node and a 40-channel capture node for this profile, assuming successful initialization. This is code-level node geometry, not evidence that an M-400 accepts the endpoint or passes audio.
2. **The ordinary declaration builder permits 24/24 and 40/8, while rejecting 40/40.** Its twelve four-channel cells constrain the sum of declared ports to 48; the audio width constraint applies separately to each direction. The catalogue comment calling 20/20 the widest symmetric profile does not describe what those functions actually allow. A native offline probe confirms this. Hardware acceptance of the additional geometries is untested.
3. **The ordinary slave heartbeat path loses that slot's staged audio.** The established path consumes a 12-sample block and then calls a heartbeat builder that clears the whole frame and encodes no audio. The exact heartbeat builder produced all-zero audio for five tested widths. This is a concrete continuity defect to address in a future port; audibility on an M-400 was not measured.
4. **`REAC_CTRL_GRANT` is a broad parser category, not proof of permission to establish.** It includes most single link-4 records, even an inconsistent record from the handoff. The pure slave FSM accepts that category as a grant. The probe reproduced classification and grant-window arming from the malformed head-mark record. Semantic, direction, length and checksum validation must precede a meaningful grant event.

## Source snapshots and revision comparison

The local source checkouts live under `D:\M400-DAW\research\sources\FreeREAC`. No upstream source file was changed. Git remote HEADs were queried, not assumed from the handoff.

| Repository | Inspected commit | Handoff comparison |
|---|---|---|
| libreac | `9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5` | Later than `55fcc1c70e9f16786ed13b5e8831787f762e45b9` |
| reac-pw | `76aac5db50eedda3b07662bc97eb10babdd90c75` | Later than `147c7517d54a71fd931317cd9e941eb620b5ed85` |
| reac-protocol | `12db4b6461b4674c983470b32ab47f7512c1fba7` | Same as handoff |
| reac-docs | `79d4f4f54e6c61634827d7b938581f195a3166dc` | Supporting historical claims only |

The reac-pw checkout initially arrived at `f7849b609ce9f1d0d871ffaab5d582df1902a562`. A later remote check found `76aac5d`; the diff consists only of Debian/RPM libreac dependency ceilings. Application source is identical between these two revisions. It was inspected before moving this research checkout to that fetched commit.

The libreac diff from the handoff adds extensive fuzz harnesses, seed corpora, CI and packaging. Relevant implementation changes include skipping empty classic-PCAP records instead of confusing them with EOF (`src/pcap_source.c:62`), and refusing headamp replay bases past the channel table (`src/reac_headamp_tx.c:81`). The control builder, inventory geometry and slave transport behaviors discussed below were not newly introduced by this revision. A wider test suite is useful evidence of validation effort, not a substitute for hardware compatibility evidence. The full Linux suite was not run here.

## What each layer supplies

**[CODE] libreac** has the reusable byte-layout, control-building, identity, inventory and pure state-machine functions. The chosen subset compiled as C11 with the installed Windows MSVC compiler for the probe. This proves portability of those exact compilation units, not of every object in the library. In particular, its top-level Makefile also includes capture/socket helpers. A Windows integration should select portable units explicitly instead of treating the whole archive as automatically platform-neutral.

**[CODE] libreac-transport** currently implements AF_PACKET sockets, POSIX threads, real-time scheduling, interface handling, rings, and orchestration. Its opaque public handles are useful separation boundaries, but the backend is Linux-specific. `transport/src/reac_slave.c` includes `recv`, `sendto`, `sockaddr_ll`, `mlockall`, `pthread_create`, and packet socket options. [Transport source](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/transport/src/reac_slave.c#L874).

**[CODE] reac-pw** binds these components to PipeWire. It supplies useful examples of channel exposure, graph/wire clock separation, buffering and node lifecycle. PipeWire nodes are not ASIO channels, and the existence of both nodes does not implement a Windows backend.

**[PROJECT] reac-protocol and reac-docs** combine measured observations, firmware interpretation, rules encoded in schemas, hypotheses, and older prose. They must be read alongside source and captures. Several contradictions remain explicit below.

The four other user-linked projects remain complementary rather than substitutes: per-gron's historical OS X driver for early split/control behavior; norihiro's OBS source and reaccapture for receive/sample-layout cross-checks; ManuKapel's prototype for host integration. Their review belongs with the separate legacy/public-project assessment. This report does not claim fresh complete audits of their code.

## Wire and audio model

**[CODE]** Untagged Ethernet has destination/source MACs at 0/6, EtherType `0x8819` at 12, little-endian counter at 14, type at 16, a 32-byte control block at 18, audio at 50, and `C2 EA` at the end. Audio has 24-bit channel-pair braiding; it is not simple sample-major PCM. Both encode and decode use the common braid helper. [Wire constants](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/include/reac/reac.h#L19), [braid implementation](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/include/reac/reac_braid.h).

For the twelve-sample format, clean frame size is `52 + 36 * channels`: 340/628/916/1204/1492 bytes correspond to 8/16/24/32/40 channels. FCS, VLAN tags and capture defects must be handled at ingest with provenance, not silently treated as audio fields. Source MAC, role, destination and session identify a stream; length alone cannot identify a console or stagebox.

**[CODE]** The generic downstream decoder still demands 1492 bytes (`src/reac_decode.c:18`). The ordinary downstream RX gate also requires broadcast and 1492 bytes (`transport/src/reac_rx.c:160-168`). Thus the handoff's shorter M-400-originated frames need explicit analysis; they are not accepted by that normal downstream path. If reproducing the handoff's reported width changes, record whether they represent idle/unestablished state or useful routed audio. Do not automatically feed a short master packet into an upstream identity policy simply because the upstream decoder can decode its geometry.

**[CODE/PROJECT]** Rate class is carried in the master announcement's control byte 17: 0=48 kHz, 1=96 kHz, 2=44.1 kHz. The scene revision and some enrollment/slot-map carriers repeat that class. Cadence is an additional observation, not a replacement for reading the field. With 12 samples/packet, the three nominal rates imply 4000/8000/3675 packets per second. Whether a particular device supports a rate is a device question, not inferred from the generic decoder.

## Declaration geometry and the 40/0 experiment

**[CODE]** `build_config` checks nonnegative port counts, divisibility by four, and total cell count at most twelve. `reac_box_model_upstream_width` separately validates each direction's width, then uses the declared input width for upstream audio (minimum two for an output-only profile). [Builder](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/src/reac_box_synth.c#L94), [width helper](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/src/reac_box_synth.c#L264).

**[MEASURED-OFFLINE]** The exact source returned:

| Declared input/output | Declaration result | Upstream width helper |
|---|---:|---:|
| 40/0 | success | 40 |
| 24/24 | success | 24 |
| 40/8 | success | 40 |
| 40/40 | rejected | 40 |

The last row matters: an individually valid upstream width does not imply the inventory declaration is valid. The probe copied the existing fr4000 row and changed only the two counts. It did not produce or install new profiles.

**[CODE]** `fr4000` is explicitly a derived experimental profile, not a captured Roland product. Its name is `FR-4000`, selector `0x84`, headamp strap zero, 40 inputs and zero outputs. [Catalogue](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/src/reac_ctrlblk.c#L635).

The box's input means DAW playback toward the console. Its output count describes declared stagebox outputs, not an inherent limit on sniffing the console's downstream slots.

**[CODE] Correction of handoff:** `reac-pw/src/main.c:2392` sizes playback from `in_ch`. At `main.c:2534`, capture is passed `out_ch`, which is zero for fr4000. But `reac_source_node_ensure:737-738` normalizes nonpositive width to 40, and `reac_source_node_new:354-355` does the same. The header explicitly documents this fallback. The handoff-pinned `147c751...` source contains the same code, verified with `git show`; this is an earlier analysis error, not a new upstream fix. [Call site](https://github.com/FreeREAC/reac-pw/blob/76aac5db50eedda3b07662bc97eb10babdd90c75/src/main.c#L2534), [constructor](https://github.com/FreeREAC/reac-pw/blob/76aac5db50eedda3b07662bc97eb10babdd90c75/src/reac_source_node.c#L353), [ensure](https://github.com/FreeREAC/reac-pw/blob/76aac5db50eedda3b07662bc97eb10babdd90c75/src/reac_source_node.c#L720).

**[OPEN]** A 40/0 profile therefore remains a plausible experiment, with more favorable source-level node geometry than the handoff claimed. Required evidence is still M-400 acceptance, 40 independently routed upstream inputs, all desired downstream slots present, uninterrupted PCM across control frames, clock stability and recovery. Node creation alone proves none of these.

**[VENDOR/OPEN] Routing caveat:** the M-400 manual describes unavailable patch jacks for the connected I/O unit (printed page116) while REAC B has up to40 output-patch slots (pages118/182). Thus host capture width40 does not prove that a declaration with zero physical outputs leaves forty independently assignable console outputs available. A40/8 declaration may likewise expose only eight freely assignable output jacks. The decisive test is the console's actual patch availability and forty distinguishable downstream signals after enrollment. [M-400 v1.5 manual](https://static.roland.com/assets/media/pdf/m400_manual_v15.pdf).

## Session lifecycle: useful reference and limits

**[PROJECT]** Captured hardware sequences describe initial presence, scene/configuration exchange, a commit report, join/ready records, master responses/headamp replay and sustained audio/heartbeat. The order is model/state dependent. A warm return can omit a cold flood. A port's role switch, endpoint inventory, session state and audio rate are distinct properties.

**[CODE]** The pure slave FSM has PHY_DOWN, FLOOD_ANNOUNCE, COLDCONNECT, TX_MUTE, ESTABLISHED, DROP and BACKOFF. The current initial flood threshold is5460 frames; transition also requires a learned master, so this is not an unconditional stop after5460 transmissions on a silent segment. Ungranted courtship is bounded to four seconds, followed by ten seconds silent backoff; those two durations derive their step budgets from `heartbeat_period`. Join retry, post-grant ACK and settle still use fixed frame counts800,7200,and800. [FSM definitions](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/include/reac/reac_fsm.h#L35), [transitions](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/src/reac_fsm.c#L102).

| Current software state | Transition condition | Returned action/next state |
|---|---|---|
| PHY_DOWN | PHY up | Start broadcast presence; FLOOD_ANNOUNCE |
| FLOOD_ANNOUNCE | Threshold reached and master known, or early GRANT category | COLDCONNECT; begin unicast control/audio sequence |
| COLDCONNECT | GRANT category received | Arm bounded ACK window; continue sequence |
| COLDCONNECT | ACK window expires | TX_MUTE settle |
| COLDCONNECT | Ungranted budget exhausted | BACKOFF, emit nothing |
| BACKOFF | Budget expires | Re-enter FLOOD_ANNOUNCE |
| TX_MUTE | Dwell expires | ESTABLISHED audio/heartbeat |
| ESTABLISHED | Peer-gone countdown or detected master MAC change | DROP |
| DROP | Next tick/RX/PHY-up | Clear learned master and re-enter flood |
| Any | PHY down | PHY_DOWN, stop |

This table describes implementation, including its broad GRANT category. It is not a fully validated M-400 state specification.

**[INFERENCE]** Fixed 800 frames means 100 ms at 96 kHz but 200 ms at 48 kHz and about 218 ms at 44.1 kHz. The header calls the settle value a placeholder, and warns about staying inside a roughly 150 ms grant window. That tension must be measured on the M-400 rather than copying the constants as universal protocol timings. The live loop advances timeout events on a 5 ms receive timeout, so no-master wall-clock behavior is also not equivalent to a nominal frame-period tick.

**[CODE]** The grammar separates link/class, fragment bits, record length and subtype. `0103` is not a single magic opcode, and `0013` is not synonymous with readiness. Explicit tags include JOIN `0100`, HEAD_MARK `0000`, HEADAMP `0101`, BOX_READY `0302`, IDENTITY `0500` (`include/reac/reac_ctrlblk.h:112-116`). An accurate timeline should retain full address, direction and record bytes.

**[CODE/MEASURED-OFFLINE]** `reac_ctrl_parse` classifies any SINGLE link-4 record that is not recognized headamp as GRANT (`src/reac_ctrlblk.c:308-333`). It does not establish that this is a well-formed join grant. The slave FSM's COLDCONNECT branch arms its ACK window from that broad kind (`src/reac_fsm.c:155-164`). The live slave receive loop does not run the stricter master-side classifier before invoking its FSM. [Parser](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/src/reac_ctrlblk.c#L308).

The handoff's anomalous block is:

`04030013000200fe0ff0410a000012120000030000007df70000000000000001`

It has a valid outer checksum, record length19 versus SysEx length+5 of20, and full DT1 address `00000300`. This matches the head-mark address/payload shape, not box-ready `03020001`. In the probe, these block bytes were placed in a synthetic frame from the FSM's chosen master and fed directly as RX to a COLDCONNECT state. The exact parser returned GRANT and the exact FSM armed `grant_ack=7199` after one step. This demonstrates why a strict source/destination and semantic validation gate must precede a grant event. It does not establish that the actual S-originated captured record was a grant, that the user's hardware accepted it, that the capture was generated by FreeREAC, or that the physical session reached ESTABLISHED.

## Scene transfers

**[CODE/PROJECT]** The implemented complete scene body is 8904 bytes: 24 FIRST + 341*26 MIDDLE + 14 LAST, 343 control records. FIRST body data starts at block+7; MIDDLE/LAST body at block+5. The LAST block has 12 more trailer bytes, which are not part of the declared body. Outer checksums apply per block. For this bulk scene family, record lengths count payload bytes, unlike the generic DT1 interpretation. [Scene builder](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/src/reac_ctrlblk.c#L76).

Body landmarks are `1234` at 0x000, `SYSP` at 0x368, `SCEN` at 0x37c, master MAC at 0x340, and little-endian revision/rate at 0x014. Firmware-derived commentary describes the tags and final commit as necessary for promoting staged state. These are excellent reconstruction checks; missing fragments must still remain missing, and unrelated sessions must not be spliced together. The upstream wire-format notes also report8748-byte initial box-master transfers despite a declared8904, with contiguous counters. That observation is not an M-400-specific explanation, but confirms why an incomplete or variant body must not be silently padded or discarded as capture loss. [Constants and provenance](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/include/reac/reac_ctrlblk.h#L125), [upstream scene observations](https://github.com/FreeREAC/reac-protocol/blob/12db4b6461b4674c983470b32ab47f7512c1fba7/wire-format.md#L354).

For a PC emulating a box, scene receive/reassembly, identity responses and correct commit signaling need to be distinguished from a master-side scene generator. Simply replaying a master's scene file does not implement a slave. Retain packet-index provenance, peer/session identity, complete/incomplete status, raw-body hash and decoded landmarks in research outputs.

## Timing and uninterrupted audio

**[CODE]** The ordinary slave engine is receive-driven: one master frame drives an FSM step and an immediate upstream response. It latches an offset between its counter and the master's counter, then tracks that offset. It does not use the master pacer's independent `clock_nanosleep`/ETF loop. The master pacer has a separate clock-discipline system and optional SO_TXTIME/ETF path. [Slave design](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/transport/src/reac_slave.c#L165), [receive and counter lock](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/transport/src/reac_slave.c#L927), [master pacing](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/transport/src/reac_pacer.c#L1594).

**[INFERENCE]** A Windows port cannot equate receive callback arrival times with clean wire timing. Packet batching can preserve total packet count while producing bursty replies. Accurate counter accounting, clock-rate estimation, bounded buffering, egress timing and physical loopback audio measurements are separate requirements. Whether Npcap and the actual NIC suffice must be tested; these sources prove neither that a kernel driver is mandatory nor that ordinary userspace is sufficient.

**[CODE/MEASURED-OFFLINE] Heartbeat continuity defect:** in the ordinary desk-facing slave path, `stage_inputs` consumes a 12-sample block (`reac_slave.c:290-312`), then the established branch calls `reac_ctrl_build_box_hb` when the heartbeat flag is set (`:843-853`). The builder calls `ctrl_emit` with no PCM (`reac_ctrlblk.c:1081-1086`); `CTRL_BOX_HB` has no `.audio=1` (`:996-998`); `ctrl_emit_as` zeroes the entire frame before optional audio encoding (`:941-949`). A native probe using nonzero PCM confirms nonzero filler audio but zero heartbeat audio at widths 8,16,24,32,40. Thus each such heartbeat slot discards its staged 12 samples. The separate box-master overlay path preserves an existing carrier; do not generalize the finding to every path.

**[INFERENCE]** A fresh endpoint should encode audio on every audio-bearing slot and overlay only the necessary control fields. Test a known continuous signal through heartbeat, join/identity and headamp frames. FIFO underrun handling must keep wire cadence and counters running. A DAW buffer size need not be divisible by twelve; preserve residual samples between packet and callback boundaries.

**[CODE]** Virtual headamp SENS/PAD updates currently multiply upstream samples by an inferred digital gain (`reac_slave.c:187-243`). This is a deliberate emulation policy, not neutral digital transport. For DAW returns, choose and document the expected gain behavior and headroom; otherwise a console headamp change can unexpectedly scale or clip playback. Phantom is represented as state, not electrical behavior.

## Contradictions and unsupported generalizations

| Source wording | Evidence and treatment |
|---|---|
| No rate field on the wire | `wire-format.md:287,339,409` contradicts its own master announce table at :205 and the scene/rate implementation. Use the explicit field and measured cadence. |
| Downstream always 40 channels | Generic decoder requires this, but handoff reports shorter console-origin frames. Determine state and stream; do not generalize a decoder precondition into universal hardware behavior. |
| Upstream fixed packet rate, samples per packet increase with rate | Older `reac-docs/findings.md:53` conflicts with current twelve-sample code/schema. Treat as superseded prose pending capture evidence for any exceptional device. |
| ANNOUNCE/LINKED always 8000 fps | `wire-format.md:375-376` is a rate-specific historical description; it is not a universal 48/44.1 kHz rule. |
| 20/20 widest symmetric box | Catalogue comment conflicts with native-tested builder acceptance of24/24. Both should remain separate from actual console acceptance. |
| S-4000D row is a physical0/32 output box | The row is synthetic/derived. Roland's S-4000D is a10-port REAC splitter/power distributor, not a32-output analogue stagebox. |

The last item is a vendor-verified distinction: [Roland S-4000D product page](https://proav.roland.com/global/products/s-4000d/). More generally, profile tokens must not be substituted for physical product specifications. Upstream marks derived rows for a reason.

## Reproducible local checks and limits

The bounded probe is at `research/tools/freereac_builder_probe.c`; `research/tools/run_freereac_builder_probe.cmd` compiles it against exact unmodified source with the installed Visual Studio18 Community x64 C11 compiler. Output is `research/results/freereac_probe/build_and_run.txt`. It includes the successful geometry, heartbeat-audio and anomalous-record/FSM checks described above. It runs no sockets, networking, DAW callbacks, installers or drivers. The script has an explicit local VS installation path and may need adjustment on another machine.

The test also demonstrates that these pure units compile on Windows: `reac_ctrlblk`, `reac_box_synth`, `reac_encode`, `reac_identity`, `reac_ports`, `reac_fsm`. It does not test the entire library, Linux transport, PipeWire, real-time scheduling, a host session or M-400 sound. The fr4000 source-node fallback was verified by reading current and pinned code, not by launching PipeWire.

All inspected FreeREAC core files carry GPL-3.0-or-later identifiers; their source and NOTICE/LICENSE files should remain part of any reuse record. Device names and captured behavior do not alter source provenance. No license or support inference is made for the separate older projects here.

## Recommended next discriminating milestone

First establish a trustworthy offline evidence set: reproduce user-capture geometry and checksums, reconstruct complete scene bodies, and produce a per-peer/session timeline that uses semantic DT1 tags rather than the broad GRANT label. Keep a known real-hardware baseline separate from any emulator-generated frames.

Then prepare a minimal Windows endpoint design with six independent pieces: packet/sample codec; validated session state machine; Windows Ethernet backend; master-clock-following scheduler; continuous bidirectional rings; ASIO host layer. Its first live milestone should be a single identifiable upstream signal plus validated downstream reception and complete session logs on a dedicated M-400 B connection. Only after acceptance and clock/continuity measurements should all40 distinguishable channels be exercised simultaneously.

Acceptance for the eventual40x40 claim requires forty distinguishable upstream inputs accepted by the M-400, forty distinguishable downstream channels captured by ASIO, one clock domain or an explicitly measured bridge, uninterrupted control-frame audio, sustained operation, and recovery from unplug/restart/rate changes. M-400's separate processing-strip/routing limits still apply. The source review leaves these hardware conditions OPEN.

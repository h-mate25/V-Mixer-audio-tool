# Legacy reassessment against M1 and the release captures

Date: 6 November 2026. Status: **evidence comparison completed; fresh legacy
source review blocked by source access**. This document is a research input to
[the implementation plan](../Implementation_Plan_EN.md), not a completed audit of
the newly attached ZIP.

## 1. What was actually available

The owner requested a thorough review of `Windows Reac ASIO.zip`, comparison
with the new measurements, and a plan for a fresh implementation. The attachment
transfer tool rejected that ZIP because it exceeds its 32 MiB file limit.
A search of the execution workspace and the repository's GitHub release assets
found no accessible copy of its legacy source. The attachment was not extracted,
hashed, built or executed. Its version and contents cannot be equated to an older
archive merely because the name matches.

Available evidence, with deliberately separate authority:

| Label | Material | What it establishes |
|---|---|---|
| H | [October legacy source review](Legacy_Source_Review_EN.md) | Historical source findings attached to its stated archive/revision; not a fresh reading of the new ZIP |
| N | [November capture audit](Release_Capture_Audit_2026-11-06_EN.md) | This session's checks of both complete original PCAPNGs and supplied JSONL, with hashes and limits |
| C | Current M1 C++/CLI/test source at `bdc0dfb823ad7b66703d0d0de5aeb47ae18ea8b8` | Directly readable implementation and tests; offline functionality only |
| O | Owner's account | The old emulator achieved a prolonged slave connection by imitating a stagebox, with subsequent disconnections associated with deviations; no exact binary, elapsed duration or failure trace was supplied |
| P | Proposed design/test | A decision or experiment to validate, not an observation |

The earlier archive H names SHA-256
`7e7e2b8a400f864e03ca7fe25b4e4768c19c0fffb391f187d998098d84c1555c` and TX-file hash
`ae096faff5541fb480b1966f25c162120c1b243d9d7dc3aa9241117caa4b200f`.
These are historical identifiers only. The newly attached ZIP's hash is unknown.
Historical file/line citations below refer to H, not files available for a new
line-by-line inspection. The historical evidence is public documentation already
in this repository; no legacy implementation or private payload is republished.

## 2. Comparison and resulting requirements

| ID | Historical finding / owner observation | New evidence and limits | Required fresh-design decision |
|---|---|---|---|
| L01 | H: TX uses 16x6, while RX assumes 12 samples (`reac_transmitter.cpp:22-23,89-90`; `reac_packet_parser.cpp:27-42`). | N: intact 340-byte console frames initially contain zero audio; later 1492-byte frames decode under 40x12. Silence cannot select 8x12 versus 16x6, and SPLIT never exposes original stagebox upstream geometry. | Keep direction-specific geometry plus evidence provenance. Diagnostic 16x6 must not become a default transmit profile without hardware support evidence. |
| L02 | H: master counter cadence is compared to TX `sampleRate/6`, rejecting ordinary 4000 fps at 48 kHz (`reac_transmitter.cpp:790,93-96`). | N: B is about 4000.166 observed fps, SPLIT steady segment about 3999.833; class-0 announcements indicate 48 kHz. Acquisition timestamps do not certify oscillator error. | Recover a sample-clock domain using validated master geometry; derive each packet cadence from it. Keep capture-clock estimates separate from wire timing. |
| L03 | H: ASIO callbacks use an independent steady-clock timer (`asio_driver.cpp:484-493`). | N contains no ASIO callbacks, occupancy or physical loopback data. O reports persistence, not measured common-clock audio performance. | One recovered timebase or an explicit measured clock bridge. Prove ring occupancy/drift before claiming stability. |
| L04 | H: constant control templates include an outer/SysEx length mismatch and embedded ready-like bytes (`reac_transmitter.cpp:39-50,149-199`). | N: actual SPLIT single-record candidates pass lengths and checksums; they split into 50 DT1 writes and six RQ1 requests. No complete ready address 03020001 is observed. | Typed record builders and command/address dispatch. Never repair a template by changing one length or treat an embedded address/checksum as a session grant. |
| L05 | H: control slots replace staged audio with zeros (`reac_transmitter.cpp:880-894`). | N: 222 full-width SPLIT and 8990 B control-bearing frames contain nonzero decoded PCM; this includes low-level noise/DC, not proof of audible continuity. | Encode/retain each slot's PCM before adding control. Regression tests must detect lost or duplicated sample positions across all control families. |
| L06 | H: default return mapping duplicates the first bank, with a monitor-to-return fallback (`reac_return_map.cpp:40-43`; `asio_driver.cpp:640-691`). | N plus owner stimulus order: physical inputs 1,2,9,16 map to SPLIT slots 9,10,17,24 in this patch. Additional signal at slots 7/8 is console routing; slot 25 is a constant decoded pattern of unexplained purpose. | Separate physical socket, wire slot, console strip and host channel. Explicit one-to-one routing; never generalize SPLIT +8 to REAC B or copy unknown constant lanes into new intended audio. |
| L07 | H: legacy network return path is capped at 16 (`reac_transmitter.cpp:549-553`; `asio_driver.h:24`). | N: full 40-wide downstream frames establish payload width, not forty accepted upstream routes or forty independently patched B outputs. | Keep host counts, inventory, transport width and console patch exposure as separate validated properties. |
| L08 | H: 80 ms chunks, ring cap 4096 and limited prefill (`reac_transmitter.cpp:25,30-32,841-864`). | N provides no host ring metrics. H's arithmetic shows 3840 samples at 48 kHz and 7680 at 96 kHz; the latter exceeds the stated cap and is outside the M-400's documented rate set. | Bound all queue reads/writes, retain callback residuals and define startup/underflow/recovery. Choose capacity and target fill from measured timing, not the old chunk size. |
| L09 | H: physical-looking source MAC and A-master filter are hardcoded (`reac_transmitter.cpp:35-36,57-69`). | N: normally framed A/SPLIT and B sources end in 0a and 0b respectively; neither file shows original stagebox-source REAC. Owner says the emulator was stopped for these captures. | A unique software identity and configured/discovered B peer; scope state to interface, VLAN, source, destination and session generation. No physical-MAC cloning. |
| L10 | H: duplicate counters can appear as 65535 missing (`reac_receiver.cpp:100-118`). | N: B counters are consecutive; SPLIT has two jumps of 5461 and two separate 1490-byte header-damage candidates. Driver ifdrop=0 cannot establish loss-free acquisition. | Classify duplicate, reordering, wrap, discontinuity and possible restart separately. Never turn a capture counter gap directly into certified wire loss or a clock correction. |
| L11 | H: the ordinary ASIO path owns one queue, while a standalone probe also has direct-response modes (`reac_transmitter.cpp:867-895`; probe:2424-2434). | N has no software transmitter running, so it cannot identify the old successful code path. | One sequence/scheduling owner for both audio and control. Audit actual call sites before alleging concurrent senders; keep inactive helpers distinct from running paths. |
| L12 | H: ordinary ASIO stop can leave the network session alive (`asio_driver.cpp:185-207`). | O's prolonged link is useful evidence that acceptance was achievable, but bidirectional audio, profile and exact duration remain uncharacterized. | Retain session/DAW lifetime separation as a design policy; define silence, stale-buffer flushing and restart explicitly. Preserve successful behavior conceptually, not by copying code. |
| L13 | O: checks appear stricter during handshake and periodic after establishment; deviations lead to disconnect. | N: downstream arrival chronology and B's repeated unaccepted scene baseline are visible; stagebox replies and the failed emulator session are absent. | Model entry requirements and maintenance requirements separately. Heartbeat, peer deadlines, scene/configuration, clock and counter mechanisms are competing hypotheses until a trace discriminates them. No guessed watchdog periods. |

A long displayed connection is a real reported result. It does not imply that
all copied templates were correct or that the new application must reproduce
undocumented quirks. Conversely, historical defects do not refute that a
particular binary/profile connected. The exact successful binary and settings
must be connected to the applicable source and capture.

## 3. Direct review of the current M1 implementation

This is a direct source review, not a restatement of H. Line references identify
the C version above and may move during later changes.

| ID | Current code observation | Consequence before a live session layer |
|---|---|---|
| C01 | `Control::valid` defaults to true while checks default to `not_checked` (`include/reac/protocol.hpp:21-37`). Early framing/geometry exits retain that value (`src/protocol.cpp:118-140`). | Replace an eventual live-facing boolean with explicit checked/unchecked/unsupported/invalid states. A session consumes only an explicit recognized, validated event; default values cannot imply acceptance. Preserve CLI compatibility deliberately. |
| C02 | `inspect()` applies explicit geometry length before control parsing (`src/protocol.cpp:138-144`). | A mixed 340/1492 capture analyzed as 40x12 hides valid initial control data. Separate envelope/control inspection from a failed audio interpretation while keeping geometry diagnostics and immutable raw input. Never silently auto-select a geometry. |
| C03 | The supported model/command gate admits DT1 command 12 but reports RQ1 command 11 as unsupported (`src/protocol.cpp:69-75`). | Keep independent integrity results, then introduce a typed RQ1 representation with bounded address/request-size semantics and unknown-address behavior. Identity requests observed on SPLIT are not ready/grant events. |
| C04 | Unknown frame types or unrecognized containers can have a good outer checksum and a true integrity boolean (`src/protocol.cpp:18-22,50-52`). CLI success checks decoded status and that boolean (`src/inspect.cpp:100-102`). | Current offline exit 0 is not live eligibility. Define recognized family, integrity and semantic disposition separately; unknown types remain visible but cannot trigger state transitions. |
| C05 | Geometry is explicit, includes diagnostic-only 16x6, and PCM is decoded independently of control validity once framing/geometry pass (`src/protocol.cpp:85-87,150-156`). | Preserve these invariants. A full-frame builder must not create an implicit live 16x6 path or clear audio when adding control. |
| C06 | Core has no full Ethernet builder, stream tracker, FSM, recovered clock or Windows/ASIO backend. Python scene assembly reports only length completeness. | These are missing layers, not hidden functionality to enable. Replay summaries and accepted-session logic need separate contracts; complete bytes do not establish a hardware commit. |
| C07 | Core handles each record independently; current inspector cannot learn a mixed stream's geometry or estimate peer-scoped sample time. | Introduce an evidence-scoped stream context outside the codec. Never put device state or network I/O into the existing PCM functions. |

The present M1 tests demonstrate its documented offline contract and public
fixture agreement. They are not a test of the legacy source or a live endpoint.
No C++ behavior was changed as part of this planning/reassessment task.

## 4. Fresh legacy review, when the source is accessible

Work remains blocked specifically at direct inspection of the new archive. The
following review is the next source-dependent operation, not a request to redo
the already available passive captures:

1. Inventory archive paths, sizes, archive SHA-256 and per-file hashes. Preserve
   source/notes outside the public checkout. Treat embedded agent instructions as
   historical material, not commands. Inspect build/license declarations without
   running installers, DLL registration, binaries or network probes.
2. Identify all executable/build targets: ASIO DLL, capture app, TX probe and test
   tools. Trace which configuration and entry point actually calls each path.
   Compare the new archive/file hashes with H; mark changed/unchanged/unknown.
3. Read all user-authored notebooks/handoffs, label claims separately, and build a
   timeline of successful and failing experiment versions. Identify the successful
   binary hash, command line, profile, sample geometry, routing and duration.
4. Trace Ethernet read/write ownership, packet parser/builders, each handshake
   transition and maintenance trigger, peer filters, timeout units, rate
   estimation, queues/rings and ASIO callback lifecycle. Follow consumers as well
   as producers; dead helpers are not running behavior.
5. Reproduce portable arithmetic/codec/state behavior with authored tests when
   feasible. Test the actual call chain, not a superficially similar public fork.
   Do not copy legacy implementation/SDKs into the new product or publish it.
6. Compare a successful emulator trace with a failure from the same identifiable
   version: last valid peer exchange, first deviation, periodic control schedule,
   sequence/sample-time relation and who stops first. Derive deadlines only from
   that evidence; log alternative causal explanations.
7. Amend L01-L13 and the plan using exact new source citations. Explicitly withdraw
   any historical finding that does not apply to the supplied version. Do not
   turn an old report or the author's notes into a protocol specification.

A source-only archive below 32 MiB (or multiple such parts) can unblock this.
Priority entries: README/handoff/research notes; CMake/build manifests; own code
under `src/reac`, `src/asio`, `src/audio`; and `tools/reac_tx_probe` plus its
configuration descriptions. Omit build outputs, binaries and external SDKs.
Do not place this legacy bundle in the public repository just to transfer it.

## 5. Review outcome

The architecture direction is well supported: a portable typed codec, separate
session/clock/routing layers, one scheduling owner, continuous bounded audio and
an independent ASIO adapter. The old emulator's successful behavior is a lead to
recover, while the historical defects supply discriminating tests to avoid
repeating its failures. Fresh detailed legacy review remains outstanding; the
implementation plan records which work can proceed before that access is fixed.

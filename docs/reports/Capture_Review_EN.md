# REAC capture study: expanded corpus and independently reproduced evidence

Analysis started 25 October 2026; completed/reverified 26 October 2026, Europe/Budapest. Scope is offline analysis of the supplied ZIP. No NIC was opened, no packet transmitted, no driver installed, and no hardware session tested.

Evidence labels: [MEASURED-OFFLINE] bytes/counters observed in the supplied files; [CODE] inspected implementation; [PROJECT] upstream documented observation; [INFERENCE] interpretation; [OPEN] unresolved.

## 1. Available corpus and integrity

[MEASURED-OFFLINE] `C:\Users\Máté\Desktop\ReacCaptures.zip` contains **58 PCAPNG captures**, not merely the fourteen available to the preceding analyst. All three formerly missing captures (`meres48-48khz`, `meres48-96khz`, `meres49`) are present, as are `meres48-44.1khz` and the earlier experiments. Capture bytes were streamed directly from the ZIP; the originals were not modified or duplicated into a 10 GB extraction tree.

The source ZIP is 1,361,359,878 bytes, SHA-256 `5fc7978aeb9e1d0f118f8c754faa00bd43d2f66ff238813b7998e3b65eadd1e9`.

Every capture was read to EOF. PCAPNG section byte order, block/footer lengths, interface link type, timestamp resolution, packet stored lengths and Ethernet/VLAN positions were checked. There are 58 unique basenames and 58 unique stems, so the output naming convention has no collisions in this ZIP. Decompression to EOF also invokes ZIP CRC verification. Per-file SHA-256, ZIP member name/CRC, byte count and record totals are in capture_manifest.json (local artifact; excluded from this documentation upload).

| Check | Entire supplied corpus |
|---|---:|
| Raw PCAPNG bytes | 10,116,560,844 |
| Packet records | 14,630,983 |
| Normally framed EtherType 0x8819 records | 14,626,212 |
| Captured length below stored original length | 0 |
| REAC records with missing C2 EA tail | 0 |
| Observed VLAN-tagged REAC records | 0 |
| Non-filler outer checksums passed / failed | 111,404 / 0 |
| Complete SINGLE DT1 inner checksums passed / failed | 8,627 / 0 |
| SINGLE DT1 length inconsistencies | 802 |
| Two-byte Ethernet-header defect candidates | 425 |

This validates the stored containers and the stated checksum predicates. It **does not establish loss-free acquisition, physical source identity, correct timing, semantic control validity, or successful device enrolment**. Capture timestamps are arrival metadata and may be batched.

The previous fourteen-file subset reproduces exactly: **1,442,733 records; 1,442,426 normal REAC records; 8,683 non-filler checksums; 764 SINGLE DT1 records; 32 length inconsistencies; 29 damaged-header candidates**. The handoff's aggregate observations therefore correspond to these same files.

Machine-readable overview: capture_summary.csv (local artifact; excluded from this documentation upload), control_summary.json (local artifact; excluded from this documentation upload). Per-capture JSON holds interfaces, source/destination counts, full control-block census, counter deltas, width changes, longest contiguous-sequence runs and scene transactions. The `events` directory contains all non-filler controls and width changes with packet indexes.

## 2. Source identity and expanded scope

[MEASURED-OFFLINE] Most REAC traffic uses M=`00:40:ab:c4:9c:0a` or S=`00:40:ab:c4:7c:09`. The expanded corpus also has `90:1b:0e:02:ae:49`: 79,395 REAC frames in `meres11` and 80,000 in `meres12`, all 340 bytes. A MAC is a stream identifier, not proof of a physical console or stagebox; the supplied emulator can choose its source MAC.

The old statement “no full-width S stream in this corpus” applied only to the fourteen-file subset. **`meres45.pcapng` contains 163,753 S frames of 1492 bytes**, alongside M's short/full streams. Its S control traffic still repeats the **16-input/8-output** inventory, 32 times. Thus the expanded corpus contains full-sized upstream payloads but does **not** supply a demonstrated forty-input inventory or forty distinct accepted return channels. The literal byte geometry alone cannot certify channel count without a samples-per-packet hypothesis.

## 3. Important correction: 340 bytes and 8000 packets/s do not prove 8 channels at 96 kHz

[CODE] The newly supplied local Windows transmitter deliberately implements **16 channels × 6 samples × 3 bytes = 288 audio bytes**, giving a 340-byte frame, and schedules `host_sample_rate / 6`. At a nominal 48 kHz that is 8000 packets/s. See the separate [legacy review](Legacy_Source_Review_EN.md), especially its geometry and clock sections. The previously assumed **8 channels × 12 samples** produces exactly the same byte count.

[MEASURED-OFFLINE] `meres50` has 330,581 S frames, all 340 bytes. The entire S audio stream was decoded under both hypotheses:

| Interpretation | Signal-bearing slots | RMS adjacent-sample difference / RMS, slots 1 and 2 |
|---|---|---|
| 16 channels × 6 samples | 1–2; slots 3–16 silent | 0.2681, 0.2180 |
| 8 channels × 12 samples | 1–2, with alternate sample vectors zero | 1.4142, 1.4142 |

Under the 16×6 view the second eight-channel bank is silent. Reinterpreting those bytes as 8×12 inserts that silent bank between successive first-bank samples. There are 227,959 nonzero S frames; none has the second eight-channel bank mirroring the first. The smoothness measurement includes capture gaps and source-counter discontinuities and is not a calibrated quality measure.

[INFERENCE] The source code plus waveform structure strongly support the **16×6 emulator-format hypothesis** for this experiment. They weaken the handoff's inferred “8 channels despite declaring 16” and “96 kHz while the master is 48 kHz” explanations. They do **not** prove that a genuine S-1608 uses six-sample packets, that the inspected build generated this capture, or that the M-400 accepted this geometry. Controlled source/version/topology provenance remains required.

[MEASURED-OFFLINE] Pairing consecutive S payloads into a 16×12 block and comparing them with every even-aligned sixteen-channel window in M's 49,413 full-width frames found **no exact non-silent match**. This narrowly negative comparison does not establish that no audio was received; repacketization, buffering, samples omitted/repeated, alignment and the physical capture path remain alternatives.

Full numerical results: meres50_audio.json (local artifact; excluded from this documentation upload). Nothing here validates the six-sample design as a hardware protocol rule.

## 4. Previously missing captures: filename rate is not console rate

[MEASURED-OFFLINE] The M-associated CFEA rate byte is:

| Capture | Packet records | M CFEA rate-class census | S frame bytes |
|---|---:|---|---:|
| meres48-44.1khz | 996,662 | 2: 83 announcements | 340 |
| meres48-48khz | 771,725 | 0: 68 announcements | 340 |
| meres48-96khz | 432,524 | **2: 32 announcements** | 340 |
| meres49 | 588,736 | 0: 47 announcements | 340 |
| meres50 | 457,668 | 0: 37 announcements | 340 |

[CODE] FreeREAC interprets class 0 as 48 kHz and class 2 as 44.1 kHz. **No CFEA class 1 occurs anywhere in the 58 captures**: the totals are 754 class-0 and 1,079 class-2 announcements. Therefore the `meres48-96khz` filename is not evidence that the M-400 announced 96 kHz. It could describe a host-side experiment; the user log is needed.

Long S sequence-contiguous runs in these files are near 8000 packets/s, with variable captured run rates and batching. That is compatible with the local 16×6/48k implementation, but also leaves a potential host-rate/console-rate discrepancy for class-2 captures. Packet cadence must be evaluated together with the actual sample layout and software version, not used as a physical ADC-rate meter.

Exact hashes for the three formerly missing files:

* `meres48-48khz.pcapng`: `6110ea3bdc44a00120a5ee75807e5204f855cd5f76a1a02f1abc1e79896d59ca`
* `meres48-96khz.pcapng`: `1db7e8cc7448e1ec8ebc8dd0a87d4fae469a72ecaf2c888c316777c222703e0e`
* `meres49.pcapng`: `8af6e4e8a04ccc048e819fd0384da1f748b2568bb79bc93c8382559da24fd3e5`

## 5. Exact meres47 forwarding result reproduced

[MEASURED-OFFLINE] `meres47.pcapng` SHA-256 is `970c132e421dd85ddc74b5e3fe53afcdbde5f8f1cc64b46b131b94f1941d8916`. With its 628-byte S frames interpreted as sixteen channels × twelve samples, there are 92,960 unique non-silent upstream blocks and 89,466 full-width M frames. All even-aligned sixteen-slot M windows were compared byte-for-byte.

**6,908 matches occur at downstream slots 9–24; zero at any other tested start slot.** Upstream packet **19,381** exactly equals that window in downstream packet **19,388**. The matched 576-byte audio block SHA-256 is `8a21d98d2cc2ae25cde2047817fcf5d641180ec13b080ba4eef887711d229289`.

Only S slots 1–4 contain nonzero samples; slots 5–16 are silent throughout the file. The evidence establishes nonzero wire-slot forwarding 1–4→9–12 plus whole-block byte equality; it does not independently calibrate the twelve silent physical sockets. It is compatible with an A/SPLIT composition, but the connector/tap location is unconfirmed and it does not validate the proposed independent B-port design.

The two original packet bytes and their capture/file hashes are registered in fixture manifest (local artifact; excluded from this documentation upload). Audio result (local artifact; excluded from this documentation upload) includes full comparison counts. Capture timestamp difference is not DAW round-trip latency.

## 6. Control signatures, lengths and ready semantics

[MEASURED-OFFLINE] The five recurring S blocks in meres47/50 match the local source's generated constants byte-for-byte. In order they are zero-filled CDEA, inventory, join-shaped DT1, the inconsistent 0x0013 record, and the heartbeat-shaped block. In the entire corpus, **802** complete SINGLE DT1 containers violate `outer_length = sysex_length + 5`; both checksums still pass. The problematic block is:

```
04030013000200fe0ff0410a000012120000030000007df70000000000000001
```

The outer length is 19, SysEx length 15, DT1 address `00000300`. Meres47 has 19 instances, first packet 18,555; meres50 has 13, first packet 98,188. Those are exactly the former subset's 32 cases. The local source and historical template ancestry provide a plausible explanation, but captured version provenance is still absent.

No valid complete SINGLE DT1 container with address prefix `0302` was found. This is **not equivalent to absence of those bytes**: the recurring block below embeds a ready-looking SysEx record inside a link-1/opcode-81 envelope:

```
01030001810200fe0ef0410a0000121203020001007af7000000000000000096
```

The outer grammar determines its classification. Searching for `03020001` anywhere in the packet or calling a variable `ready` does not demonstrate the expected ready transaction. This distinction is now supported by both source and captures.

The event files retain every control and its destination, counter, timestamp, checksum status and decoded SINGLE address. They are an evidence timeline, **not a fully recovered accepting-device state machine**. Repeated five-block bursts and M reconfiguration must not automatically be called successful reconnects.

## 7. Scene reconstruction: completed offline milestone, bounded interpretation

[MEASURED-OFFLINE] Link-1 bulk FIRST/MIDDLE/LAST records were concatenated by their declared lengths, source/interface and destination. Repeated chunks were **not deduplicated** and no bytes were padded or guessed. All incorporated non-filler records passed the outer checksum.

* **114 transactions are length-complete**: 24 + 341×26 + 14 = 8,904 bytes, matching the FIRST declaration.
* **129 transactions reach LAST with a length mismatch**: 125 contain 8,748 bytes / 335 middle chunks; four contain 8,722 bytes / 334 chunks.
* Ten transactions remain open at capture EOF.

The JSON status `complete` means **length-complete reassembly only**, not accepted configuration. The implementation does not fully validate every fragment/counter transition, device state, transaction restart, opaque body field or master acknowledgement. No “stable session” conclusion follows from that flag.

The 114 length-complete bodies have only two hashes:

| Count | SHA-256 | Byte 20 | Associated CFEA class |
|---:|---|---:|---:|
| 105 | `536f6978fd8a60edcb7b066aa6a798e14804c6701fded870c0a591ed0ce02c56` | 0 | 0 |
| 9 | `10b8d9831e00f37938a5fd959349959cd6a94557ff9eeea9ee9feaf55cf76563` | 2 | 2 |

They differ at exactly **offset 20 and nowhere else**. Both contain `1234` at offset 0, `SYSP` at 872, `SCEN` at 892 and M's MAC at 832. [CODE] This independently agrees with the current libreac scene layout's u16-LE rate field at offset 0x014; [source definition](https://github.com/FreeREAC/libreac/blob/9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5/include/reac/reac_ctrlblk.h#L176). It does not decode all other body fields.

Concrete meres47 example, same source M/interface 0/broadcast destination throughout:

| FIRST→LAST packet | Middle chunks | Bytes | Interpretation |
|---|---:|---:|---|
| 7,607→12,555 | 341 | 8,904 | length-complete, rate-2 body |
| 92,247→95,422 | 335 | 8,748 | repeated shorter body |
| 185,271→188,472 | 335 | 8,748 | identical shorter body |

The shorter bodies have hash `4505eb3056185026490ea1073056ac45ac9ed1ee47d9597d2d00a4aa8f7a04cf`. Alignment against the full body removes six 26-byte fragments (reference indexes 63–68, body bytes 1636–1791); repeated content makes the precise deletion position nonunique. At the chosen boundary, packets **92,373→92,374** have consecutive source counters **32,178→32,179**; the second short scene has packets **185,393→185,396**, counters **13,023→13,024**. Therefore “six packets were simply lost at that boundary” is not supported. Same-source stream mixing or incomplete grammar remains possible without topology/source provenance.

[PROJECT] FreeREAC already documents a similar 8,904-declared/8,748-delivered scene in a different, box-as-master setup with contiguous counters. This is relevant corroborating context, not proof that the user's experiment has the same cause: [upstream observation](https://github.com/FreeREAC/reac-protocol/blob/12db4b6461b4674c983470b32ab47f7512c1fba7/wire-format.md#L357). Do not “repair” these traces by padding 156 bytes or assume the complete-builder format describes every emitted transaction.

The two complete bodies are saved as hash-named fixtures. Scene control examples (local artifact; excluded from this documentation upload) preserve header/final/boundary control blocks, exact original indexes, source/destination and source-capture hash. `control_summary.json` records fragment comparison and selected discovery/configuration/control/width chronologies.

## 8. Header damage is wider than the original sample

[MEASURED-OFFLINE] The same missing-two-destination-byte signature occurs **425 times** across the expanded corpus. EtherType 8819 appears at raw offset 10 rather than 12; the frame is two bytes short and retains C2 EA. All 425 have filler frame type 0000 under the diagnostic shifted interpretation. None was silently inserted into normal REAC/audio/control statistics. Their original record indexes and prefixes are retained; the first case per file is saved as an unchanged byte fixture.

The prior fourteen-file counts still match 9/2/18 for meres47 / its console-only variant / meres50. The defect's origin remains unknown; valid container lengths and zero capture-interface drop reports cannot exclude upstream capture bugs or losses.

## 9. Reproduction, verification and limitations

Run with Python 3.12 or compatible. The census/control analysis uses only the standard library; audio decoding uses NumPy. Example PowerShell (replace the interpreter path if needed):

```powershell
$reacPython = 'C:\Users\Máté\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $reacPython D:\M400-DAW\research\capture_study\analyze_zip.py 'C:\Users\Máté\Desktop\ReacCaptures.zip' D:\M400-DAW\research\capture_study\results
& $reacPython D:\M400-DAW\research\capture_study\deepen_audio.py 'C:\Users\Máté\Desktop\ReacCaptures.zip' meres47.pcapng D:\M400-DAW\research\capture_study\audio_results\meres47_audio.json
& $reacPython D:\M400-DAW\research\capture_study\deepen_audio.py 'C:\Users\Máté\Desktop\ReacCaptures.zip' meres50.pcapng D:\M400-DAW\research\capture_study\audio_results\meres50_audio.json
& $reacPython D:\M400-DAW\research\capture_study\deepen_control.py D:\M400-DAW\research\capture_study
& $reacPython D:\M400-DAW\research\capture_study\verify_study.py
```

Six focused regression checks pass: little/big-endian multiple PCAPNG sections and timestamp resolution; footer/truncation rejection; exact prior-fourteen census; every registered raw-packet fixture hash/length; exact forwarding fixture equality; complete-scene hashes, tags and single-byte rate difference. The full 58-file census was completed once; selected priority files and the audio studies were repeated independently. The `priority_results` folder is an intermediate duplicate subset; `results` is the authoritative complete census.

Remaining limits: no calibrated physical channel map; no user-proven capture connector or topology; no captured executable/build identity; no complete scene/body or accepting-device state-machine specification; no demonstrated forty-input slave inventory; no forty distinguishable returns validated by M-400; no ASIO/FL Studio timing, drift, round-trip latency or recovery measurement. Those remain hardware/host milestones.

The next discriminating experiment is a labelled, known-good hardware-only M-400/S-1608 capture at a documented rate, followed by a separately labelled emulator run with known binary hash and distinct per-channel tones. Specifically compare 340-byte physical and emulated streams for sample count, filler descriptor, counter cadence and waveform continuity before expanding return width. Existing raw captures already support useful offline regression work; they should not be replaced by guessed “corrected” packets.

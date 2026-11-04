# Continuation audit: 4 November 2026

The October source review and 58-capture census are preserved at their original revisions. This continuation checks current source trees, independently verifies published raw-byte examples and tightens interpretation of the saved user-control events. It does not exercise hardware, a network adapter, ASIO or FL Studio.

## 1. Remote revisions checked today

| Repository | October reviewed tree | Remote HEAD observed 4 November |
|---|---|---|
| FreeREAC/libreac | `9277a1fb3c550e9fcc3a8dda0a9cd2573b4054e5` | `cfb43f91b7e4a7238c4feec07d4b8de1ed5a6139` |
| FreeREAC/reac-pw | `76aac5db50eedda3b07662bc97eb10babdd90c75` | `f86a30a3d06e84e8a7d6ec647cae77c4cbc9137b` |
| FreeREAC/reac-protocol | `12db4b6461b4674c983470b32ab47f7512c1fba7` | `81466f2c86d7e27e66ddabf82e31ba1b8f8a8335` |
| FreeREAC/reac-docs | `79d4f4f54e6c61634827d7b938581f195a3166dc` | `310aa8e3df4cef1554973cb25d51d876c0ff1bdd` |

The four other user-linked repositories have the same observed HEADs as the October pins. The existing checkouts have no tracked or untracked changes and remain on the October commits. Exact new objects were fetched and selected text source snapshots archived separately. Complete tree diffs, per-file hashes and source-refresh metadata are saved under `results/source_refresh_2026-11-04`.

The old reac-protocol commit is **not an ancestor** of today's observed HEAD. Therefore its comparison is a tree-to-tree comparison; the intervening `git log old..new` is not presented as a simple chronological series of new changes. The pinned original remains available. No claim about the reason for the history difference is made.

## 2. Which earlier findings survive the source refresh?

[MEASURED-OFFLINE] Byte-for-byte comparison shows eight relevant implementation units unchanged:

- libreac `reac_ctrlblk.c`, `reac_box_synth.c`, `reac_encode.c`, `reac_identity.c`, `reac_ports.c`, `reac_fsm.c`;
- libreac `transport/src/reac_slave.c`;
- reac-pw `src/reac_source_node.c`.

Their hashes are recorded in `independent_source_checks_2026-11-04.json`. Thus the October builder, broad GRANT classification, ordinary slave-heartbeat audio clearing and capture-node zero-to-forty fallback findings still apply to these exact implementations. They were not inferred from commit messages. The native probe was originally compiled against the October tree; it was not rerun as a new full-stack test. The changed `main.c` still passes the declared box output count to `reac_source_node_ensure` (new lines 2574-2589), so the inspected fallback call chain remains applicable.

libreac's relevant new production change is a Linux rtnetlink qdisc dump-walker bound check. Its loop now checks `off + sizeof(header) <= len`, avoiding unsigned subtraction after an aligned offset passes the received byte count. Version declarations now identify 1.10.0; pace/role declarations were consolidated into libreac in 1.9.0. These changes improve the Linux transport/dependency boundary; they do not implement a Windows backend or fix the unrelated heartbeat path. [Pinned change](https://github.com/FreeREAC/libreac/blob/cfb43f91b7e4a7238c4feec07d4b8de1ed5a6139/transport/src/reac_etf_qdisc.c#L76).

## 3. New upstream discovery behavior and supported configurations

[CODE/PROJECT] reac-pw has materially changed its empty-wire master discovery. It now sends a bounded control-only knock: scene chunks, map and announcements, leaving filler slots empty. A responding box causes transition to the full master stream; unanswered knocks back off. Host NIC ownership is checked before link-cycling actions, and mixed native-VLAN/trunk configurations are explicitly limited. This is Linux master-side discovery of a physical stagebox, not the desk-facing slave handshake needed for the proposed PC on M-400 B.

[PROJECT] The README now reports rig validation on 27 October for two deployment shapes: one box on an untagged NIC/access connection, or one box per tagged VLAN on a trunk. A box on the trunk's untagged native VLAN is not served. A cold box whose VLAN has never been heard still needs that VLAN sub-interface to exist in host network configuration. The project reports 75 control-knock answers across S-1608 and S-4000S-3212 direct/VLAN runs. Those are the project's tests; this research did not reproduce them or turn them into an M-400 Windows compatibility claim. [Supported configurations](https://github.com/FreeREAC/reac-pw/blob/f86a30a3d06e84e8a7d6ec647cae77c4cbc9137b/README.md#L194), [knock evidence and limits](https://github.com/FreeREAC/reac-pw/blob/f86a30a3d06e84e8a7d6ec647cae77c4cbc9137b/docs/KNOCK.md).

Some older wording remains contradictory: README lines 123-127 say a desk is joined as slave, while its later automatic-role description says a desk is tapped. Lines 265-266 still say the rate is never on the wire, despite explicit rate fields in the implementation and grammar. Read the specific path and bytes; do not treat a README paragraph as a complete session specification.

reac-docs now explicitly calls its AES67 bridge **in preparation, not yet public**. That resolves the previously ambiguous missing public repository link; no ready-to-use bridge is established by that statement. [Pinned overview](https://github.com/FreeREAC/reac-docs/blob/310aa8e3df4cef1554973cb25d51d876c0ff1bdd/README.md).

## 4. Updated protocol grammar: useful corrections and remaining limits

The current tree comparison changes the grammar and real-frame checks in several useful ways:

1. **Trailing capture residue.** It now accounts for bytes after a geometry-positioned C2 EA marker as capture residue. Upstream documents an AX88179 USB path delivering 1494-byte captures of 1492-byte frames on a direct cable. Thus residue cannot be diagnosed solely from switch/mirror topology. It remains capture-path data; it must not be emitted as REAC payload. The user's two-byte *missing header* defect is a different condition. The new grammar's residue field is remainder-based; production ingest should still explicitly document the residue lengths and CRC/context it accepts rather than assume arbitrary appended bytes are a valid frame.
2. **All-zero CFEA control blocks exist in an upstream M-5000 example.** The type word can identify an announcement even when its control bytes are all zero. Reading byte 17 from that block would yield zero, but does not independently establish a 48 kHz rate. An empty/unrecognized carrier needs an unknown-rate state and other valid evidence.
3. **Filler descriptors are not universal fixed stagebox words.** Upstream's newer corpus observations relate repeated descriptor words to prior control checksums, with documented persistence beyond the most recent chunk in some console streams. Treat them as contextual observations, not a single magic constant for every product/state. This continuation did not rerun that entire descriptor census.
4. **The DT1 checksum is now explicitly seven-bit in the grammar.** This agrees with the existing modulo-128 implementation and the earlier correction in the notebook.

[Pinned grammar](https://github.com/FreeREAC/reac-protocol/blob/81466f2c86d7e27e66ddabf82e31ba1b8f8a8335/spec/reac.ksy), [facts](https://github.com/FreeREAC/reac-protocol/blob/81466f2c86d7e27e66ddabf82e31ba1b8f8a8335/spec/protocol-facts.yaml).

The generic grammar still states a twelve-sample downstream format. That remains a reference assumption, not a reason to reject the user's observed shorter M-associated control-bearing packets or to reinterpret every six-sample emulator packet as a hardware format. Device, direction, session state and sample count remain explicit evidence dimensions.

## 5. Independent checks of published raw bytes

[MEASURED-OFFLINE] `tools/independent_source_checks.py` reads the current pinned public `real_frames.json` and uses its own byte indexing and signed-integer arithmetic. It checked:

- **17 whole published frames:** declared geometry, marker, MAC, little-endian counter and independently decoded channel-major PCM SHA-256 all match the published reference values;
- **61 control windows**, including **49 non-filler outer checksums**;
- **18 complete DT1 windows:** terminator, seven-bit checksum and outer-versus-SysEx length relationship;
- the published all-zero M-5000 CFEA example.

This reproduces arithmetic against public byte fixtures. It does not independently authenticate their physical origin, load their original capture files, generate/run the Kaitai parser, or reproduce the project's entire test suite. The distinction matters: matching two software decoders is useful, but not physical channel calibration. [Pinned fixture source](https://github.com/FreeREAC/reac-protocol/blob/81466f2c86d7e27e66ddabf82e31ba1b8f8a8335/spec/fixtures/real_frames.json).

## 6. Independent audit of the user's control-event corpus

[MEASURED-OFFLINE] `capture_study/audit_event_semantics.py` separately reread all **111,404 saved non-filler control events**. All **1,833 CFEA events** use the recognized nonempty `ffff0100` carrier; none is all-zero or an unrecognized carrier. The earlier class counts, including class 2 for `meres48-96khz`, therefore survive this stricter carrier check. This result comes from the user's saved raw control blocks, not the upstream examples.

The audit also looks for byte-identical complete SINGLE SysEx content from opposite M/S source identities within two capture seconds. For meres47 it finds three M-after-S observations for address 01000600 and three for 00000300; meres50 has six of each. Reverse-order observations also exist. These are leads for control chronology, **not successful-grant counts**: repeated identical templates can pair coincidentally, two seconds is an analysis window rather than a protocol timeout, and a MAC cannot authenticate the sender. Such matches cannot replace direction/session/semantic checks or resolve who generated the traffic.

The original length-inconsistent S head-mark and a properly framed M record can contain the same inner SysEx bytes. Outer validity and inner-payload equality are separate facts. The offline evidence must preserve both rather than discard the envelope or infer complete acceptance.

## 7. User clarification and next evidence

On 4 November the user said the historical wiring is not remembered precisely, and offered new captures as needed. Therefore the old topology remains explicitly unknown. Do not keep requesting a retrospective guess or silently promote the old traces to a known-good hardware baseline.

A concrete new capture plan and metadata template are supplied. Prefer one mirrored endpoint port, ingress plus egress, to observe original master and stagebox frames without double-mirroring both ports. A PC on the M-400 SPLIT port is a useful receive-only alternative, but observes the console's downstream composition; it cannot alone identify the stagebox's original upstream packet geometry. A separate passive B capture establishes the console's behavior on the proposed endpoint port. Only then define an active emulator comparison with exact binary/configuration identity and deliberate signal mapping.

The remaining high-value questions are hardware acceptance and patch exposure of a virtual forty-input endpoint, physical packet/sample mapping, a complete accepting-device session model and clock-locked Windows transport/ASIO behavior. The now-complete offline research package supplies a reproducible basis for those tests rather than claiming that they have already passed.

# New capture campaign: distinguish hardware from emulator behavior

Prepared 4 November 2026. The user's historical meres47/meres50 wiring is not remembered precisely. New files should replace uncertainty with a recorded experiment, not a guessed retrospective label. No live test was started as part of preparing this plan.

## Record once per take

Copy `capture_study/new_capture_metadata_template.json` beside each PCAPNG and fill the actual values. Record M-400 firmware, displayed rate, FOH/monitor/backup mode, S-1608 switch setting, all cables/ports, switch/mirror setup, PC NIC/driver, capture interface, software/binary hashes and console input/output patch. Unknown values remain null. Include a photo or drawing when convenient; the text ledger must still say what each port does.

Use full packet capture with no snap-length truncation. On a dedicated REAC capture connection, initially use no capture filter; tagged frames and unexpected control traffic must remain available. `eth.type == 0x8819` can be a Wireshark display filter afterward, adapted for actual VLAN representation. Save interface statistics and original timestamps. Do not edit packet bytes. Timestamp precision is not an egress-jitter guarantee.

## H01: known-good original bidirectional link, if mirroring is available

```text
M-400 REAC A ---- switch port 1
S-1608      ---- switch port 2
PC capture  ---- mirror destination port 3
```

Mirror **port 2 ingress and egress once**. Do not mirror both endpoint ports into one capture, which can duplicate the same traffic. Keep the M-400 in its normal FOH master configuration and the physical S-1608 in slave mode. Stop all REAC emulators/transmit probes; record `emulator_running=false` and which processes were checked. This plan does not assume such a switch is available or ask for a purchase.

Begin capture before the stagebox is connected/powered for the documented cold-start test. Record at least 60 seconds after enrollment. Use the console's documented 48 kHz setting for the first new series; record the actual displayed and announced rate. A second separate take can cover 44.1 kHz after the first result is understood. Observe a deliberate single reconnect in its own take, with the event time in the ledger.

Expected evidence: master and original S-source frames at one known endpoint, direction, frame geometry, CFEA carrier/rate, scene/control sequence, steady behavior and reconnect. Presence of a MAC alone is not the evidence of physical origin; the controlled absence of the emulator and documented cable path supplies the missing provenance.

## H02: receive-only SPLIT mapping, usable without a mirror

```text
S-1608 SLAVE <---- M-400 REAC A
PC capture  <---- M-400 SPLIT/BACKUP, in FOH split operation
```

Run only a passive capture. Record the M-400's A output patch and input patch. Use distinguishable input activity on physical S-1608 sockets 1, 2, 9 and 16, one at a time, with event times. One source/cable can be moved between sockets; sixteen independent analogue sources are not required for this first mapping.

Suggested 90-second take: 0-15 s baseline; socket 1 active around 15-25 s; socket 2 around 30-40 s; socket 9 around 45-55 s; socket 16 around 60-70 s; then baseline to 90 s. Record actual transitions if manual patching differs. Use a recognizable steady signal or pulses at a moderate level and note the source; do not describe uncalibrated analogue level as an amplitude calibration. Avoid changing preamp/patch settings during the take unless that is the named experiment.

Expected evidence: which physical input appears in which forwarded console slot and whether channel-bank aliasing is visible. **Limit:** SPLIT observes the console's composition and possible forwarding/repacketization. It does not expose original stagebox unicast frames and cannot by itself decide whether the physical upstream packets carry six or twelve samples. If H01 is unavailable, preserve this question rather than substitute the forwarded format as its answer.

## H03: passive view of the proposed B-port endpoint

```text
S-1608 SLAVE <---- M-400 REAC A
PC capture  <---- M-400 REAC B
```

With the emulator stopped, capture startup/idle behavior on B for about 60 seconds. Save the B input/output patch state, including unavailable/grayed jack numbers. This establishes what the console emits before a software slave exists. Short/full master frames are observations, not automatically corrupt or a successfully routed session.

Expected evidence: B's own master identity/rate/control cadence and baseline patch availability. This is not the active 40-input test and cannot prove forty-channel playback.

## E01: later controlled emulator comparison

Use B for the software endpoint, keep the real S-1608 on A, and define the exact endpoint profile, binary hash, source MAC, sample format, clock policy and console patch before the take. A distinct software identity is needed to distinguish it from the real S-1608. The old source hardcodes MACs and a six-sample policy; do not assume an arbitrary old executable is a neutral baseline.

Begin with one identifiable route each way and log packet counters, state events, clock estimate and ring occupancy. Keep a continuous signal running across heartbeat/control frames. Check for duplicated bank routing and automatic monitor fallback. Only after minimal acceptance and clock behavior are understood expand to forty independent lanes and sustained operation.

## What to return

For each take: unchanged PCAPNG, filled JSON ledger, patch/settings screenshots or text, test-signal event times and relevant software log. A filename example is `H01_M400A_S1608_bidir_48k_cold_001.pcapng`; the filename is a label, while the ledger and bytes establish the facts. ZIP files are suitable for transport.

This campaign can be staged: H02 or H03 is already useful with the existing console/stagebox/PC. H01 provides the original bidirectional bytes when a suitable tap/mirror is available. No claim about actual switch features, current cabling or live access is assumed.

# M-400 / S-1608 / Windows system model

Review date: 26 October 2026 (Europe/Budapest). Target: simultaneous 40 mono capture channels and 40 mono playback channels in FL Studio. This is a research model and proposed validation path. No live rig, complete Windows REAC driver or proven 40 x 40 session was tested here.

## 1. Signal direction and equipment capacity

| Name | Signal direction | Meaning |
|---|---|---|
| Stagebox input | Microphone -> stagebox ADC -> console | An input from the box's perspective |
| Stagebox output | Console -> stagebox DAC -> loudspeaker/monitor | An output from the box's perspective |
| ASIO input | Console/network -> Windows -> FL Studio | Host recording/capture |
| ASIO output | FL Studio -> Windows/network -> console | Host playback |
| Wire slot | A sample position in a REAC packet | Neither a physical socket nor a console DSP strip by itself |

[VENDOR] The S-1608 has 16 analogue inputs and eight analogue outputs. The M-400 has 48 input processing strips. Network capacity, physical I/O and DSP strip count must be budgeted separately. Sixteen live microphone strips plus 32 independent DAW-return strips fill the 48 strips; sixteen plus forty would require 56. Virtual soundcheck can replace strip sources rather than add a second strip for every source.

## 2. The two M-400 REAC systems

The M-400 v1.5 manual was read as text and its relevant rendered pages were visually checked. Page numbers below are the printed page numbers, not an assumed PDF viewer offset. Local PDF SHA-256: `12186356a7f0fe5d0aa5097a9a6e4c7f6402e58bd7b5736f5d767e5309b13d6c`.

| Property | REAC A and SPLIT/BACKUP | REAC B | Manual |
|---|---|---|---|
| REAC system | One shared system | Separate system | pp.181-182 |
| FOH role | Master on A; split/backup is associated with A | Master | pp.181-182,187 |
| Maximum incoming jack positions | 40 on A | 40 on B | p.116 |
| Freely assigned output-patchbay channels | 8 | 40 | p.118 |
| FOH/backup downstream composition | Eight console output-patchbay channels, then A device inputs, then B device inputs, capped at 40 | Forty output-patchbay channels | pp.181-182 |
| Meaning of split | Receives master's distribution; it does not provide a second independent slave connection | B may also be distributed with a splitter | pp.181-182 |

[VENDOR] Two slaves cannot be attached separately to A and SPLIT/BACKUP as if those were independent systems (p.181). An Ethernet switch distributes frames; it does not perform the audio/control aggregation of an S-4000M merge unit.

[INFERENCE] The candidate development topology remains:

```text
S-1608 (real slave) <---- REAC A ----> M-400 (FOH master)
                                         |
                                      REAC B
                                         |
                              dedicated Windows NIC
                                         |
                             virtual slave + common clock
                                         |
                             one 40-input / 40-output ASIO
                                         |
                                      FL Studio
```

This keeps the candidate PC endpoint on the separate B system. It is a proposed experiment, not a tested connection recipe. Normal audio reception on SPLIT/BACKUP and active bidirectional playback on B are different development milestones.

## 3. The 40-input/0-output virtual-box idea

[CODE] FreeREAC has an experimental `fr4000` profile. In stagebox terminology, 40 declared inputs would carry 40 DAW-playback channels upstream into the console. A receiver can independently inspect all downstream audio bytes; receiving bytes does not require 40 physical DAC sockets.

The handoff's assertion that a zero-output profile necessarily creates a zero-width PipeWire capture node was incorrect. The explicit caller passes `out_ch=0`, but the source-node constructor applies a non-positive-width fallback to 40. Both the old pinned and newly reviewed code contain that fallback. The source review records the exact locations and commits.

[MEASURED-OFFLINE] A bounded MSVC harness of unmodified libreac builder code accepts ordinary inventory profiles 40/0, 40/8 and 24/24, and rejects 40/40. Its twelve four-channel inventory cells represent 48 declared sockets in this builder. This limits that declaration model; it does not establish a 48-channel shared transport limit. Roland documents a physical S-MADI bridge with up to 40 x 40 REAC/MADI transmission.

[OPEN] Host capture width and console patch availability remain separate. The M-400 manual says unavailable jack positions are grayed out for the connected unit (p.116). A 40/0 identity might leave downstream patch choices unavailable even when the host captures 40 wire slots. It must be tested whether the M-400 accepts the identity, exposes input slots 1-40, permits forty independently assigned B output signals, and retains them across reconnect/scene changes. A 40/8 declaration is another representable profile; it is not proof of forty usable output-patch entries.

## 4. Clock and packet units

[CODE] The ordinary FreeREAC format uses twelve samples per channel per packet. At 48 kHz this gives 4000 packets/s; at 44.1 kHz, 3675 packets/s. For untagged packets without FCS, `length = 52 + 3 * channels * samples_per_packet`. The familiar `52 + 36 * channels` rule assumes twelve samples.

[CODE] The supplied legacy transmitter uses sixteen channels and six samples per packet. Its 340-byte packet at 8000 packets/s is therefore an intended nominal 48 kHz software format. A 340-byte packet can also be interpreted as eight channels and twelve samples. Frame length cannot select between these hypotheses. This finding describes software behavior; it does not establish that Roland accepts the six-sample format.

[CODE] The legacy follower compares the master's counter cadence against the transmitter's `sampleRate/6` nominal packet cadence. A normal 48 kHz master at 4000 packets/s cannot pass a +/-1500 ppm gate around 8000. The correct unit boundary is master sample rate -> local TX packet cadence. ASIO callbacks, RX and TX must share or deliberately bridge that sample-clock domain.

[INFERENCE] ASIO block sizes need not be multiples of twelve. An accumulator must retain residual samples across callbacks and packet boundaries, including 64/128/256-frame host buffers. A DAW stop or underrun must not stop the network cadence or replay stale playback on restart.

## 5. Routing and host limits

[VENDOR] M-400 sampling choices are 44.1 and 48 kHz (manual p.196). A generic REAC-family 96 kHz claim or a filename containing `96khz` does not extend this console's supported rates.

[VENDOR] The documented M-400 external insert path uses local CONSOLE IN/OUT 1-8 pairs (pp.54,138). Forty network channels do not add forty external insert sockets or forty extra DSP strips. Network send/return workflows need an explicit console patch and latency budget.

[VENDOR] FL Studio obtains its channel list from the selected audio driver and routes external audio through Mixer input/output selectors. Its Mixer tracks are stereo. Forty independent mono transport lanes can be exposed by ASIO while output routing may use twenty stereo pairs with explicit L/R placement. The actual installed FL Studio version's selectors and channel labels must be checked; a 40 x 40 transport must not be described as forty stereo pairs. No FL Studio routing preset was tested or produced in this study.

[INFERENCE] A single custom ASIO endpoint should own both directions. Adding a local waveOut monitor creates a second clock domain unless it is deliberately bridged. Npcap is a plausible Windows I/O experiment, but API availability and successful queue submission do not establish stable physical egress timing. Measure clock drift, ring occupancy, actual audio discontinuities and end-to-end latency before choosing whether a deeper Windows transport is needed.

## 6. Next milestone and acceptance evidence

1. **Controlled hardware reference.** Identify firmware, rate, A/B/SPLIT wiring, capture location, software/binary version and test patch. Record the real S-1608 with the emulator stopped. Distinguishable signals in both channel banks must distinguish geometry and physical socket mapping. Include startup and reconnect.
2. **Offline protocol gate.** Replay raw provenance fixtures through a strict decoder. It must preserve malformed/unknown traffic diagnostically, validate length/checksums/fragment bounds, retain stream identity and distinguish duplicate/reset/reorder events. Reassemble scenes with explicit completeness limits; do not infer hardware commits from byte counts.
3. **Minimal B-port endpoint.** Develop a separate identity with session state and measured clock lock. Verify discovery, inventory, scene/control response, a deliberately chosen audio route in each direction, no unintended duplication and recovery after restart. Live TX needs coordination with the user's rig.
4. **40 x 40 acceptance.** Forty distinguishable signals each way simultaneously, valid console patch, documented mono numbering, bounded ring occupancy and drift, no unexplained discontinuities, measured latency, and restart/reconnect recovery. Proposed initial endurance: one hour under normal host load, then the intended session duration. These are proposed engineering acceptance criteria, not a vendor guarantee.

Immediate implementation priority is a verified packet/session/clock model, followed by a small B-port test. The existing legacy project supplies failure mechanisms and reference material, not the new product's foundation.

## Sources

- [Roland M-400 v1.5 manual](https://static.roland.com/assets/media/pdf/m400_manual_v15.pdf), visually checked pp.54,115-116,118,138,181-182,187,196. Local original: `research/vendor/m400_manual_v15.pdf`.
- [Roland M-400 product description](https://proav.roland.com/global/products/m-400/), 48 processing channels and recording/split scope.
- [Roland S-MADI](https://proav.roland.com/global/products/s-madi/support/), 40 x 40 and selectable REAC/MADI/word-clock sources. This is a hardware reference option, not a purchase recommendation or ready-made emulation profile.
- [FL Studio audio settings](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_audio.htm) and [Mixer external inputs/outputs](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm). Checked 26 October 2026.
- [Npcap API](https://npcap.com/guide/npcap-api.html), supported API and timestamp/statistics semantics. Checked 26 October 2026.
- Local source-specific reports: `FreeREAC_Protocol_Review_EN.md`, `Legacy_Source_Review_EN.md`, and capture report. Their evidence is version-pinned; this system model does not replace those details.

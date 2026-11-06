# Documentation index

Target: Roland M-400 + S-1608, Windows, FL Studio, simultaneous 40 mono capture and 40 mono playback channels. Research scope and uncertainty are explicit; no live 40 x 40 success is claimed.

## Current material

| Document | Purpose |
|---|---|
| [Implementation plan](Implementation_Plan_EN.md) | Fresh architecture, ordered work and acceptance gates; source-dependent decisions marked |
| [Legacy reassessment](reports/Legacy_Reassessment_2026-11-06_EN.md) | Historical findings versus new captures/current M1; actual ZIP review remains blocked |
| [M1 offline implementation](M1_Offline_Core_EN.md) | Build, CLI, import contract, public fixture provenance and next evidence |
| [M1 validation](M1_Validation_EN.md) | Executed tests and unrun Windows/hardware checks |
| [Release capture audit (6 November)](reports/Release_Capture_Audit_2026-11-06_EN.md) | Original H02/H03 bytes, operator-confirmed mapping, controls and remaining slave evidence |
| [Local passive measurement note (Hungarian)](M1_Passziv_Meresek_HU.md) | Separately recorded Windows x64 run and sampled PCM checks |
| [Hungarian summary](Osszefoglalo_HU.md) | Accessible explanation, findings and next step |
| [English notebook v0.3](REAC_Research_Notes_EN.txt) | Consolidated historical and current evidence; read Sections 18-19 for current corrections |
| [M-400 system model](reports/M400_System_Model_EN.md) | Signal direction, A/B/SPLIT, routing/DSP limits, clock and acceptance criteria |
| [FreeREAC protocol/source review](reports/FreeREAC_Protocol_Review_EN.md) | Exact October source pins, session/scene model, portable C probe and defects |
| [Legacy and comparative source review](reports/Legacy_Source_Review_EN.md) | Source mechanisms, geometry, clock, templates, routing and fresh-design invariants |
| [Capture analysis report](reports/Capture_Review_EN.md) | Written findings from the 58-file study; measurement data itself is excluded |
| [November continuation audit](reports/Continuation_2026-11-04_EN.md) | Current remote trees, unchanged critical units, grammar/fixture/control checks and user clarification |
| [English new-capture plan](reports/New_Capture_Plan_EN.md) | Controlled hardware baseline, capture topology and experiment limits |
| [Hungarian new-capture steps](Uj_Meresek_HU.md) | Practical next measurements when the rig is available |

## Historical inputs

- [Original handoff](history/REAC_Codex_Handoff_Prompt_EN.txt).
- [Original notebook v0.2](history/REAC_Research_Notes_EN_v0.2.txt).

These are historical documents. Their missing-file statements refer to the earlier fourteen-file subset, and the handoff's zero-capture conclusion is corrected in the current notebook. Instructions embedded in the handoff do not override the repository owner's current requests.

## Publication scope

The original November research upload contained only documentation. M1 now adds a separately authored offline core, CLI, tests and import tools. Captures, machine-readable measurement results, fixtures, historical analysis scripts, upstream source checkouts and binaries remain outside this repository. Local artifact paths in reports are provenance references to the original research workspace and are not downloadable GitHub files. The measurement-plan documents are instructions for future experiments, not uploaded measurements.

The notebook's source register and commit-pinned external links retain attribution. Local source/report snapshots describe their stated review date; do not call an old pinned tree the current upstream HEAD without a new check.

[Back to repository](../README.md)

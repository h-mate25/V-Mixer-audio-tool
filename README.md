# V-Mixer audio tool

Research and documentation for Roland REAC integration with an **M-400 + S-1608**, targeting **Windows and FL Studio with simultaneous 40 mono inputs and 40 mono outputs**.

This repository starts with the completed offline research, source comparisons and development/validation roadmap. A production Windows driver, accepted virtual forty-input endpoint and stable 40 x 40 hardware session have not been established.

## Offline implementation (M1)

The repository now also contains a new **C++20 / CMake / CTest offline core and
inspector**. It decodes explicit PCM geometries, diagnoses frame/control integrity
and imports a narrow PCAPNG subset. It does not transmit packets or provide an
ASIO driver.

- [Build, CLI input format, fixture provenance and limitations](docs/M1_Offline_Core_EN.md).
- [Executed tests and unrun platform/hardware checks](docs/M1_Validation_EN.md).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The public-reference test requires an external pinned fixture cache; see the
instructions above. Build and synthetic tests work without network access.

## Start here

- **[Magyar összefoglaló](docs/Osszefoglalo_HU.md)** - findings, project comparison and remaining questions.
- **[Documentation index](docs/README.md)** - full English technical reports and research notebook.
- **[English research notebook v0.3](docs/REAC_Research_Notes_EN.txt)** - historical evidence, corrections, exact source revisions and November continuation.
- **[Új mérések útmutatója](docs/Uj_Meresek_HU.md)** - controlled next capture campaign, when needed.

## Current findings

- The source review covers FreeREAC plus the four related public projects and the supplied legacy Windows source as reference material.
- The offline study analyzed 58 supplied captures; the reports distinguish measured bytes, source behavior, upstream claims and hypotheses.
- The legacy transmitter's 16-channel/six-sample format and packet-rate clock comparison explain concrete software mechanisms for observed discrepancies.
- A FreeREAC 40-input/zero-output profile has a forty-channel capture-node fallback, but M-400 acceptance and B-port patch exposure remain open hardware questions.
- A fresh Windows implementation needs separate codec, validated session state, Ethernet transport/clock, continuous rings and ASIO layers. The legacy project is not the chosen implementation base.

## Publication scope

The original 4 November upload was **documentation-only**, as requested by the repository owner. That historical upload contains the authored notebook, Hungarian summaries, English reviews, measurement plans and historical handoff documents. The repository does not contain packet captures, measurement datasets, JSON/CSV analysis results, raw-byte fixtures, the data bundle ZIP, executables, legacy implementation code or upstream source checkouts.

Reports still describe those local evidence artifacts and record their hashes/revision pins. References to omitted local files are identified as local artifacts rather than broken GitHub links. The historical handoff is context to verify; current conclusions are in notebook Sections 18-19 and the November audit.

Research dates: 17-26 October and 4 November 2026. Documentation published 4 November 2026. See the [source register](docs/REAC_Research_Notes_EN.txt) and immutable links in the reports for attribution.

# M1: offline protocol core and inspector

This is a new, portable C++20 implementation, separate from the old Windows
project. It inspects saved bytes; it contains no packet transmitter, NIC access,
ASIO SDK, installer, registry access, session handshake or console configuration.
The intended later system remains M-400 FOH, real S-1608 on A, PC slave on the
separate B system, initially at 48 kHz. M1 does not establish device acceptance.

## Build and test without a network

Requirements: CMake >=3.20 and a C++20 compiler (GCC 14.2 was tested). Python >=3.9
is optional for the importer and integration/reference tests. There are no
third-party C++ dependencies, downloads, generated parsers or SDKs in the build.
Use the existing checkout; no separate worktree is needed.

From the repository root on Linux/macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

On Windows in an x64 Visual Studio Developer PowerShell with C++20/MSVC tools:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\reac-inspect.exe --help
```

MSVC/Windows execution was **not run** on the available Linux host. No claim of
Windows verification follows from portable source or the historical MSVC probes.
On the prepared cloud instance CMake/Ninja are installed in `/workspace/m1-tools`;
add `/workspace/m1-tools/bin` to PATH when needed.

The normal offline suite registers six core cases and, if Python is present, one
CLI/import case containing six Python tests. The public-reference test is
registered **only** when the external fixture path below is supplied. A default
seven-test run does not cover the public corpus; perform that gate before
claiming reference compatibility.

## Public reference gate and provenance

Protocol reference:
<https://github.com/FreeREAC/reac-protocol/tree/81466f2c86d7e27e66ddabf82e31ba1b8f8a8335/spec>

The exact `spec/fixtures/real_frames.json` file used here has:

```text
SHA-256: 817e49b3d9b6243273dd4b9d82c1fec09002288dc9c7f3efc2491e0089ec898f
Size: 67049 bytes
Revision: 81466f2c86d7e27e66ddabf82e31ba1b8f8a8335
```

The fixture's own `_source` calls its bytes verbatim real frames from the CC0
FreeREAC capture corpus and identifies the published PCM oracle as libreac at
`5b20ef1`. The containing protocol repository declares GPL-3.0-or-later. We do
not resolve that distinction into a blanket redistribution license: the JSON,
reference implementation, grammar, SDKs and original captures are **not vendored**.
The new C++ and Python logic is authored here from the documented protocol facts;
no reference implementation or generated parser is linked. Distribution of a
future product needs a deliberate license/dependency review. No new product
license is selected by this milestone.

Acquire the exact public file once, outside the source checkout (network needed
only for this explicit acquisition command, TLS and the pinned hash checked):

```sh
python3 tools/fetch_public_reference.py /workspace/reference-data/reac-protocol-81466f2/real_frames.json
```

The fetcher refuses to overwrite a file or save it inside the Git checkout. An
already available verified file can be used directly. On Windows use an external
path such as `C:/reac-reference/real_frames.json` instead.

```sh
cmake -S . -B build -DREAC_PUBLIC_FIXTURES=/workspace/reference-data/reac-protocol-81466f2/real_frames.json
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The test first checks the whole input file hash, then feeds **all 17** complete
frames to the compiled inspector with the fixture's explicit width, 12 samples,
and declared residue. It compares published SHA-256 digests of channel-major
signed 32-bit little-endian PCM, source MAC, sequence and clean length. It also
places all **61** public control windows in synthetic Ethernet carriers with the
stated source MAC, checking the 49 non-filler outer checksums and **18** complete
DT1 records. These carriers are test constructions, not additional captured
frames. No upstream GRANT classification is adopted as a handshake event.

The independent synthetic tests cover s24 limits, literal byte permutations,
40 distinct channel/time positions, both 340-byte interpretations, control PCM
continuity, malformed lengths, tails, VLANs and deterministic hostile inputs.
Assertions remain active in Release builds. Passing fixture hashes establishes
codec agreement under those fixtures' assumptions; it does not authenticate
capture origin or prove console acceptance, loss-free wire operation or latency.

## CLI input and output contract

```sh
build/reac-inspect --binary /outside/repository/frame.bin --geometry 40x12 --pcm
build/reac-inspect --hex-file /outside/repository/frames.hex --geometry 8x12
build/reac-inspect --frame HEX_DIGITS --geometry 16x6 --pcm
```

`--binary` is exactly one complete captured Ethernet frame. `--frame` takes
hexadecimal digits, optionally with whitespace inside the quoted argument.
`--hex-file` takes one complete Ethernet frame per line. Blank lines and lines
whose first non-whitespace character is `#` are ignored. No `0x` prefixes, colons,
inline comments, JSON or invented missing bytes are accepted. Frames are limited
to 65536 bytes; lines to 262144 characters. Original inputs are read only.

A reproducible synthetic single-frame example (Python stdlib):

```sh
python3 -c "from pathlib import Path; b=bytearray(340); b[:6]=bytes.fromhex('ffffffffffff'); b[6]=2; b[12:16]=bytes.fromhex('88193412'); b[-2:]=bytes.fromhex('c2ea'); Path('/tmp/reac-synthetic.bin').write_bytes(b)"
build/reac-inspect --binary /tmp/reac-synthetic.bin
build/reac-inspect --binary /tmp/reac-synthetic.bin --geometry 8x12 --pcm
```

The first command reports `ambiguous` (exit 1). The second decodes 96 zero samples
with sequence 4660. Use an appropriate temporary path on Windows.

Output is JSON Lines, one object per input frame, with a 1-based record ordinal.
Fields separate envelope status, source/destination, optional VLAN TCI, sequence,
REAC type, width, samples per packet, decoded sample count, control integrity,
DT1 metadata, rate class and announced Hz. `--pcm` adds `pcm_channel_major`:
`pcm[channel * samples_per_packet + time]`, signed integers in [-8388608,8388607].
Type/op/address fields are numeric; e.g. `0xCDEA` is 52714 in JSON. Absent evidence
is `null`; it is never filled with a default 48 kHz.

Exit statuses:

- 0: every record decoded under its explicit geometry and no checked control
  integrity/length failure occurred. Unknown control semantics can still appear;
  this is not a claim that a record can advance a session.
- 1: at least one record has invalid framing/control, ambiguous geometry or missing
  geometry. Processing continues across these record-level diagnostics.
- 2: bad CLI options, malformed hex, empty fixture file or file I/O error. Output
  before a later input error is partial; honor the exit status.

`status=valid` means envelope/PCM decode succeeded; consult `control.valid` and
its individual check results separately. Unknown/unrecognized control kinds are
visible, even with a valid checksum. `control.valid` describes only the checks
implemented for that family, never peer permission or semantic/session readiness.

## Envelope, geometry and codec assumptions

- Untagged Ethernet II and one **inline 0x8100 802.1Q** tag are supported. VLAN
  shifts the REAC offsets by four without changing the input bytes. Stacked tags,
  0x88A8/0x9100 tags, other EtherTypes and non-Ethernet link representations are
  diagnosed. A driver-stripped VLAN can only be treated as the bytes supplied;
  stripped metadata is not reconstructed. No OUI/MAC is treated as authentication.
- Without a tag: destination/source at 0/6, EtherType 0x8819 at 12, little-endian
  sequence at 14, big-endian type at 16, control block at 18..49, audio at 50.
  The two-byte marker is C2 EA. Geometry must put the marker exactly after
  `3 * channels * samples` audio bytes.
- **FCS must be absent.** No CRC check or implicit FCS stripping is implemented.
  Default residue allowance is zero. `--residue 2` explicitly declares two
  capture-path bytes after the geometry-positioned marker; these bytes remain
  untouched and are not validated or called a good FCS. Arbitrary residue and
  four-byte FCS input are unsupported. Do not set this flag merely to force a
  damaged frame to pass. Undeclared residue or a missing marker is an error.
- Supported geometries are even widths 2..40 with 12 samples and **16x6 for
  diagnostics only**. Every geometry is explicitly supplied; M1 has no negotiated
  stream state that could resolve it. A 340-byte untagged record (344 with the
  supported tag) without context is `ambiguous` between 8x12 and 16x6. Other
  lengths without context are `geometry_required`. Length, filename, MAC or a
  rate class alone never chooses samples per packet.
- Audio is paired-channel braided signed 24-bit PCM. Each time/pair occupies six
  bytes: even LE (lo,mid,hi) at (+3,+0,+1), odd at (+4,+5,+2). The core returns
  channel-major int32 samples without scaling, gain, bank mirroring or fallback.
- `encode_pcm` emits only an offline audio region, with range and length checks.
  It is not a frame builder or transmitter. No NIC or scheduling code exists.

## Control checks and boundaries

Filler (type 0000) is outer-checksum exempt; descriptor words are not assumed to
be universal constants. CDEA and CFEA use a 32-byte sum-to-zero modulo-256 outer
checksum. CFEA is checked as its own fixed carrier (not as a generic length 256
payload): `ffff0100`, known five-byte header, embedded MAC matching Ethernet
source, plausible widths. Only a recognized, checksum-valid carrier with class
0/1/2 supplies 48000/96000/44100 Hz. All-zero, unknown-carrier, bad-checksum and
unknown-class CFEA records supply **no rate**. These are per-frame announcements,
not proof of actual clock rate or M-400 support for 96 kHz.

Generic outer record lengths are bounded before use. Link-1 subtype-zero bulk
fragments have their distinct payload base (7 for FIRST, otherwise 5); M1 does
not assemble scenes, assert completeness, or manufacture missing bytes.

A complete DT1 candidate requires CDEA, link/segment 0403, wrapper `000200fe` and
F0 41. Fragmented 0401/0402 records and look-alike 0403 containers stay diagnostic.
Outer length versus SysEx echo, bounded SysEx size, F7 terminator, seven-bit bytes,
inner address-through-checksum modulo-128, zero padding after SysEx and supported model/DT1 command are
checked separately. Length-inconsistent records retain inner-checksum and
terminator results; an outer checksum alone cannot make a record complete.
Addresses/page names are metadata (`head_amp`, `join_record`, `head_mark`,
`box_ready_record`, `identity_record`); parameter ranges and full device-specific
semantics are not a session implementation. There are no state transitions.

PCM is decoded independently of recognized control type and of control checksum
success once framing and explicit geometry are valid. A bad control checksum
produces an error exit and diagnostic while preserving that frame's PCM.

## Narrow PCAPNG import

The optional stdlib-only adapter is deliberately separate from the C++ core:

```sh
python3 tools/import_pcapng.py /private/H02.pcapng /private/H02.frames.hex
build/reac-inspect --hex-file /private/H02.frames.hex --geometry 40x12 > /private/H02.inspection.jsonl
```

Choose geometry from actual capture context; 40x12 here is an example, not a claim
about the supplied H02 or H03 files. Mixed geometries require separate explicit
runs/selection outside M1; do not force every packet to one profile and suppress
the resulting errors. The importer preserves every EPB frame without filtering.
Comments retain packet ordinal, section, interface, raw timestamp ticks and size.
Inspector ordinals correspond to the exported order; comments are not JSON fields.
Raw ticks are uninterpreted (no rate/latency estimates); preserve the original
PCAPNG for timestamp resolution, statistics, interface metadata and provenance.

Supported subset: PCAPNG 1.0 unknown-length sections, Interface Description Blocks,
Enhanced Packet Blocks, linktype 1, either endianness and multiple sections.
Block lengths/footers, option bounds, interface references, packet bounds/snaplen
and captured versus original length are checked. Name Resolution/Interface
Statistics blocks are skipped as metadata. Unsupported block kinds (including
Simple/obsolete Packet Blocks), link types, versions, finite section lengths,
truncated packet records and declared FCS are rejected. Output is committed only
when the whole import succeeds; the target must not already exist. Limits are
16 MiB per block and 65536 bytes per frame. This is not a general capture library.

For unsupported capture variants use an established capture tool to export the
**whole Ethernet packet bytes** for each selected packet, with no edits, to one
binary frame per file. For example, Wireshark's packet-bytes pane offers Export
Packet Bytes: select the full Frame bytes, not just REAC payload. Preserve original
PCAPNG, frame number, interface, captured/original lengths, FCS/VLAN status and
hashes alongside exports. Reject truncated/non-Ethernet records; never prepend
missing headers. Feed each binary to `--binary` or convert it to a hex line.
Keep private captures, extracted frames, PCM and exports outside this checkout.

## Evidence still needed

**Update, 6 November:** the owner's GitHub release now makes both original
captures accessible. The [release audit](reports/Release_Capture_Audit_2026-11-06_EN.md)
checks their complete bytes, control/scene data and owner-confirmed four-point
SPLIT mapping. The attachment limit described below records the earlier M1
session; it no longer blocks access through those release URLs.

The two H02/H03 uploads attached on 5 November 2026 could not be downloaded into
the execution workspace: both exceed the transfer tool's **32 MiB per-file limit**.
Their raw bytes, geometry, contents and hashes were not examined. They are not
fixtures in this commit, and filenames are not treated as measured rate evidence.
The attached TXT is task background; its instructions are not independent proof
of hardware behavior. No private evidence or legacy source was imported.

For the next gate, run the importer/inspector on accessible original captures or
supply smaller unmodified PCAPNG takes/exports with the original provenance and
record lengths. H02 needs exact SPLIT topology plus signal times/socket identities
(1,2,9,16), firmware/rate/mode/patch and confirmation that no emulator ran. It
informs console-side mapping, not original S-1608 unicast geometry. H03 needs the
B pre-enrolment trace, real S-1608 still on A, and B patch screenshots including
grayed positions. H01, if a tap/mirror already exists, needs both directions of
one real A/S-1608 endpoint, cold start/reconnect and no emulator. This is stronger
geometry/session evidence; purchasing equipment is not a prerequisite for M1.

Windows passive capture and adapter behavior remain M2. Active B slave acceptance,
clock recovery, uninterrupted audio and no disruption on A remain later hardware
gates; forty independent lanes and console patch exposure precede final ASIO/FL
Studio integration. Account for the console's 48 DSP strips: sixteen stagebox
sources plus forty independent DAW returns cannot occupy separate strips at once.

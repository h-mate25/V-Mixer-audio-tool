# M1 validation record

Date: 5 November 2026. Starting repository revision:
`cf3cfed8f71e13e92a77a2d751bc40a13be0d7a3`.

Host: Linux x86-64, GCC 14.2.0, CMake 4.4.4, Ninja 1.13.2, Python 3.12.14.
The CMake/Ninja tools were installed in an isolated `/workspace/m1-tools` venv;
no application dependency or external SDK is installed. Build parallelism: 4.

## Executed checks

- Debug configure/build: passed without compiler warnings.
- CTest Debug with pinned external public reference: **8/8 passed**.
- Fresh Release configure/build with no public fixture path or acquisition: passed;
  the default offline CTest suite passed **7/7** (public corpus not registered).
- Six C++ test cases cover signed edges, literal pair/40-channel ordering,
  geometry ambiguity, framing/bounds/VLAN/residue, independent control checks
  and deterministic hostile/truncated input.
- CLI/import CTest runs six Python unittest tests, including both PCAPNG byte
  orders, multi-section input, exact imported bytes, malformed/truncated records,
  unsupported link type/FCS and atomic export failure: all passed.
- Public reference: **17/17 full-frame PCM SHA-256** matches, **61/61 control
  windows**, **18 complete DT1** checks. Input JSON SHA-256 was verified before
  tests; see [provenance and commands](M1_Offline_Core_EN.md).
- GCC AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer: **8/8 passed**.
  The initial sandbox run failed at LeakSanitizer startup because the execution
  sandbox uses ptrace. The identical tests were rerun outside that sandbox and
  passed with leak detection still enabled. No sanitizer check was disabled.

Sanitizer reproduction (GCC/Linux):

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie" \
  -DREAC_PUBLIC_FIXTURES=/workspace/reference-data/reac-protocol-81466f2/real_frames.json
cmake --build build-sanitize --parallel 4
ctest --test-dir build-sanitize --output-on-failure
```

## Unrun checks and limitations

Windows x64/MSVC: **unrun**, suitable host/toolchain unavailable. No live NIC,
packet TX, driver, ASIO, FL Studio, console or stagebox check was attempted.
The supplied H02/H03 PCAPNG files both exceed the download tool's 32 MiB transfer
limit; **neither capture was inspected**. PCAPNG replay was verified against
synthetic records, not these private captures. The 58-capture historical corpus,
private fixtures and legacy code were not available in the repository.

This result passes the offline codec/inspector gate under the documented public
and synthetic assumptions. It is not a 40x40 hardware/audio-interface claim.

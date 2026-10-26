# Validation: October 26, 2026

## Native Matter server: 0.1.0-alpha.1

ESPHome 2026.10.0, official LibreTiny 1.14.0, Rust nightly-2026-10-24,
rs-matter `7195e2b1f45df94ee1bc63605b7db91a6878b9dc`.
Independent controller: ConnectedHomeIP C++ SDK from `home-assistant-chip-core`
and `home-assistant-chip-clusters`, both 2025.7.0.

The automated interoperability suite passes **85 checks in 12 stages**:

| Stage | Evidence |
| --- | --- |
| Local controls | Close/reopen before pairing and on a paired device; existing fabric remains usable |
| Initial commissioning | QR discovery via mDNS, independent setup-code/check-digit checks, wrong PIN rejected, PASE, PAA/DAC/PAI attestation, CSR, AddNOC, CASE, CommissioningComplete |
| Process restart | Actual FlashDB fabric reload, CASE, persisted OnOff=true and NodeLabel; repeat operational checks |
| Warm restart | Drop Rust future/stack, close sockets, reinitialize; same fabric/state; repeat operational checks |
| Reset storage | Matter reset retains the numeric ESPHome preference sentinel |
| Reset revocation | Previous controller fabric can no longer authenticate |
| Recommission after reset | Fresh controller pairs, OnOff=false, empty NodeLabel, exactly one fabric; repeat operational checks |
| Reset failure | Injected NOR deletion failure stops reset without reporting success |
| Fabric write failure | NOR rejects fabric write; commissioning fails |
| Storage recovery | Failed fabric absent after reboot; manual-code pairing with another PIN/discriminator succeeds; repeat operational checks |
| Setup-code privacy | No codes when disabled or after normal commissioned restarts |
| Entropy failure | Stop before commissioning; no insecure fallback |

Operational checks include Off/On/Toggle and readback, subscription reports, product
and alpha version identity, NodeLabel write/read/persistence, OnOff write protection,
ACL denial, an invalid endpoint, 98 malformed UDP datagrams and a subsequent valid
read. The [machine-readable summary](server-test-results.json) lists each assertion.

The host uses the same Rust library, UDP adapter and FlashDB delegate as firmware.
POSIX replaces FreeRTOS/lwIP/drivers; actual FlashDB runs over a 32-KiB NOR simulator.
This does not simulate physically cutting power during a flash operation.

Additionally, **93 pytest tests** pass:

- Actual ESPHome/FreeRTOS bridge with platform doubles and ASan/UBSan: Wi-Fi/IPv6
  startup gates, reconnect, lost IPv6, low heap, failed task creation, one-command
  mailbox, reset restart, fatal reset failure, bounded protocol retries, full TRNG
  buffer filling, entropy errors/stuck output and aligned allocation boundaries.
- Actual ESPHome validation: prohibited passcodes, platform, IPv6, mDNS,
  discriminator and commissioning-window limits.
- QR images decoded independently by ZXing, white quiet zone, matching setup-code
  pairs from logs and rejection of ambiguous/invalid inputs.
- SHA-256 verification of unchanged upstream files and the public test PAA.
- IPv6 probe, FlashDB and CHIP codecs, including 20,000 reproducible random parser
  inputs under address/undefined-behavior sanitizers. The standalone ESPHome codec
  host component runs 72 individual checks.

Black, isort with repository settings, autoflake, Ruff, clang-format and Rustfmt
check the modified sources. GitHub Actions runs host tests, independent controller
interoperability and 14 firmware profiles. Inspect the commit's Actions runs for
CI results; local results are not a substitute for those runs.

## Firmware budget

Both Beken targets fit the normal LibreTiny application partition, with a
**1,083,136-byte flash limit**. The `*-existing` profiles add API, OTA, local buttons
and the virtual-state diagnostic sensor.

| Profile | Flash | Static RAM |
| --- | ---: | ---: |
| BK7231N | 873,870 B | 106,956 B |
| BK7238/T1 | 974,768 B | 110,856 B |
| BK7231N + API/OTA/controls | 905,130 B | 107,380 B |
| BK7238/T1 + API/OTA/controls | 1,006,048 B | 111,280 B |

Linker RAM is **not measured free runtime heap**. BK7231N uses separate RAM/TCM
regions, so 256 KiB minus linker RAM is not available heap. The 24-KiB worker stack
is allocated dynamically. Startup rejects less than 40 KiB free heap; this threshold
does not prove adequate reserves throughout operation.

Vendor SDK warnings remain possible. API/OTA builds verify linking and capacity,
not a real OTA transfer or recovery after an interrupted update.

## Earlier diagnostic/codec profiles

These figures come from earlier successful local/CI builds. They contain **no
running Matter server** and remain separate from the server budget.

| Profile | Flash | Static RAM |
| --- | ---: | ---: |
| BK7231N diagnostics | 500,794 B | 47,620 B |
| BK7238/T1 diagnostics | 601,344 B | 51,528 B |
| LN882H diagnostics | 409,816 B | 94,332 B |
| RTL8720CF diagnostics | 435,769 B | 12,841 B |
| BK7231N storage | 502,490 B | 47,644 B |
| BK7238/T1 storage | 603,032 B | 51,552 B |
| BK7231N codecs | 508,878 B | 47,684 B |
| BK7238/T1 codecs | 609,416 B | 51,592 B |
| LN882H codecs | 417,792 B | 94,400 B |
| RTL8720CF codecs | 443,117 B | 12,841 B |

LN882H has a 1,228,800-byte flash limit; the tested RTL8720CF 2M/992k profile has
1,015,808 bytes. Platforms count RAM segments differently; the small RTL figures
in particular do not represent total consumption.

## Platform dependencies and limits

| Target | SDK | lwIP |
| --- | --- | --- |
| BK7231N | BDK 3.0.33, `841343e64d7584a3a650222ab0b6ea967755352a` | 2.2.0-bdk, `0d1cedf` |
| BK7238 | BDK 3.0.78, `44800e7451ea30fbcbd3bb6e905315de59349fee` | 2.2.0-bdk, `0d1cedf` |
| LN882H | `8794085af8f548b4a1f783fdbab36d86aeff9a74` | 2.1.3-ln882h, `7ef5aac` |
| RTL8720CF | AmebaZ2 `5f7b9378a7f226a46e3b13349844a3f5f07565aa` | 2.1.3-ambz2, `929d2dd` |

LibreTiny resolves its framework packages; branch dependencies are not a complete
package lock. Cargo dependencies are pinned by commit and `Cargo.lock`.

**Not yet demonstrated:** physical Beken execution, real AP multicast, TRNG quality,
PASE/CASE timing on ARM9, peak heap/stack, physical power-loss recovery, all FlashDB
metadata failure paths, multiple fabrics/groups/subscriptions under load, long-term
operation, BLE/Wi-Fi coexistence or acceptance by commercial ecosystems. Tests use
public development certificates, not a certified product identity. Follow the
[first hardware test](first-hardware-test.md) to establish the next evidence gate.

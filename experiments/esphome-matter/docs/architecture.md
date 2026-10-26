# Native Matter port architecture

The server uses **Project CHIP rs-matter**. Protocol handling, cryptography and
cluster state come from a pinned upstream version. Platform integration is kept
separate from ESPHome's Python schema and the Matter data model.

```text
ESPHome YAML -> validation / code generation
    |
MatterServer: ESPHome main loop
    |-- existing Wi-Fi and IPv6 / address changes
    |-- FlashDB delegate in the ESPHome preferences task
    |-- one-request queue + semaphore
    |-- local commissioning/reset commands and virtual state
    |
FreeRTOS Matter worker (24 KiB stack, priority 1)
    |-- C ABI: TRNG, time, allocator, logs, storage, UDP
    |-- rs-matter: PASE/CASE, MRP, fabrics/ACL, Interaction Model
    |-- root endpoint: Wi-Fi diagnostics, administration, attestation
    `-- endpoint 1: Descriptor, Identify, Groups, OnOff light
```

The server does not configure GPIOs. A critical section protects the virtual OnOff
state shared with the main loop. Future entity mapping belongs in the ESPHome
layer; platform networking remains behind the C ABI. This follows the separation
of common configuration and platform backends in ESPHome's Zigbee component.
Zigbee's radio protocol is not interchangeable with Matter.

## Platform contract

- ESPHome owns Wi-Fi credentials and reconnection. Matter waits for valid IPv6.
  Address changes close sessions and restart the stack with persisted fabrics.
  Matter Wi-Fi scan/network changes are explicitly rejected.
- Two nonblocking dual-stack UDP sockets serve Matter on 5540 and mDNS on 5353.
  Receive buffers are bounded; oversized packets are discarded without another
  packet queue. The Rust receiver yields between datagrams. The executor polls
  every 10 ms; send retries after `EAGAIN` are bounded.
- A single-entry queue dispatches storage operations to the ESPHome main loop.
  The worker waits for acknowledgement so a borrowed stack pointer cannot expire
  while the main loop is still using it. All shared FlashDB access stays there.
- FlashDB uses the existing partition and `matter/rs/xxxx` keys, with values up to
  2 KiB. The tested C++ CHIP storage delegate is reused. No new partition is added.
- Entropy uses Beken `sddev_control(IRDA_DEV_NAME, TRNG_CMD_GET, ...)`, one checked
  word at a time. Three identical consecutive words stop Matter. This continuity
  check is not entropy certification. Neither the old BDK byte helper nor
  `lt_rand_bytes()`/`rand()` supplies Matter entropy.
- FreeRTOS provides heap and critical sections. The clock extends millisecond
  counter wraparound. Rust panics/entropy errors stop the worker while ESPHome
  remains active; this is not protection against hardware stack overflow.
- Static protocol state is initialized in place to avoid large temporary stack
  copies. Warm stop drops the future before its borrowed protocol state.
  Initialization, polling and stopping all run in the same worker.
- Local commands use a one-entry mailbox. Opening/closing uses upstream Interaction
  Model APIs. Reset first drops network tasks, resets cluster state and removes
  Matter keys, then restarts Matter. It retains ESPHome's numeric preferences.
  A failed reset returns an error; it never reports successful completion.

## Reproducible dependencies

`rs-matter` 0.4.1, commit `7195e2b1f45df94ee1bc63605b7db91a6878b9dc`,
Apache-2.0, implements Matter 1.6 schemas. The independent controller is
ConnectedHomeIP from `home-assistant-chip-core/clusters==2025.7.0`.
Rust nightly `2026-10-24` builds `core`/`alloc` for `thumbv5te-none-eabi`.
`Cargo.lock` pins transitive dependencies; `cargo --locked` prevents silent updates.

Two limited upstream adaptations live under `rust/vendor`:

1. `embassy-sync` uses portable atomics in `LazyLock`/`OnceLock` because ARMv5TE
   lacks the required core atomics. FreeRTOS provides the critical sections.
2. `rs-matter-codegen` generates only the 53 cluster declarations referenced by
   compiled modules. Used fields/commands remain unchanged. Unfiltered upstream
   generates over 30 MB of Rust and exceeds the compiler memory budget.

Manifests record original checksums and modified files. Tests verify unchanged
files. Licenses and copyrights remain intact. Cryptographic algorithms and protocol
implementations have not been locally reimplemented.

## Evidence and remaining work

The [validation report](validation.md) records controller interoperability, platform
simulation and Beken firmware builds. These cover development attestation, stored
fabrics, CASE after restart, OnOff/read/write/subscribe, access rejection, malformed
packets, storage/entropy failures and the local control/reset lifecycle.

Remaining work includes actual Beken radio/resource measurements, power loss during
flash operations, multiple fabrics/groups/subscriptions under load, product
attestation, entity mapping and optional BLE provisioning. LN882H/RTL8720C need
additional IPv6/platform integration.

Performance claims require MCU measurements: flash, static RAM, minimum remaining
stack/heap, PASE/CASE duration, command latency and a 24-hour run. Successful linking
does not establish runtime headroom or sufficient ARM9 cryptographic performance.

# Matter over Wi-Fi for ESPHome and LibreTiny

**Hardware test release: `matter-v0.1.0-alpha.1` — BK7231N first, BK7238/Tuya T1 second.**
This native ESPHome external component runs Project CHIP **rs-matter** on the
**official LibreTiny 1.14.0** platform. It provides a virtual On/Off light, secure
commissioning, persistent fabrics, subscriptions, manual setup codes and QR codes.

The software is ready for a first controlled device test. Independent controller
interoperability, simulated platform tests and firmware builds are verified.
**It has not yet run on physical Beken hardware.** Radio behavior, ARM9 crypto
latency and runtime memory headroom still require UART measurements. This is a
development device with public test attestation credentials, not a certified product.

## Start here

1. Read the [first hardware test procedure](docs/first-hardware-test.md).
2. Follow the [build, installation and pairing guide](docs/matter-server.md).
3. Record the result using [the test report template](docs/hardware-test-report.md).

Use the immutable `matter-v0.1.0-alpha.1` tag for this test. The
`feat/esphome-matter-feasibility` branch may contain later development changes.
[Validation results](docs/validation.md) distinguish host evidence from hardware
measurements. See also [architecture](docs/architecture.md),
[storage](docs/storage.md) and [hardware feasibility](docs/feasibility.md).

## Included in the first test release

- Native PASE/CASE, development attestation, fabric/ACL storage, Interaction Model,
  MRP and mDNS from a pinned upstream stack.
- Endpoint 1: a **virtual** On/Off light. No GPIO or relay mapping is configured.
- Manual Matter pairing code and IP commissioning QR payload generated from the
  device's actual configuration. Local PNG/SVG/HTML rendering is included.
- Persistent fabric, NodeLabel and OnOff state; recovery after network changes.
- Optional ESPHome API controls to open/close commissioning and reset **Matter only**.
  The reset button is disabled by default; ESPHome preferences and Wi-Fi remain.
- A read-only ESPHome diagnostic entity showing the virtual light's state.
- Logs for startup, commissioning, storage failures, network recovery, heap,
  slowest protocol poll and minimum remaining worker stack.

ESPHome manages the existing Wi-Fi connection. Matter Wi-Fi scan/reconfiguration,
BLE commissioning, Matter OTA, real entity mapping and product attestation are
outside this release. ESPHome API/OTA remain usable by IP; ESPHome's mDNS responder
must be disabled because the Matter stack supplies its own.

LN882H and RTL8720CF retain diagnostics and codec test profiles. They do not yet run
this server. The investigated chips do not expose documented IEEE 802.15.4 support;
BLE does not enable Zigbee or Matter-over-Thread on this hardware.

## Build locally

Use Linux x86_64, Python 3.12, a C/C++ compiler and
[Rustup](https://www.rust-lang.org/tools/install). Allow approximately 6 GiB of
available host RAM for the initial Rust build.

```sh
git clone --branch matter-v0.1.0-alpha.1 https://github.com/seb5594/libretiny.git
cd libretiny/experiments/esphome-matter
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-dev.txt
rustup toolchain install nightly-2026-10-24 --profile minimal --component rust-src,rustfmt
cp examples/secrets.yaml.example examples/secrets.yaml
# Edit examples/secrets.yaml with your test network's Wi-Fi credentials.
esphome compile examples/bk7231n-matter.yaml
esphome compile examples/bk7238-t1-matter.yaml
```

`bk7231n-matter-existing.yaml` and `bk7238-t1-matter-existing.yaml` additionally
include API, OTA and the test controls. For your existing device, import the
[release package](docs/matter-server.md#existing-esphome-device) into its own YAML
instead of replacing its board/pin configuration with a generic example.

PlatformIO builds the locked Rust library for `thumbv5te-none-eabi` and links it
into the normal firmware. Bootloader and flash partitions are unchanged. Set
`LTM_CARGO_TARGET_DIR` to a shared absolute cache directory when building several
profiles. Cargo uses one compiler job to limit host RAM use.

## Components and tests

| Component | Purpose |
| --- | --- |
| `libretiny_matter_server` | Native Matter server and LibreTiny platform integration |
| `libretiny_matter_storage` | CHIP storage delegate over the existing ESPHome FlashDB |
| `libretiny_matter` | Unmodified upstream C++ headers/codecs used by storage and codec tests |
| `libretiny_matter_protocol` | [72 local codec checks](docs/protocol-tests.md) for existing devices |
| `libretiny_matter_probe` | IPv6/multicast diagnostics, heap logs and `LTM1` echo on UDP/49152 |

```sh
esphome compile tests/host.yaml
esphome compile tests/protocol-host.yaml
python tools/fetch_flashdb.py
pytest -q
```

The [independent interoperability suite](docs/matter-server.md#independent-interoperability-tests)
uses a ConnectedHomeIP C++ controller, the firmware's Rust library and UDP adapter,
and real FlashDB on simulated NOR. CI also builds 14 firmware profiles. It publishes
only a test summary, not controller credentials, setup logs or flash images.

The separate `bk7231n.yaml`, `bk7238-t1.yaml`, `ln882h.yaml` and `rtl8720cf.yaml`
diagnostic profiles remain available. `tools/check_udp.py` exercises their Beken
IPv6 echo service; that diagnostic protocol is separate from Matter.

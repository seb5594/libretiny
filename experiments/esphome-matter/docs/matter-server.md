# Build and test the native Matter server

## Existing ESPHome device

This release supports ESPHome 2026.10.0, official LibreTiny 1.14.0 and BK7231N or
BK7238/Tuya T1. Wi-Fi must already be configured in ESPHome. Install the pinned
Rust toolchain on the **build machine**, as described in the [README](../README.md).
An unmodified ESPHome Dashboard container usually lacks this toolchain; use the
local CLI for the first test.

Add this to your existing device YAML:

```yaml
packages:
  matter_test: github://seb5594/libretiny/experiments/esphome-matter/examples/existing-device-matter-package.yaml@matter-v0.1.0-alpha.1
```

Add a valid setup PIN to your existing `secrets.yaml`:

```yaml
matter_passcode: 34567890
```

Choose your own PIN for actual devices. The example is public. The component rejects
Matter's prohibited PIN values. The package keeps your board, framework, Wi-Fi and
GPIO definitions. It enables IPv6, requires a valid IPv6 address and disables
ESPHome's mDNS responder. Use the **device IP address** for ESPHome API/OTA and logs;
`.local` discovery for the ESPHome service is unavailable in this release.

Check existing external-component definitions for duplicate entries. Validation
rejects an effective configuration with IPv6 disabled or ESPHome mDNS enabled.
The immutable release tag pins both the package and its component source.

Endpoint 1 is a **virtual light**. The package does not attach it to your existing
relays. Other device functions consume additional flash and heap; successful
minimal/profile builds do not prove that every existing application fits.

## Local controls and observable state

The package and the `*-matter-existing.yaml` examples reuse ESPHome's template
buttons and binary sensor. They are available through your existing ESPHome API:

| Entity | Behavior |
| --- | --- |
| Matter test light state | Reports the virtual light's current OnOff state |
| Matter open commissioning | Opens a fresh bounded basic commissioning window |
| Matter close commissioning | Closes an active commissioning window |
| Matter reset fabrics | Removes Matter fabrics and cluster state, then restarts Matter |

The reset button is **disabled by default**. Enable it explicitly in the ESPHome
integration before using it. It removes all Matter pairings and restores the
virtual light's defaults. It does not erase ESPHome preferences or Wi-Fi settings.
Remove the old device entry from the Matter controller before pairing again.

Controls require a started, connected server. A single pending command is allowed;
additional requests are rejected and logged instead of replacing the pending one.
An already-open commissioning window is reported rather than silently replaced.
A storage error during reset stops Matter without reporting successful completion.
Reset is not a transaction across multiple FlashDB keys; interruption during reset
may require retry/recovery. See [storage limitations](storage.md).

To use these controls in another configuration, include `examples/matter-controls.yaml`
and use `id: native_matter` for the server. The minimal standalone profiles omit API
and controls; reboot an unpaired device to reopen its initial window.

## Manual code and scannable QR code

The examples/package set `log_setup_codes: true`. On opening the initial window,
the firmware prints one matching pair to the UART/ESPHome log:

```text
Matter setup: manual=34970112332 qr=MT:-24J0AFN00KA0648G00
```

[QR image for this exact example configuration](../examples/matter-test-setup.svg).
This example corresponds to PIN `20202021`, discriminator `3840` and test VID/PID.
**The YAML PIN and the 11-digit manual Matter pairing code are different formats.**
Unmodified rs-matter codecs generate both codes from the configured values. The
manual code includes a check digit; the QR payload advertises IP discovery.

In a compatible Matter app, choose Add device / Enter code manually and enter the
11 digits following `manual=`. Generate a scannable image locally:

```sh
python tools/render_matter_qr.py --log boot.log --output matter-setup
# Or copy the actual MT: payload from your device:
python tools/render_matter_qr.py --payload 'MT:-24J0AFN00KA0648G00' --output matter-setup
```

The tool writes `.png`, `.svg` and `.html`. With `--log`, the printable HTML page
also shows the matching manual code. The tool rejects logs containing different
code pairs. It sends nothing to a QR website. Dependencies are included in
`requirements-dev.txt`; standalone installation is `pip install 'qrcode[pil]==8.2'`.

Set `log_setup_codes: false` to suppress setup codes (the component default).
Normal restarts of a paired device do not print them again. An explicit local
Open commissioning request may print them when that option is enabled.
Setup logs and QR files contain commissioning credentials: keep them private and
redact them before sharing logs. A QR image does not itself open a pairing window.

## Pair and operate

An unpaired device opens a 300-second window at startup. A paired device retains
its fabric and reconnects without opening a new window. Local controls can reopen
commissioning deliberately; support for every commercial ecosystem is not claimed.

Device and controller need IPv6/mDNS connectivity on the same LAN. Internet IPv6
and BLE are unnecessary for this flow. Client isolation or multicast filtering
can prevent discovery. The stack uses UDP/5540 and UDP/5353.

With an official `chip-tool` build and the unmodified examples:

```sh
chip-tool pairing onnetwork 305414945 20202021
chip-tool onoff on 305414945 1
chip-tool onoff read on-off 305414945 1
chip-tool onoff off 305414945 1
chip-tool onoff toggle 305414945 1
chip-tool onoff subscribe on-off 0 40 305414945 1
```

For `pairing onnetwork`, use the configured **PIN**, not the manual code.
`pairing onnetwork-long 305414945 <PIN> 3840` can select the discriminator; exact CLI
options depend on the chip-tool version. The automated tests use both QR and manual
code commissioning and verify attestation against the bundled public test PAA.

Controllers must accept development VID `0xFFF1`, PID `0x8001` and public test
attestation. Apple, Google, Alexa and Home Assistant acceptance has not been
verified on physical devices. Matter Wi-Fi scan/connect commands return
`InvalidAction`; ESPHome owns credentials. Updates use ESPHome/LibreTiny OTA or UART,
not the Matter OTA protocol.

## Logs and recovery

Expected first boot sequence:

1. ESPHome obtains Wi-Fi and a valid IPv6 address.
2. `Starting Matter: heap=... stack=24576 interface=...`
3. Version, memory sizes and `Loading fabric credentials and Matter state`.
4. `Matter endpoint ready; commissioned=false`.
5. Optional `Matter setup: manual=... qr=MT:...`.
6. `On-network commissioning open for 300 seconds`.
7. `Starting UDP/5540, mDNS, secure sessions and subscriptions`.
8. PASE, attestation, CSR, AddNOC, CommissioningComplete and OnOff changes.

Every 30 seconds: connection/state, current and minimum heap, slowest poll and
`stack_min_free`. Storage logs contain operation/key/size, not fabric secrets.
Rust logging is limited to INFO; examples enable ESPHome DEBUG. Setup credentials
are printed only when explicitly enabled.

Network/address changes close sessions and restart the stack with stored fabrics.
Loss of IPv6 pauses operation too. Repeated internal errors stop Matter while
ESPHome continues. The first valid IPv6 address and the STA IPv4 address are
advertised; multiple-prefix networks have not been tested.

Follow the [hardware procedure](first-hardware-test.md) and keep a full flash backup.
An interrupted write can damage settings and require UART recovery. Existing
application boot/restore behavior still applies when the device restarts.

## Independent interoperability tests

On an isolated Linux host with Python 3.12:

```sh
pip install -r tests/server/requirements.txt
# Install libnl-route-3-200 and libglib2.0 for your distribution.
# The native CHIP controller needs a writable /data directory on this test host.
python tools/fetch_flashdb.py
python tools/build_matter_host.py --build-dir .cache/server-build
python tools/test_matter_server.py \
  --binary .cache/server-build/matter-host \
  --build-dir .cache/interop-run-1 \
  --ipv4 <test-host-LAN-IPv4>
```

The output directory must be new to avoid overwriting commissioned state. Ports
5353/5540 must be available. The host advertises `::1` and its LAN IPv4, so the
controller and server run on the same machine. Tests use the actual server library,
UDP adapter and FlashDB on simulated NOR.

Tests cover onboarding codecs, incorrect PIN/check digit, QR/manual pairing,
attestation, read/write/invoke/subscribe, ACL/write protection, malformed UDP,
process/warm restart, local commissioning controls, Matter-only reset and injected
storage/entropy failures. The reset test also checks an ESPHome preference sentinel.
Physical power loss and Beken radio timing are separate hardware tests.

Only `summary.json` is suitable for public CI artifacts. Controller JSON, setup
logs and flash images contain development credentials and stay local.

Optional controller isolation uses `tests/server/Dockerfile`. Supply its BuildKit
`ca_bundle` secret with your environment's trusted CA bundle. Install the pinned
controller packages in a separate Python environment, then pass
`--controller-container <image>` and `--controller-site-packages <absolute-path>`
to the runner. Host networking is required for local mDNS/IPv6 interoperability.
The CI job uses its own Ubuntu runner and does not require this Docker path.

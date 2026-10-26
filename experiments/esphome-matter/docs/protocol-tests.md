# Test Matter codecs on existing ESPHome devices

This guide covers the separate codec self-tests. For the implemented network
server and pairing, see [the native Matter server](matter-server.md).

`libretiny_matter_protocol` builds original Matter codecs under LibreTiny and runs
the same 72 checks on the host and MCU. It writes and reads real **Interaction Model
payloads for Off, On and Toggle** using unmodified CHIP `InvokeRequestMessage`,
`CommandDataIB`, `CommandPathIB`, `TLVReader` and `TLVWriter` classes.

**The codec component alone is not a commissionable Matter device.** It checks
payload processing, without encrypted UDP, PASE/CASE, controller connections, mDNS,
access control, subscriptions or relay mapping. It opens no port and writes no
flash test data. Those protocol services belong to the separate native server.

## Add to an existing configuration

Add this package to your device YAML. If `packages:` already exists, add only the
new entry beneath it:

```yaml
packages:
  matter_protocol_test: github://seb5594/libretiny/experiments/esphome-matter/examples/existing-device-package.yaml@matter-v0.1.0-alpha.1
```

The package adds external components and a diagnostic button. It does not set a
board, GPIOs, Wi-Fi credentials or framework source. Your existing configuration
can continue using **official LibreTiny**. Tested versions are ESPHome 2026.10.0
and LibreTiny 1.14.0. The release tag pins package and component sources.

1. Compile your device YAML and check the reported flash usage.
2. Install using the device's established ESPHome procedure.
3. Open the ESPHome log after reboot. Expected output:

   ```text
   [libretiny_matter_protocol] Matter codec tests: passed=72 failed=0 duration=... ms
   ```

4. Run the **Matter codec self-test** diagnostic button to repeat the checks. It is
   available through an existing ESPHome API. Without API, the boot test still logs.

Failures report their count and the first failed check. Include chip/module,
ESPHome/LibreTiny versions, this message, duration and build memory figures when
reporting results. Passing confirms codecs for that build, not pairing or radio
operation. Set `libretiny_matter_protocol: {run_on_boot: false}` to disable the boot
test while keeping the button.

## Standalone profiles

These examples explicitly use official LibreTiny 1.14.0:

```sh
esphome compile examples/bk7231n-protocol.yaml
esphome compile examples/bk7238-t1-protocol.yaml
esphome compile examples/ln882h-protocol.yaml
esphome compile examples/rtl8720cf-protocol.yaml
```

Use the package above for an existing relay/light: these minimal profiles contain
only diagnostics, not the device's own controls. Codec tests do not need IPv6, so
LN882H/RTL8720CF can run them despite their current ESPHome IPv6 limitation.

## Coverage

- Original CHIP builders serialize Off/On/Toggle and compare every byte against
  fixed vectors. Original parsers read endpoint 1, OnOff cluster `0x0006`, command
  and Interaction Model revision back.
- Every one of 29 truncation points and each undersized output buffer is tested,
  including guard bytes before and after the buffer.
- Wrong root type, trailing TLV and unsupported timed requests are rejected.
  Additional host cases cover missing path fields/revision and wrong field types.
- Independent TLV vectors cover 64-bit boundaries, byte order, integer narrowing,
  a truncated byte string and UTF-8 output.
- Host checks use AddressSanitizer/UndefinedBehaviorSanitizer and 20,000 reproducible
  random parser inputs. This is bounded fuzzing, not complete Matter conformance.

`protocol_codec.cpp` contains limited test/inspection helpers for one untimed OnOff
invoke with empty command fields and at most 512 bytes. They do not validate every
schema rule or authorization and must not become network command handlers. The
native server uses upstream rs-matter's sessions/exchanges/Interaction Model.

## Sources and reuse

`components/libretiny_matter/vendor/connectedhomeip` contains only required upstream
sources/headers, including the `nLIO` byte-order dependency. Both are Apache-2.0;
licenses, copyrights, commits and checksums remain intact. Test sources are local.
The storage adapter uses the same CHIP definitions.

Pinned CHIP commit `4b4a3e405882afaf8161c0e4af244e65fa8f3f94` encodes Interaction
Model revision 12. This is not a certified port release. Do not link this codec
subset alongside another differently configured C++ CHIP library. The Rust server
and C++ codec tests have separate roles, explained in [architecture.md](architecture.md).

```sh
esphome compile tests/protocol-host.yaml
pytest -q tests/test_protocol.py
```

The [first hardware test](first-hardware-test.md) now covers the native server,
including pairing and real controller commands for a virtual light.

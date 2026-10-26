# Hardware feasibility and sources

Investigated October 24–26, 2026. A successful build does not prove radio operation,
pairing on hardware, interoperability with every ecosystem or certification.

## Per-MCU decision

| MCU | Zigbee without extra radio hardware | Matter over Wi-Fi | Next technical gate |
| --- | --- | --- | --- |
| BK7231N | No documented IEEE 802.15.4 radio | Native rs-matter server built; host interoperability passes | Measure radio behavior, runtime heap/stack and TRNG on hardware |
| BK7238 / Tuya T1 | No documented IEEE 802.15.4 radio | Same native server built using official LibreTiny | Hardware measurements; BLE/Wi-Fi coexistence separately |
| LN882H | No documented IEEE 802.15.4 radio | Wi-Fi hardware is suitable in principle; server port pending | End-to-end IPv6 support through ESPHome/LibreTiny |
| RTL8720C | No documented IEEE 802.15.4 radio | Wi-Fi hardware is suitable in principle; server port pending | IPv6, limited RAM and exact CF/CM/flash variant |

“LN88H” is interpreted as LN882H. RTL builds use `generic-rtl8720cf-2mb-992k`;
this does not validate all RTL8720C modules.

Zigbee requires IEEE 802.15.4 PHY/MAC. Wi-Fi and BLE share the 2.4-GHz band but use
different modulation and packet formats. The investigated chips/SDKs have no
documented usable 802.15.4 support. Porting ESPHome's Zigbee component cannot supply
that radio capability. An external 802.15.4 NCP would be a separate hardware project
requiring a backend driver. Matter-over-Thread has the same radio requirement.

Matter-over-Wi-Fi uses IPv6 over Wi-Fi. BLE may assist initial provisioning but is
not needed for IP operation. This first port commissions through an already
configured LAN. The controller must support that flow and development attestation.

## Beken is not one uniform platform

BK7231N and BK7238 use ARM968E-S / ARMv5TE builds in LibreTiny. A generic statement
that Beken supports Matter is insufficient:

- The official [connectedhomeip Beken example][chip-example] targets BK7235 with
  Armino and a RISC-V toolchain. It is not a drop-in for BK7231N/T1.
- The [Sparkleiot BK7238 plug SDK][plug-sdk] contains `src/platform/Beken`, ARM968E-S
  flags, FreeRTOS and an On/Off plug example. It is a useful porting reference.
  BK7231N entries in `build.sh` alone do not prove a working build for that chip.
- That SDK includes prebuilt libraries and Quectel adaptations (`libqladapt.a`,
  Git LFS). Apache notices in CHIP do not automatically license every other file
  or binary. This experiment does not copy or distribute that third-party SDK.
- Its [2-MB partition table][sdk-flash] differs from [LibreTiny Tuya T1][t1-flash]
  in application, OTA, RF and Matter data regions. Porting requires adapting
  interfaces, not flashing the complete SDK image.

## Memory and SDK details

[LibreTiny's Beken base profile][bk-board] lists 2 MiB flash and 256 KiB RAM. Its
logical application limit is 1,083,136 bytes, not 2 MiB. CRC blocks, bootloader,
OTA download and calibration/data regions must be considered separately. T1 has
its own physical layout.

On BK7231N, the [heap implementation][bk-heap] ends the dynamic region at 192 KiB
of normal RAM; TCM regions are separate. Subtracting linker RAM from 256 KiB does
not give available heap. Wi-Fi, lwIP and RTOS also allocate at runtime. These
figures alone cannot establish a safe maximum Matter endpoint count.

ESPHome selects BDK 3.0.33 for BK7231N and 3.0.78 for BK7238. The platform backend
must accommodate both. The examined [ESPHome BLE component][ble] supports BK7231N
but blocks BK7238 because its SDK hangs when enabling Wi-Fi STA alongside BLE.
Physical BLE support therefore does not imply working Matter BLE provisioning.
The minimal profiles let ESPHome disable unused BLE.

Entropy needs particular care:

- [LibreTiny `lt_rand_bytes()`][random] uses `rand()`.
- BDK 3.0.33's [mbedTLS hardware adapter][old-entropy] also uses `rand()`.
- Its [TRNG driver][old-trng] exists, but `BkRandomNumberRead()` fills at most one
  word per call. Passing a larger buffer would leave it incomplete.
- BDK 3.0.78 has another adapter and revised driver. A promising function name
  does not establish correct initialization, error handling or entropy quality.

The server therefore calls the TRNG device directly, word by word, with error and
repetition checks. Host tests cover complete buffers, boundaries and failures.
There is no `rand()` fallback. Actual entropy quality and Beken initialization
still need hardware checks. Protocol and cryptography come from rs-matter/RustCrypto.

## Reusing ESPHome patterns

[DavidvtWout/esphome-matter][esphome-matter] is a useful reference for endpoint
mapping and YAML, but depends on `esp-matter`/ESP-IDF. Allowing another platform name
in validation would not create a LibreTiny port. Its examined README also does not
present BLE pairing as the working default path.

The native [ESPHome Zigbee component][zigbee] separates ESP32 and Zephyr backends.
That pattern suits a platform-neutral Matter component: common schema/entity
mapping and separate platform integration. Zigbee and Matter clusters are still
not interchangeable binary interfaces.

[ESPHome networking][network] enables IPv6 on Beken with LibreTiny 1.7.0 or later,
including `CONFIG_IPV6`. LN882H and RTL87xx are not enabled there. This is a software
limitation in the investigated versions, not a physical impossibility of IPv6.

## Assessment

A small native Matter-over-Wi-Fi server with one initial OnOff endpoint and existing
LAN provisioning is the most promising route for unmodified devices. BK7231N remains
the priority; BK7238/T1 is the second build target. The server fits both normal app
partitions and pairs with an independent CHIP controller on the host. Confirming
that it works on devices still requires measurements during PASE/CASE on ARM9.

An external local Matter bridge could also expose ESPHome devices to several
ecosystems, but would require additional running infrastructure. This work implements
the requested native port.

[bk-board]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/boards/_base/beken-72xx.json
[bk-heap]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/cores/beken-72xx/base/api/lt_mem.c
[t1-flash]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/boards/_base/beken-7238-tuya.json
[random]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/cores/common/base/api/lt_utils.c
[old-entropy]: https://github.com/libretiny-eu/framework-beken-bdk/blob/841343e64d7584a3a650222ab0b6ea967755352a/beken378/func/mbedtls/mbedtls-port/src/tls_hardware.c
[old-trng]: https://github.com/libretiny-eu/framework-beken-bdk/blob/841343e64d7584a3a650222ab0b6ea967755352a/beken378/func/user_driver/BkDriverRng.c
[ble]: https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/bk72xx_ble/__init__.py
[network]: https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/network/__init__.py
[zigbee]: https://github.com/esphome/esphome/blob/c2911a51bd60f45b6cef72c27f8d878e4981b2d8/esphome/components/zigbee/__init__.py
[esphome-matter]: https://github.com/DavidvtWout/esphome-matter
[chip-example]: https://github.com/project-chip/connectedhomeip/blob/4b4a3e405882afaf8161c0e4af244e65fa8f3f94/examples/lighting-app/beken/README.md
[plug-sdk]: https://github.com/Sparkleiot/matter_bk7238_plug_sdk/tree/c10b9a828e4e85d34c0b49705451cf5d9b054752
[sdk-flash]: https://github.com/Sparkleiot/matter_bk7238_plug_sdk/blob/c10b9a828e4e85d34c0b49705451cf5d9b054752/beken378/func/user_driver/BkDriverFlash.c

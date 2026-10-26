# First hardware test: 0.1.0-alpha.1

This release is a software-verified candidate for a controlled **BK7231N** test,
followed by **BK7238/Tuya T1**. Passing this procedure on physical hardware is the
next release gate. It exposes a virtual light, not an actual relay.

## Prepare once

1. Identify the chip, module, flash size and board profile. Back up the full flash,
   including RF calibration data, and confirm that UART recovery is available.
   Never connect an unisolated mains-powered device to a computer/UART adapter.
2. Use the `matter-v0.1.0-alpha.1` tag. Start with a spare device and a test network
   with IPv6 link-local connectivity and multicast between controller and device.
3. Follow the [build guide](matter-server.md). For an existing ESPHome device,
   import the release package and retain its known board, Wi-Fi and pin definitions.
   Do not use a generic example's GPIO configuration as a replacement for your device.
4. Build locally. Confirm the selected chip/profile and a successful flash-size check.
   Flash using your established LibreTiny/ESPHome procedure, keeping UART logs from boot.

## Test sequence and acceptance criteria

| Step | Action | Expected evidence |
| --- | --- | --- |
| Boot | Start with Wi-Fi available | Version `0.1.0-alpha.1`, valid IPv6, Matter ready, no reset loop |
| Pair | Use the device's logged manual code or locally rendered QR | Controller completes commissioning; no storage/entropy failure |
| Operate | On, Off, Toggle; read the attribute | Controller, UART OnOff log and optional ESPHome diagnostic entity agree |
| Subscribe | Subscribe, then change the light | Controller receives a changed attribute report |
| Persist | Set On; wait at least 4 seconds; reboot | Existing fabric works via CASE without re-pairing; OnOff remains On |
| Network | Interrupt Wi-Fi, restore it, then operate again | Server resumes using stored fabric; no accumulating failures |
| Controls | Open and close commissioning through ESPHome | Explicit open/close logs; no device reboot required |
| Reset | Explicitly enable and press Matter reset fabrics | Matter state clears; ESPHome API/Wi-Fi remain; fresh pairing works |
| Observe | Run for at least 30 minutes with periodic commands | Record heap minimum, stack minimum and slowest poll; no repeated resets |

Do not interpret host timings or linker RAM as measured device headroom. Record the
lowest heap/stack values during commissioning and recovery. If startup reports
insufficient heap, the TRNG check fails, or the device resets repeatedly, stop that
profile's test and preserve the logs; do not remove the checks to force it to run.

## Report a result

Fill in [hardware-test-report.md](hardware-test-report.md). Redact the complete
`Matter setup:` line, Wi-Fi passwords, controller credentials and flash dumps before
sharing. Keep unredacted logs privately if they are needed for local QR generation.

A successful run establishes this particular module/controller/network combination.
It does not establish certification, all ecosystem compatibility, safe power-loss
recovery, arbitrary entity counts or reliable 24-hour operation. Those are later gates.

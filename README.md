# NES D-Pad Kindle Remote

A custom miniature Bluetooth Low Energy (BLE) page-turner remote compatible with Amazon Kindle (Kindle Scribe, Colorsoft, Paperwhite 12th Gen, and Kindle devices running firmware 5.20.1+). Built using a **Seeed Studio XIAO nRF52840** running **ZMK Firmware**, designed to fit inside a Nintendo NES-style D-pad gashapon keychain capsule.

---

> **Disclaimer:** This project is an independent open-source hardware and software project created strictly for personal interoperability and educational purposes. It is **not affiliated with, sponsored by, or endorsed by Amazon.com, Inc.** All trademarks, product names, and company names belong to their respective owners.

---

## Overview & Architecture

Kindle firmware 5.20.1+ supports wireless page-turner accessories. This project implements the standard BLE services and HID specifications required for seamless interoperability with stock Kindle devices without jailbreaking or modifications.

- **Microcontroller:** Seeed Studio XIAO nRF52840 (plain, non-Sense)
- **Firmware:** [ZMK Firmware](https://zmk.dev/) with Kindle advertising data patch
- **Battery:** 3.7V 100 mAh LiPo (401230 cell) with onboard battery reporting
- **Enclosure:** Gashapon NES D-pad keychain capsule (internal cavity ~35 × 32 × 7 mm with 3D-printed PETG insert)
- **Contacts:** Direct carbon pill contacts over copper tape pads

---

## Compatibility & BLE Specifications

To pair and operate with compatible Kindle e-readers:

1. **Advertising Data (AD Type `0xFF`):** Must carry Amazon's Manufacturer Specific Data `0x0171` with Product ID `0x0443` in the primary advertising packet (`71 01 04 43`).
2. **Device Information Service (`0x180A`):**
   - **PnP ID (`0x2A50`):** Vendor ID `0x0171`, Product ID `0x0443`, Version `0x0100`, Vendor ID Source `0x01` (Bluetooth SIG).
   - **Firmware Revision (`0x2A26`):** `"1.0.0"`
   - **Manufacturer Name (`0x2A29`):** `"Amazon"`
   - **Model Number (`0x2A24`):** `"Kindle Remote"`
3. **HID Service (`0x1812`):**
   - **Page Down / Next:** Keyboard usage `0x4E`
   - **Page Up / Previous:** Keyboard usage `0x4B`
   - **Brightness Up:** Consumer usage `0x006F`
   - **Brightness Down:** Consumer usage `0x0070`
   - **Sleep / System Power:** Consumer usage `0x0030`
4. **Battery Service (`0x180F`):** Standard battery level notification (`0x2A19`). The Kindle displays battery percentage under Settings > Accessories, warning at `< 20%` and critically at `< 5%`.

---

## D-Pad Key Mapping

| Button | Function | Kindle HID Event | Behavior |
|---|---|---|---|
| **D_RIGHT** | Page Forward | `KEY_PAGEDOWN` (`0x4E`) | Tap to turn page |
| **D_LEFT** | Page Backward | `KEY_PAGEUP` (`0x4B`) | Tap to turn page back |
| **D_UP** | Brightness Up | Consumer `0x006F` | Tap or hold to increase brightness |
| **D_DOWN** | Brightness Down / Sleep | Consumer `0x0070` / `0x0030` | Tap: Brightness down. Hold 1.5s: Kindle sleep |
| **D_UP + D_RIGHT** (diagonal) | Forget paired Kindle | Bluetooth bond clear (no key sent) | Hold 5 s to clear the stored pairing so a different Kindle can pair (see Pairing) |

### Wiring and custom pins

Each pad shorts its XIAO pin to GND (internal pull-ups, no resistors needed). Default pins: D_RIGHT `D0`, D_UP `D1`, D_DOWN `D3`, D_LEFT `D5`.

The pins are user-configurable. To use different pins, edit `input-gpios` in `zmk-config/config/xiao_ble.overlay` (any free XIAO pin `D0`-`D10`, e.g. `<&xiao_d 2 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>`). Keep the entry order (D_RIGHT, D_UP, D_DOWN, D_LEFT), because the keymap `bindings` in `xiao_ble.keymap` follow that order; edit the bindings to change what each button sends.

---

## Building the Firmware

Firmware builds use [ZMK Firmware](https://zmk.dev/) on top of Zephyr RTOS.

### Prerequisites & Toolchain
- Python 3.10+ and CMake
- Zephyr SDK 0.17.0 with `arm-zephyr-eabi` cross-toolchain (do not use generic system `arm-none-eabi-gcc`).
  - *Note for CMake 4.x users:* If Zephyr SDK discovery fails, ensure `ZEPHYR_TOOLCHAIN_VARIANT=zephyr` and `ZEPHYR_SDK_INSTALL_DIR` are exported before running CMake.

### Workspace Setup & Build
1. Initialize the local ZMK workspace and toolchain:
   ```bash
   cd zmk-config
   ./setup_workspace.sh
   ```
2. Build the firmware:
   ```bash
   ./build.sh
   ```
   This compiles `kindle_remote.uf2` in `zmk-config/`. On macOS, if the board is mounted in bootloader mode, `build.sh` automatically flashes the UF2 to the drive.

### Flashing
1. Connect the Seeed XIAO nRF52840 via USB-C.
2. Double-tap the small reset button to enter the UF2 bootloader (mounts as `XIAO-NRF52` on plain XIAO or `XIAO-SENSE` on Sense boards).
3. Copy `kindle_remote.uf2` to the drive. The device automatically flashes and reboots.

---

## Pairing

1. On the Kindle, open the Bluetooth / accessories settings and start pairing a page turner. Press any pad on the remote to wake it if it was asleep.
2. The remote is listed (the Kindle may show it as "Kindle Click").
3. **Re-pairing the same Kindle** after removing the remote on the Kindle works directly.
4. **Pairing a different Kindle:** the remote remembers one Kindle. Hold **D_UP + D_RIGHT** (up-right diagonal) for 5 seconds to clear it, then pair the new Kindle. If the old Kindle still lists the remote, remove it there too.

---

## Known Behaviors & Limitations

- **Kindle Wake:** The remote cannot wake a sleeping Kindle. Kindle OS 5.20.1 power management does not designate page-turner virtual input devices as wakeup sources. Wake the device with its physical power button.
- **Deep Sleep:** After 60 minutes without a press, the remote powers down to save battery. The next press wakes it but is not sent; the remote reconnects within ~0.3-3 s, then presses work normally. Change `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT` in `zmk-config/config/xiao_ble.conf` to adjust.
- **Battery Reporting:** The Kindle's low (< 20%) and critical (< 5%) warnings were verified using simulated battery levels sent from the ESP32 test harness. The ZMK build reports the XIAO's real cell level through the same Battery Service.

---

## Hardware & Assembly Safety

- **LiPo Battery Safety:** Always verify battery polarity with a digital multimeter before soldering to the XIAO pads (JST connector polarities frequently vary across vendors). Insulate the PCM and solder joints with Kapton tape. Never short battery leads.
- **Enclosure Clearance:** Ensure battery leads and pad wires do not pinch against the PETG carrier walls or casing screws.
---

## Repository Map

- `zmk-config/`: Production ZMK configuration, keymap, and advertising patch.
- `protocol_test/`: ESP32-S3 PlatformIO harness used to validate the protocol against a Kindle before the ZMK build.
- `HARDWARE.md`: Bill of materials, hardware components, tools, and assembly instructions (contains Amazon affiliate links).
- `PROTOCOL_SPEC.md`: Architectural findings and interoperability specifications for Kindle OS input management.
- `FINDINGS.md`: Complete hardware research log, protocol benchmarks, power consumption budget, and test results.
---

## Acknowledgments

- [ZMK Firmware](https://zmk.dev/) (MIT) — keyboard firmware the remote runs on; `zmk-config/patches/` modifies it.
- [Zephyr RTOS](https://www.zephyrproject.org/) (Apache-2.0) and the Zephyr SDK — RTOS, Bluetooth stack and toolchain under ZMK.
- [Seeed Studio XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) — board and its Zephyr board support.
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) by h2zero (Apache-2.0) — BLE stack for the ESP32-S3 protocol test harness.
- [PlatformIO](https://platformio.org/) — builds the test harness.
- [Bleak](https://github.com/hbldh/bleak) (MIT) — BLE scanning and GATT reads in `protocol_test/tools/ble_scan.py`.
- [KindleTool](https://github.com/NiLuJe/KindleTool) by NiLuJe (GPL-3.0) — unpacking the Kindle firmware update for interoperability analysis.
- [Capstone](https://www.capstone-engine.org/), [pyelftools](https://github.com/eliben/pyelftools) and [7-Zip](https://www.7-zip.org/) — disassembly and image extraction during analysis.

No code from these projects is vendored here except the ZMK patch, which is a diff against ZMK.

---

## License

This project is licensed under the [MIT License](LICENSE).

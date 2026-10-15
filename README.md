# Kindle Compatible D-Pad Remote

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

Default wiring (each pad shorts its pin to GND; internal pull-ups, no resistors needed):

| Pad / Pin | Function | Kindle HID Event | Behavior |
|---|---|---|---|
| **D0** (Right) | Page Forward | `KEY_PAGEDOWN` (`0x4E`) | Tap to turn page |
| **D5** (Left) | Page Backward | `KEY_PAGEUP` (`0x4B`) | Tap to turn page back |
| **D1** (Up) | Brightness Up | Consumer `0x006F` | Tap or hold to increase brightness |
| **D3** (Down) | Brightness Down / Sleep | Consumer `0x0070` / `0x0030` | Tap: Brightness down. Hold 1.5s: Kindle sleep |

The pins are user-configurable. To use different pins, edit `input-gpios` in `zmk-config/config/xiao_ble.overlay` (any free XIAO pin `D0`-`D10`, e.g. `<&xiao_d 2 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>`). Keep the entry order (forward, brightness up, dim/sleep, back), because the keymap `bindings` in `xiao_ble.keymap` follow that order; edit the bindings to change what each position sends.

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

## Known Behaviors & Limitations

- **Kindle Wake:** The remote cannot wake a sleeping Kindle. Kindle OS 5.20.1 power management does not designate page-turner virtual input devices as wakeup sources. Wake the device with its physical power button.
- **Battery Reporting:** The Kindle's low (< 20%) and critical (< 5%) warnings were verified using simulated battery levels sent from the ESP32 test harness. The ZMK build reports the XIAO's real cell level through the same Battery Service.

---

## Hardware & Assembly Safety

- **LiPo Battery Safety:** Always verify battery polarity with a digital multimeter before soldering to the XIAO pads (JST connector polarities frequently vary across vendors). Insulate the PCM and solder joints with Kapton tape. Never short battery leads.
- **Enclosure Clearance:** Ensure battery leads and pad wires do not pinch against the PETG carrier walls or casing screws.
---

## Repository Map

- `zmk-config/`: Production ZMK configuration, keymap, and advertising patch.
- `protocol_test/`: ESP32-S3 PlatformIO harness used to validate the protocol against a Kindle before the ZMK build.
- `HARDWARE.md`: Bill of materials, hardware components, tools, and assembly instructions.
- `PROTOCOL_SPEC.md`: Architectural findings and interoperability specifications for Kindle OS input management.
- `FINDINGS.md`: Complete hardware research log, protocol benchmarks, power consumption budget, and test results.
---

## License

This project is licensed under the [MIT License](LICENSE).

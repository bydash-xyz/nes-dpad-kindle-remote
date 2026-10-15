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

| Pad / Pin | Function | Kindle HID Event | Behavior |
|---|---|---|---|
| **D0** (Right) | Page Forward | `KEY_PAGEDOWN` (`0x4E`) | Tap to turn page |
| **D5** (Left) | Page Backward | `KEY_PAGEUP` (`0x4B`) | Tap to turn page back |
| **D1** (Up) | Brightness Up | Consumer `0x006F` | Tap or hold to increase brightness |
| **D3** (Down) | Brightness Down / Sleep | Consumer `0x0070` / `0x0030` | Tap: Brightness down. Hold 1.5s: Kindle sleep |

---

## Building the Firmware

Firmware builds are powered by ZMK and Zephyr. A local build script is provided in `zmk-config/`.

### Prerequisites
- POSIX shell with Python 3, `west`, and CMake (or Docker/GitHub Actions)
- Arm GNU Toolchain (`arm-none-eabi-gcc`)

### Local Build
```bash
cd zmk-config
./build.sh
```
This generates `zmk-config/kindle_remote.uf2`.

### Flashing
1. Connect the Seeed XIAO nRF52840 via USB-C.
2. Double-tap the onboard reset button to enter the UF2 bootloader (mounted as `XIAO-SENSE` or `XIAO-NRF52`).
3. Copy `kindle_remote.uf2` to the drive. The board flashes and reboots automatically.

---

## Hardware & Assembly Safety

- **LiPo Battery Safety:** When soldering pouch cells directly to the board, always insulate the PCM and battery leads with Kapton tape. Never short battery terminals.
- **Enclosure Fit:** Ensure battery leads and switch wiring do not pinch against the PETG carrier walls or casing screws.

---

## Repository Map

- `zmk-config/`: Production ZMK configuration, keymap, and advertising patch.
- `protocol_test/`: Standalone PlatformIO harness used on ESP32-S3 to test and validate BLE compatibility.
- `FINDINGS.md`: Complete hardware research log, protocol benchmarks, power consumption budget, and test results.
- `PROTOCOL_SPEC.md`: Architectural findings and interoperability specifications for Kindle OS input management.
- `legacy/`: Superseded ESP32-C3 firmware prototype and initial hardware guide.
---

## License

This project is licensed under the [MIT License](LICENSE).

> **Superseded (2026-10-08):** this is the ESP32-era document with an incorrect enclosure size. See `CLAUDE.md` and `FINDINGS.md` for the current plan (XIAO nRF52840 + ZMK, 35 x 32 x 7 mm insert).

# ESP32 Kindle Scribe / Colorsoft Bluetooth Remote

Custom miniature Bluetooth Low Energy (BLE) page-turner remote for Amazon Kindle (Kindle Scribe, Colorsoft, Paperwhite 12th Gen, and FW 5.20.1+), designed to fit inside a Nintendo Gashapon NES Controller D-pad capsule toy enclosure (`50 mm × 50 mm × 20 mm`).

---

## Key Reverse-Engineering Discoveries

By extracting and reverse-engineering the Amazon firmware bundle `update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin`, we identified the internal requirements for native Kindle remote compatibility:

1. **Native Allowlist & Match Criteria (`devices.json` & `input_manager`):**
   - The Kindle OS admits devices into the `page_turner` category only if they match:
     - **Vendor ID (VID):** `0x0171` (Amazon.com, Inc.)
     - **Product ID (PID):** `0x0443` ("Amazon Kindle Remote")
   - Any device not presenting this VID/PID tuple is logged as `device rejected (not in allowlist)` and ignored for page turning.
2. **Device Discovery & Classification (`acsbtfd` / `BtDiscoveryFilter`):**
   - The Bluetooth stack filters for devices exposing the **Human Interface Device** service (`0x1812`) and **Device Information Service** (`0x180A`).
   - The PnP ID characteristic (`0x2A50`) must report VID `0x0171` and PID `0x0443`.
   - The Firmware Revision characteristic (`0x2A26`) is explicitly queried by `acsbtfd` to display the remote firmware version.
   - The Battery Service (`0x180F`) reports the remote battery percentage. Low battery warnings trigger at `< 20%`, critical warnings at `< 5%`.
3. **HID Input Mappings (`categories.json`):**
   - **Page Forward:** `KEY_PAGEDOWN` (HID Keyboard Usage `0x4E`)
   - **Page Backward:** `KEY_PAGEUP` (HID Keyboard Usage `0x4B`)
   - **Brightness Up:** Consumer Control Usage `0x006F` (`display.brightness_up`)
   - **Brightness Down:** Consumer Control Usage `0x0070` (`display.brightness_down`)
   - **Sleep / Power:** Consumer Control Usage `0x0030` (`system.sleep`)
   - **Orientation Swap:** Kindle automatically swaps forward and back buttons when inverted 180°.

Detailed technical breakdown and disassembled routines: [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)

---

## Hardware Architecture

### Recommended Bill of Materials (BOM)

| Component | Part / Model | Dimensions | Notes |
|---|---|---|---|
| **Enclosure** | Nintendo Capsule Toy NES Controller D-Pad Keypad | `50 × 50 × 20 mm` | Sourced from eBay / Japan Gashapon |
| **Microcontroller** | **Seeed Studio XIAO ESP32-C3** | `21.0 × 17.5 × 3.5 mm` | BLE 5.0, ~44 µA deep sleep, onboard LiPo charger |
| **Battery** | 3.7V LiPo Pouch Cell (Model **502030**) | `30.0 × 20.0 × 5.0 mm` | ~250–300 mAh capacity (~7 months standby) |
| **Switches** | 4× Low-profile SMD Tactile Switches | `3.0 × 4.0 × 2.0 mm` | Positioned under D-pad arms |
| **Carrier PCB** | Custom perfboard / prototype substrate | `35 × 35 × 1.0 mm` | Holds switches under silicone membrane |

Hardware wiring schematic, pinouts, and step-by-step mechanical guide: [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md)

---

## D-Pad Control Layout

```
               [ UP ]
           Brightness Up
                 ▲
   [ LEFT ]      │      [ RIGHT ]
   Page Back ◄───┼───► Page Forward
                 │
                 ▼
              [ DOWN ]
          Brightness Down

  [ HOLD UP + DOWN for 1.5s ]  -->  Kindle Sleep / Wake
```

---

## Building and Flashing

### Option 1: PlatformIO (Recommended)
1. Install PlatformIO in VS Code or CLI.
2. In this directory, run:
   ```bash
   pio run -e seeed_xiao_esp32c3 -t upload
   ```

### Option 2: Arduino IDE
1. Install **ESP32 by Espressif Systems** via Board Manager.
2. Install **NimBLE-Arduino by h2zero** via Library Manager.
3. Select board: **XIAO_ESP32C3**.
4. Open `src/main.cpp` (or rename to `.ino`) and click **Upload**.

---

## Pairing with your Kindle

1. Power on the remote (or wake via any button press).
2. On your Kindle (FW 5.20.1+), open **Settings > Wi-Fi & Bluetooth > Bluetooth Devices**.
3. Put Kindle in pairing mode; select **Amazon Kindle Remote**.
4. Once paired, the Kindle popup will announce: `Amazon Kindle Remote connected` and unlock remote page turn options under **Settings > Accessories**.

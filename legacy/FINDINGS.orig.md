# Kindle Scribe / Colorsoft Bluetooth Remote: Comprehensive Technical Findings

**Investigation Date:** October 2026  
**Target Firmware:** `update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin` (FW 5.20.1, Build 4893920050)  
**Target Platform:** Amazon `Platcs8` (Kindle Scribe Colorsoft 2025 platform, Variants: 4VX, 4PF, 4PH, 4F9, 4FB, 46P)  
**Hardware Form Factor Target:** Nintendo Gashapon NES Controller D-Pad Keypad (`50 mm × 50 mm × 20 mm`)

---

## 1. Context & Objective

Amazon introduced native Bluetooth Page Turner remote support in firmware version 5.20.1 for the Kindle Scribe and Kindle Colorsoft. Third-party generic Bluetooth presentation clickers, gamepads, and volume buttons frequently fail or are ignored by the Kindle UI. 

The goal of this investigation was to:
1. Deconstruct the official firmware update `update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin`.
2. Reverse engineer the Bluetooth stack, input pipeline, and user interface daemons to determine why generic remotes are rejected and what exact parameters the Kindle expects.
3. Determine how to build a fully compatible custom Bluetooth remote using an ESP32.
4. Select a hardware platform small enough to fit inside a miniature Nintendo controller keypad enclosure (`50 mm × 50 mm × 20 mm`).

---

## 2. Firmware Deconstruction Summary

* **Container Format:** The update file is an Amazon `SP01` (Signing Envelope) wrapping an `FB03` (Fullbin / Recovery V2) bundle targeting platform `Platcs8`.
* **Extraction:** Unpacked using `KindleTool v1.6.6` into constituent payload components:
  * `boot_images/`: U-boot, BL2, LK, TEE, Android-format boot image (`boot.img`).
  * `data.stgz`: Scrambled user data bundle.
  * `rootfs.img.gz`: Compressed ext4 filesystem image (`1.5 GB` uncompressed).
* **OS Architecture:** 32-bit ARM (ARMhf) Linux OS running kernel `6.6.77-lab126`.
* **Key Binaries Analyzed:**
  * `/usr/bin/input_manager`: Linux `evdev`/`udev` event router and allowlist classifier.
  * `/usr/bin/acsbtfd`: Amazon Common Software Bluetooth Framework Daemon (discovery & GATT host).
  * `/usr/lib/libbluetooth.so`: Fluoride / Bluedroid Bluetooth stack (`btif_hh` HID Host).
  * `/app/KPPPillow-2.0/bin/pillowd` & `/app/KPPPillow-2.0/locale/en/LC_MESSAGES/pillow.mo`: System UI notifications & setup popups.
  * `/app/KPPMainApp/`: React Native user settings interface (`libBluetoothSetup.so`).

---

## 3. Core Reverse-Engineering Discoveries

### 3.1 The Input Allowlist (`/etc/input_manager/devices.json`)
The fundamental reason generic Bluetooth remotes fail on modern Kindle firmware is the existence of a strict hardware allowlist inside `/usr/bin/input_manager`.

Inspection of `/etc/input_manager/devices.json` revealed:
```json
{
  "devices": [
    {
      "name": "Amazon Kindle Remote",
      "vid": "0x0171",
      "pid": "0x0443",
      "category": "page_turner",
      "match_type": "vid_pid"
    }
  ]
}
```

Disassembly and string analysis of `/usr/bin/input_manager`:
```
D def:input_manager:device_add:processing device: name='%s' vid=0x%04x pid=0x%04x fd=%d
D def:input_manager:device_add:device rejected (not in allowlist): name='%s'
D def:input_manager:device_add:classified '%s' -> category '%s'
I def:input_manager:device_add:registered category='%s'
```
* When any Bluetooth HID device pairs, the kernel `uhid` driver registers `/dev/input/eventX`.
* `input_manager` inspects the device via `udev`.
* If the Vendor ID and Product ID do **not** match `0x0171:0x0443`, the device is logged as `rejected (not in allowlist)` and all input events are discarded.
* If it matches **VID `0x0171`** (Amazon.com, Inc.) and **PID `0x0443`** ("Amazon Kindle Remote"), it is admitted to the `page_turner` input category.

---

### 3.2 Bluetooth Discovery & Classification (`/usr/bin/acsbtfd`)
The Bluetooth daemon `acsbtfd` enforces discovery filtering before devices are even paired:

1. **Policy Hierarchy:**
   The daemon supports two active device policies:
   * **Type 0 (`AudioSinkPolicy`):** Bluetooth audio devices (A2DP Profile, Service UUID `0x110B`).
   * **Type 1 (`BleHidPageTurnerPolicy`):** Remote page turners.

2. **Classification Logic (`BtDiscoveryFilter::Classify` at VMA `0x220a6` - `0x22800`):**
   * Disassembly reveals checks matching:
     - `I BtDiscoveryFilter:Classify:VP ID match: name=%P type=%d:`
     - `I BtDiscoveryFilter:Classify:UUID match: name=%P uuid=0x%04X type=%d:`
     - `D BtDiscoveryFilter:Classify:rejected: name=%P uuids=%zu enabledTypes=%zu:`
   * Device tables loaded at `0x1a23c`–`0x1aa68` register **VID `0x0171`** and **PID `0x0443`** as Type 1 (`pageTurner`).

3. **GATT Characteristic Inspection:**
   * At VMA `0x2f78a` / `0x3b60c`:
     - Disassembles `movw r3, #0x2a26` (Firmware Revision String) and `movw r2, #0x180a` (Device Information Service).
     - Kindle queries characteristic `0x2A26` and logs:
       `I BtService: [pageTurner] %P "%P" bat=%d fw=%s::`
   * Battery Service (UUID `0x180F`):
     - Kindle queries Battery Level characteristic (`0x2A19`).
     - Logs from `acsbtfd`:
       - `I BtService:OnHidBatteryNotify:addr=%P level=%d:`
       - `W BtService:OnHidBatteryNotify:LOW threshold crossed addr=%P prev=%d cur=%d:`
       - `E BtService:OnHidBatteryNotify:CRITICAL threshold crossed addr=%P prev=%d cur=%d:`
     - UI triggers: Low battery dialog at `< 20%`, Critical alert at `< 5%`.

---

### 3.3 Input Capability Table (`/etc/input_manager/categories.json`)
The mapping between Linux input keycodes and Kindle reading actions is defined in `/etc/input_manager/categories.json`:
```json
{
  "categories": {
    "page_turner": {
      "virtual_device": {
        "name": "kindle-input-pt",
        "vendor_id": "0x0001",
        "product_id": "0x0001"
      },
      "capability_table": [
        "KEY_PAGEDOWN",
        "KEY_PAGEUP",
        "KEY_BRIGHTNESSUP",
        "KEY_BRIGHTNESSDOWN",
        "KEY_POWER"
      ],
      "semantic_names": {
        "nav.page_forward": "KEY_PAGEDOWN",
        "nav.page_back": "KEY_PAGEUP",
        "display.brightness_up": "KEY_BRIGHTNESSUP",
        "display.brightness_down": "KEY_BRIGHTNESSDOWN",
        "system.sleep": "KEY_POWER"
      },
      "orientation_swap_pairs": [
        {
          "event1": "nav.page_forward",
          "event2": "nav.page_back",
          "swap_on": [180]
        }
      ]
    }
  }
}
```

* **Page Forward (`nav.page_forward`):** Linux `KEY_PAGEDOWN` (code 109) $\rightarrow$ Standard HID Keyboard Usage `0x4E`.
* **Page Backward (`nav.page_back`):** Linux `KEY_PAGEUP` (code 104) $\rightarrow$ Standard HID Keyboard Usage `0x4B`.
* **Brightness Up (`display.brightness_up`):** Linux `KEY_BRIGHTNESSUP` (code 225) $\rightarrow$ Consumer Usage `0x006F`.
* **Brightness Down (`display.brightness_down`):** Linux `KEY_BRIGHTNESSDOWN` (code 224) $\rightarrow$ Consumer Usage `0x0070`.
* **Kindle Sleep (`system.sleep`):** Linux `KEY_POWER` (code 116) $\rightarrow$ Consumer Usage `0x0030`.
* **Orientation Swap:** When the Kindle is rotated 180° (e.g. held upside-down), the kernel input pipeline automatically swaps `nav.page_forward` and `nav.page_back` so the forward button remains physically consistent.

---

### 3.4 Official Physical Remote Ergonomics (`pillow.mo`)
From the extracted localization catalog (`app/KPPPillow-2.0/locale/en/LC_MESSAGES/pillow.mo`):
* `pillow.pageTurnerPairedPopupTitle`: `"{0} connected"`
* `pillow.pageTurnerPairedPopupBullet1`: `"To turn pages, press the top or bottom button."`
* `pillow.pageTurnerPairedPopupBullet2`: `"To adjust brightness, use the side buttons."`
* `pillow.pageTurnerPairedPopupBullet3`: `"To put your Kindle to sleep, long press both side buttons at the same time."`
* `pillow.pageTurnerPairedPopupTopLeftButton`: `"Button 1, top face"` (Page Turn)
* `pillow.pageTurnerPairedPopupBottomLeftButton`: `"Button 2, bottom face"` (Page Turn)
* `pillow.pageTurnerPairedPopupTopRightButton`: `"Button 3, upper side"` (Brightness Up)
* `pillow.pageTurnerPairedPopupBottomRightButton`: `"Button 4, lower side"` (Brightness Down)

---

## 4. BLE Peripheral Protocol Specification

To build an ESP32 remote recognized by stock Kindle software without modifications or jailbreaking, the peripheral must implement:

| BLE Parameter | Specification | Purpose |
|---|---|---|
| **Advertised Device Name** | `Amazon Kindle Remote` | Display name in Kindle Bluetooth settings |
| **Appearance** | `0x03C1` (Keyboard) or `0x0180` (Remote Control) | Bluetooth SIG appearance |
| **Advertised Services** | `0x1812` (HID), `0x180F` (Battery), `0x180A` (DIS) | Triggers `acsbtfd` discovery filter |
| **Device Information (0x180A):** | | |
| - PnP ID (`0x2A50`) | `0x02, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01` | Source: USB-IF, VID: 0x0171, PID: 0x0443, v1.0.0 |
| - Firmware Revision (`0x2A26`) | `"1.0.0"` | Read by `acsbtfd` |
| - Manufacturer Name (`0x2A29`)| `"Amazon"` | Standard identification |
| - Model Number (`0x2A24`) | `"Kindle Remote"` | Standard identification |
| **Battery Service (0x180F):** | | |
| - Battery Level (`0x2A19`) | `0` – `100` (%) | Supports Read + Notify |
| **HID Service (0x1812):** | | |
| - Report ID 1 (Keyboard) | 8-byte standard report | `0x4E` (PageDown), `0x4B` (PageUp) |
| - Report ID 2 (Consumer) | 2-byte 16-bit usage report | `0x006F` (Bright+), `0x0070` (Bright-), `0x0030` (Sleep) |

---

## 5. Hardware Architecture & Build Plan

### 5.1 Enclosure Constraints
* **Enclosure:** Nintendo Game Controller Gashapon Capsule Toy (NES D-Pad Keychain).
* **Outer Dimensions:** `50 mm × 50 mm × 20 mm`.
* **Internal Usable Cavity:** Approx. `46 mm × 46 mm × 15 mm`.
* **Interface:** 4-way D-Pad (Up, Down, Left, Right).

### 5.2 Microcontroller: Seeed Studio XIAO ESP32-C3
* **Size:** **`21.0 mm × 17.5 mm × 3.5 mm`** — takes up only ~15% of the shell area.
* **Core:** ESP32-C3 32-bit RISC-V @ 160 MHz.
* **Radio:** Bluetooth 5.0 (BLE) with integrated ceramic antenna.
* **Power:** Onboard LiPo battery charger with direct solder pads (`BAT+` / `BAT-`) and USB Type-C port.
* **Deep Sleep:** Consumes **~44 µA** in deep sleep. Wakes up in **< 40 ms** via GPIO interrupt on button press.

### 5.3 Battery: 3.7V 250mAh LiPo (Model 502030)
* **Dimensions:** `30.0 mm × 20.0 mm × 5.0 mm`.
* **Capacity:** 250 mAh – 300 mAh.
* **Estimated Runtime:**
  - Standby (@ 44 µA): **~5,600 hours (~7.5 months)**.
  - Active (@ 15 mA for 30 ms per turn): **> 80,000 page turns** between charges.

### 5.4 D-Pad Button Mappings

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

   [ HOLD UP + DOWN for 1.5s ]  -->  Kindle Sleep
```

* **Right:** D0 (GPIO2) $\rightarrow$ `KEY_PAGEDOWN` (Page Forward)
* **Left:** D1 (GPIO3) $\rightarrow$ `KEY_PAGEUP` (Page Backward)
* **Up:** D2 (GPIO4) $\rightarrow$ Consumer `0x006F` (Brightness Up)
* **Down:** D3 (GPIO5) $\rightarrow$ Consumer `0x0070` (Brightness Down)
* **Up + Down (Simultaneous Hold 1.5s):** Consumer `0x0030` (Kindle Sleep)

---

## 6. Implementation Artifacts in Repository

| File | Description |
|---|---|
| `REVERSE_ENGINEERING.md` | In-depth technical breakdown with disassembly dumps of `acsbtfd` and `input_manager`. |
| `HARDWARE_GUIDE.md` | Full bill of materials, schematic, wiring table, and mechanical assembly instructions. |
| `src/main.cpp` | Complete, compilable Arduino/PlatformIO C++ source code utilizing `NimBLE-Arduino`. |
| `platformio.ini` | PlatformIO configuration for `seeed_xiao_esp32c3` and `esp32-c3-devkitm-1`. |
| `README.md` | Quickstart guide and pairing instructions. |
| `update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin` | Downloaded official Amazon firmware image (502 MB). |
| `extracted_fw/` | Extracted U-boot, kernel, and ext4 `rootfs.img`. |
| `extracted/` | Extracted configuration files, LIPC scripts, and binary samples. |

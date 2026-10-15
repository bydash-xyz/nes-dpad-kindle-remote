# Kindle Bluetooth Remote Interoperability Specification

## Target Platform
- **Reference Firmware:** Kindle OS 5.20.1+
- **Compatible Hardware:** Kindle Scribe, Kindle Colorsoft, Kindle Paperwhite (12th Gen+)

---

## 1. Executive Summary

Kindle OS 5.20.1+ includes native Bluetooth Page Turner remote support. This document specifies the exact Bluetooth Low Energy (BLE) profiles, identification data, and HID usage codes required for a custom wireless remote to pair and interact natively with stock Kindle devices without software modifications.

1. **Vendor ID & Product ID Validation:**
   - **Vendor ID (VID):** `0x0171` (Amazon.com, Inc.)
   - **Product ID (PID):** `0x0443` ("Amazon Kindle Remote")
   - Device Name: `"Amazon Kindle Remote"`
2. **Device Information Service (DIS 0x180A) & PnP ID:**
   - Must expose standard Bluetooth PnP ID characteristic (`0x2A50`) encoding VID `0x0171` and PID `0x0443`.
   - Firmware Revision characteristic (`0x2A26`) is explicitly queried by `acsbtfd` to display the remote firmware version.
3. **HID over GATT Profile (HOGP 0x1812):**
   - Must advertise Human Interface Device service (`0x1812`).
   - Standard HID reports for:
     - `KEY_PAGEDOWN` (Usage `0x4E`): Page Forward
     - `KEY_PAGEUP` (Usage `0x4B`): Page Backward
     - `KEY_BRIGHTNESSUP` (Consumer Usage `0x006F`): Increase brightness
     - `KEY_BRIGHTNESSDOWN` (Consumer Usage `0x0070`): Decrease brightness
     - `KEY_POWER` (Consumer Usage `0x0030`): Put Kindle to sleep
4. **Battery Service (BAS 0x180F):**
   - Reports battery percentage (`0x2A19`). The Kindle UI shows Low Battery when below 20% and Critical Battery when below 5%.

---

## 2. Firmware Deconstruction Methodology

The Amazon Kindle firmware was unpacked using `KindleTool` (v1.6.6):
```bash
kindletool convert -i update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin
kindletool extract update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin extracted_fw/
```
The package contains:
- `rootfs.img.gz`: Compressed ext4 filesystem image (1.5 GB uncompressed).
- `boot_images/`: U-boot, BL2, LK, TEE, and Android-format kernel `boot.img`.

Using `7zz`, the ext4 partition was inspected and targeted components were extracted for analysis.

---

## 3. Subsystem Analysis & Findings

### 3.1 Input Manager (`/usr/bin/input_manager`)
The input manager is an Amazon daemon that monitors `udev` for Linux `evdev` input devices (`/dev/input/event*`) created by the kernel `uhid` driver when a Bluetooth HID peripheral connects.

Inspecting `/etc/input_manager/devices.json`:
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

Critical observation in `/usr/bin/input_manager` binary strings:
```
D def:input_manager:device_add:processing device: name='%s' vid=0x%04x pid=0x%04x fd=%d
D def:input_manager:device_add:device rejected (not in allowlist): name='%s'
D def:input_manager:device_add:classified '%s' -> category '%s'
I def:input_manager:device_add:registered category='%s'
```
**Key Takeaway:** If a connected Bluetooth device does NOT match `vid=0x0171` and `pid=0x0443`, `input_manager` logs `device rejected (not in allowlist)` and ignores its keystrokes for page turning! Presenting Amazon's VID `0x0171` and PID `0x0443` is mandatory for plug-and-play operation.

### 3.2 Category & Event Mapping (`/etc/input_manager/categories.json`)
The `page_turner` category configuration reveals the input pipeline:
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

- Keystrokes for `KEY_PAGEDOWN` trigger `nav.page_forward`.
- Keystrokes for `KEY_PAGEUP` trigger `nav.page_back`.
- When the Kindle display is rotated 180 degrees (e.g., upside down), `input_manager` automatically swaps `nav.page_forward` and `nav.page_back` so the upper button remains page-forward.

### 3.3 Bluetooth Framework Daemon (`/usr/bin/acsbtfd`)
`acsbtfd` manages device discovery, connection, GATT inspection, and pairing.

Disassembly of `BtDiscoveryFilter::Classify` (`0x220a6` - `0x22800`):
1. **Device Policies:**
   - Type 0: `btfd::AudioSinkPolicy` (Bluetooth headphones, A2DP UUID `0x110B`)
   - Type 1: `btfd::BleHidPageTurnerPolicy` (Page Turner Remote, VID `0x0171`, PID `0x0443`)
2. **Matching Criteria:**
   ```
   I BtDiscoveryFilter:Classify:VP ID match: name=%P type=%d:
   I BtDiscoveryFilter:Classify:UUID match: name=%P uuid=0x%04X type=%d:
   D BtDiscoveryFilter:Classify:rejected: name=%P uuids=%zu enabledTypes=%zu:
   ```
   The daemon reads the 16-bit Vendor ID and Product ID from the device's Device Information Service (PnP ID characteristic). When VID is `0x0171` and PID is `0x0443`, it accepts the device into Type 1 (`pageTurner`).
3. **Characteristic Queries:**
   - At VMA `0x2f78a` / `0x3b60c`:
     `movw r3, #0x2a26` (Firmware Revision String)
     `movw r2, #0x180a` (Device Information Service)
     Kindle reads `0x2A26` and logs:
     `I BtService: [pageTurner] %P "%P" bat=%d fw=%s::`
   - Battery level is obtained from service `0x180F` / characteristic `0x2A19`.

### 3.4 User Interface & Physical Remote Specification (`pillow.mo`)
From the compiled gettext translation table in `app/KPPPillow-2.0/locale/en/LC_MESSAGES/pillow.mo`:
- **Top face button (Button 1):** Page turn forward / back
- **Bottom face button (Button 2):** Page turn forward / back
- **Upper side button (Button 3):** Brightness up (`KEY_BRIGHTNESSUP`)
- **Lower side button (Button 4):** Brightness down (`KEY_BRIGHTNESSDOWN`)
- **Long-press both side buttons simultaneously:** Puts Kindle to sleep (`KEY_POWER`)
- **Battery warnings:**
  - `< 20%`: "Battery low - {1} battery is below 20%."
  - `< 5%`: "Battery critically low - {1} battery is below 5%. Charge your remote to continue using it."

---

## 4. BLE Emulation Requirements Summary

| Parameter | Required Value | Notes |
|---|---|---|
| **Advertised Name** | `Amazon Kindle Remote` | Appears in Kindle Bluetooth settings |
| **Appearance** | `0x03C1` (Keyboard) or `0x0180` (Remote) | Standard BLE appearance |
| **Advertised Services** | `0x1812` (HID), `0x180F` (Battery), `0x180A` (DIS) | Required for fast discovery filter match |
| **PnP ID (0x2A50)** | `0x02, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01` | Source: USB (0x02), VID: 0x0171, PID: 0x0443, Ver: 1.0.0 |
| **Firmware Rev (0x2A26)** | `"1.0.0"` | Read by Kindle upon connection |
| **Manufacturer (0x2A29)** | `"Amazon"` | Standard DIS string |
| **Battery Level (0x2A19)** | `0` - `100` (%) | Supports Read + Notify |
| **Page Forward Key** | `KEY_PAGEDOWN` (HID Usage `0x4E`) | Standard Keyboard report |
| **Page Back Key** | `KEY_PAGEUP` (HID Usage `0x4B`) | Standard Keyboard report |
| **Brightness Up** | `0x006F` | Consumer Control report |
| **Brightness Down** | `0x0070` | Consumer Control report |
| **Sleep / Power** | `0x0030` | Consumer Control report |

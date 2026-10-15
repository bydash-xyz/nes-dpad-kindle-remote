# Kindle Bluetooth Remote Interoperability Specification

## Target Platform
- **Reference Firmware:** Kindle OS 5.20.1+
- **Compatible Hardware:** Kindle Scribe, Kindle Colorsoft, Kindle Paperwhite (12th Gen+)

---

## 1. Executive Summary

Kindle OS 5.20.1+ includes native Bluetooth Page Turner remote support. This document specifies the exact Bluetooth Low Energy (BLE) profiles, identification data, and HID usage codes required for a custom wireless remote to pair and interact natively with stock Kindle devices without software modifications.

1. **Advertising Data & Primary Discovery Filter:**
   - **Manufacturer Specific Data (AD Type `0xFF`):** Must carry Amazon company ID `0x0171` (little-endian `71 01`) and Product ID `0x0443` (big-endian `04 43`) in the primary advertising packet (`71 01 04 43`). Without this exact payload, the Kindle discovery filter drops the device immediately, even if HID `0x1812` is advertised.
   - **Advertised Name:** `"Amazon Kindle Remote"` (scan response is acceptable).
2. **Device Information Service (DIS 0x180A) & PnP ID:**
   - **PnP ID (`0x2A50`):** Vendor ID Source `0x01` (Bluetooth SIG), Vendor ID `0x0171` (Amazon), Product ID `0x0443`, Version `0x0100`.
   - **Firmware Revision (`0x2A26`):** `"1.0.0"` (read by `acsbtfd` upon connection).
   - **Manufacturer Name (`0x2A29`):** `"Amazon"`
   - **Model Number (`0x2A24`):** `"Kindle Remote"`
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
The daemon classifies devices into internal policies:
- Type 0: `btfd::AudioSinkPolicy` (Bluetooth audio, A2DP UUID `0x110B`)
- Type 1: `btfd::BleHidPageTurnerPolicy` (Page Turner Remote, VID `0x0171`, PID `0x0443`)

**Matching Criteria (determined by testing against a Kindle, not from disassembly):**
The discovery filter examines incoming advertising packets:
- It checks AD `0xFF` (Manufacturer Specific Data) for Amazon's company ID `0x0171` (`71 01`) followed by PID `0x0443` (`04 43`).
- HID Service UUID `0x1812` alone is rejected without this manufacturer payload.

**Post-Connection Characteristic Queries:**
- Upon pairing, the Kindle queries DIS (`0x180A`) for PnP ID (`0x2A50`) and Firmware Revision (`0x2A26`).
- Battery level is read and monitored via Battery Service (`0x180F` / characteristic `0x2A19`).

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
| **Manufacturer Data (AD 0xFF)** | `71 01 04 43` | Company 0x0171 (LE), PID 0x0443 (BE). **Required in primary ad packet.** |
| **Advertised Name** | `Amazon Kindle Remote` | Appears in Kindle Bluetooth settings (scan response OK) |
| **Advertised Services** | `0x1812` (HID) | Human Interface Device service |
| **PnP ID (0x2A50)** | `0x01, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01` | Source: Bluetooth SIG (0x01), VID: 0x0171, PID: 0x0443, Ver: 1.0.0 |
| **Firmware Rev (0x2A26)** | `"1.0.0"` | Read by Kindle upon connection |
| **Manufacturer (0x2A29)** | `"Amazon"` | Standard DIS string |
| **Model Number (0x2A24)** | `"Kindle Remote"` | Standard DIS string |
| **Battery Level (0x2A19)** | `0` - `100` (%) | Supports Read + Notify |
| **Page Forward Key** | `KEY_PAGEDOWN` (HID Usage `0x4E`) | Standard Keyboard report |
| **Page Back Key** | `KEY_PAGEUP` (HID Usage `0x4B`) | Standard Keyboard report |
| **Brightness Up** | `0x006F` | Consumer Control report |
| **Brightness Down** | `0x0070` | Consumer Control report |
| **Sleep / Power** | `0x0030` | Consumer Control report |

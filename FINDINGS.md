# Kindle Scribe / Colorsoft Bluetooth Remote: Comprehensive Technical Findings

**Investigation Date:** October 2026  
**Target Firmware:** `update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin` (FW 5.20.1, Build 4893920050)  
**Target Platform:** Amazon `Platcs8` (Kindle Scribe Colorsoft 2025 platform, Variants: 4VX, 4PF, 4PH, 4F9, 4FB, 46P)  
**Hardware Form Factor Target:** Nintendo gashapon NES-style controller keychain; usable insert volume `35 mm × 32 mm × 7 mm` (measured)  
**Revised:** 2026-10-08 — platform changed from ESP32 to XIAO nRF52840 + ZMK (see sections 4.1–9)

---

## 1. Context & Objective

Amazon introduced native Bluetooth Page Turner remote support in firmware version 5.20.1 for the Kindle Scribe and Kindle Colorsoft. Third-party generic Bluetooth presentation clickers, gamepads, and volume buttons frequently fail or are ignored by the Kindle UI. 

The goal of this investigation was to:
1. Deconstruct the official firmware update `update_kindle_scribe_colorsoft_1st_generation_5.20.1.bin`.
2. Reverse engineer the Bluetooth stack, input pipeline, and user interface daemons to determine why generic remotes are rejected and what exact parameters the Kindle expects.
3. Determine how to build a fully compatible custom Bluetooth remote (originally ESP32; now nRF52840, see section 5.2).
4. Select a hardware platform small enough to fit inside the miniature Nintendo controller keychain (insert volume `35 mm × 32 mm × 7 mm`).

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

To be recognized by stock Kindle software (no jailbreak), the remote must implement:

| BLE Parameter | Specification | Purpose |
|---|---|---|
| **Advertised Device Name** | `Amazon Kindle Remote` | Display name in Kindle Bluetooth settings |
| **Appearance** | `0x03C1` (Keyboard) or `0x0180` (Remote Control) | Bluetooth SIG appearance |
| **Manufacturer Data (AD `0xFF`)** | `71 01 04 43` (company `0x0171` LE16, then PID `0x0443` **big-endian**) in the primary packet | **Required.** The only way `acsbtfd` classifies a device as page turner (see note below) |
| **Advertised Services** | `0x1812` (HID) in the primary advertising packet; `0x180F`, `0x180A` exposed via GATT | Standard HOGP; does NOT trigger page-turner discovery by itself |

**Discovery filter (verified against 5.20.1 stack, 2026-10-08).** `BtDiscoveryFilter::Classify` (`acsbtfd` `0x22574`) has two tables: VID/PID `{0x0171, 0x0443, type 1}` (built at `0x1a1e4`) and UUID `{0x110B, type 0}` only (A2DP). There is no `0x1812` entry, so HID UUID alone is rejected. VID/PID come from `aceBT_getEirManfInfo`, which is filled by `libbluetooth.so` at discovery time (`0xac5ec`): it reads AD `0x10` (Device ID, 8 bytes, Amazon VID skipped to the next step) then AD `0xFF`. For an `0xFF` payload of length > 3 and != 8 it takes company = LE16(data[0..1]), PID = BE16(data[2..3]) (`rev16` at `0xad600`) and calls `btif_in_is_amazon_ble_remote(company, pid)` (accepts VID `0x0171` with PIDs `0x411-0x415, 0x418, 0x419, 0x41c, 0x41e, 0x420, 0x421, 0x423, 0x424, 0x443`). Length 8 is a hex-ASCII form for USB VID `0x1949`; length >= 10 with `0x0171` at [4..5] is the Alexa gadget form. GATT PnP ID is not used for discovery.
| **Device Information (0x180A):** | | |
| - PnP ID (`0x2A50`) | `0x01, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01` | Source 0x01 (Bluetooth SIG), VID 0x0171, PID 0x0443, ver 0x0100 |
| - Firmware Revision (`0x2A26`) | `"1.0.0"` | Read and logged by `acsbtfd` |
| - Manufacturer Name (`0x2A29`)| `"Amazon"` | Standard identification |
| - Model Number (`0x2A24`) | `"Kindle Remote"` | Standard identification |
| **Battery Service (0x180F):** | | |
| - Battery Level (`0x2A19`) | `0` - `100` (%) | Read + Notify; Kindle warns at < 20%, critical at < 5% |
| **HID Service (0x1812):** | | |
| - Report ID 1 (Keyboard) | 8-byte standard report | `0x4E` (PageDown), `0x4B` (PageUp) |
| - Report ID 2 (Consumer) | 2-byte 16-bit usage report | `0x006F` (Bright+), `0x0070` (Bright-), `0x0030` (Sleep) |

* **Verified** against device stack: see `PROTOCOL_SPEC.md` section 3.1 (`devices.json`) for the `0x0171:0x0443` `page_turner` allowlist entry; the same VID/PID pair appears in the `acsbtfd` table at `0x1a23c`.
* **PnP vendor-ID source byte:** the original spec used `0x02` (USB-IF). `0x0171` is Amazon's Bluetooth SIG company ID (Amazon's USB VID is `0x1949`), so `0x01` (Bluetooth SIG) is the likely-correct source. `input_manager` matches VID/PID only; whether `acsbtfd` checks the source byte is unverified. Try `0x01` first, fall back to `0x02` if pairing is rejected.
* **Duplicate services in `src/main.cpp`:** `NimBLEHIDDevice` already creates the 0x180A and 0x180F services, and the sketch created a second copy of each (two PnP IDs). Fixed in `protocol_test/`.
* **Stay connected; do not deep-sleep between presses.** Deep sleep drops the BLE link, so each press after idle forces a reconnect + re-encryption (hundreds of ms to >1 s), loses the wake press unless queued, and may trigger Kindle connect/disconnect popups. The remote should hold a persistent connection using a long connection interval and peripheral latency.

**ESP32-S3 `protocol_test/` results on Kindle Scribe Colorsoft, FW 5.20.1 (2026-10-08): all pass.**

| Check | Result |
|---|---|
| Discovery | Not listed with HID `0x1812` alone. Listed after adding mfr data `71 01 04 43` to the primary packet (see section 4 note). Name in scan response is fine. |
| Display name | The Kindle ignores the advertised name and lists the remote as "Kindle Click"; a second bonded remote with the same VID/PID (XIAO/ZMK, 2026-10-11, ESP32 still paired) became "Kindle Click 2". The string is not in `extracted/` (plain or `.mo` grep), so it comes from a part of the firmware not extracted. |
| Pairing | Just Works, encrypted + bonded, first try. PnP source `0x01` accepted (`0x02` not needed). |
| GATT reads | Kindle reads PnP ID, FW revision, battery; subscribes to keyboard, consumer and battery reports. |
| Connection params | Kindle default: 48.75 ms at connect, 7.5 ms during discovery, then 20 ms / latency 0 / timeout 20 s; unchanged during Kindle sleep. Peripheral requests: the Kindle **always keeps the interval at 20 ms** (ignored 30-50 ms and 100-125 ms) but **accepts the requested latency and timeout** (latency 4, 30; timeout 4 s, 6 s). 20 ms + latency 30 = ~620 ms idle wake-ups, no noticeable page-turn delay. |
| Reconnect | After board reset, the bonded Kindle reconnects automatically within seconds (uses a resolvable private address). |
| Page forward `0x4E` / back `0x4B` | Work. |
| Brightness up `0x006F` / down `0x0070` | Work. |
| Hold-to-repeat | Brightness up/down held 2 s (`U`, `D`) ramps continuously. Page forward held 2 s (`F`) turns one page only (no repeat). |
| Sleep `0x0030` | Puts the Kindle to sleep; link stays connected. |
| Wake from sleep | Neither a page key nor `0x0030` wakes the Kindle. Wake with its own power button. Expected; see below. |
| Reconnect after long absence | Remote dropped the link and stopped advertising (`o`), then resumed (`a`). Offline 60 s: reconnected 0.27 s after advertising resumed. 180 s: 2.3 s. 600 s: 2.9 s. No sign of a reconnect limit within 10 min, so the Kindle keeps a background connect to the bonded device. Kindle was asked to stay awake (not confirmed). A deep-sleep remote would still reconnect, at a cost of ~0.3-3 s on the first press. |
| Battery `< 20%` / `< 5%` | Low and critical warnings both shown on notify. |

**XIAO nRF52840 + ZMK (`zmk-config/`, 2026-10-11), same Kindle on 5.20.1:** discovered and paired first try (listed as "Kindle Click 2" while the ESP32 was still bonded). Page forward/back, brightness up/down and Down-hold sleep all work, driven by jumpering D0-D3 to GND on USB power. Battery cell arrived and attached to BAT pads. 2026-10-12: remapped to D0 fwd / D1 bright+ / D3 back / D5 dim+sleep and retested through the copper-tape pads; all four work after fixing bridged zigzag gaps (5.5). The `zmk-usb-logging` snippet build (separate build dir) logs every kscan event over USB serial and is the quickest way to tell a pad fault from a Kindle-side fault.

**Why the remote cannot wake the Kindle (firmware analysis, 2026-10-08): by design, not a bug in our implementation.**
* `input_manager` handles the remote's `KEY_POWER` (`0x0030`) as the action `system.sleep` (`categories.json`, `dispatch_on: ["press"]`). The handler at `0x21294` calls powerd proxy vtable slot `0x1c`. The vtable at `0x3bc70` maps that slot to `PowerManagementServiceProxy::setPowerButtonIfActiveAsync(1)`. The name says it acts only when the device is active, so it is one-way sleep, not a toggle. The toggle `setPowerButtonAsync` is slot `0x18` and is never called.
* Cross-check: brightness up/down call slot `0x68` = `stepBrightnessWithUI(+1/-1)`, which matches the vtable mapping.
* `input_manager` reaches powerd in only three places: sleep plus the two brightness calls. `setWakeUpAsync` (slot `0x14`) is in the vtable but never called. `acsbtfd`, `btd` and `btmanagerd` have no power-button or wake calls.
* The `capability_filter` stage passes only the 5 listed keys, so a HID System Wake Up / `KEY_WAKEUP` report would be dropped.
* Page keys are emitted to the virtual device `kindle-input-pt`. That device is not a powerd wake source, so the UI ignores them while in the screensaver.
* The official Kindle Click (announced 2026-10-01, ships 2026-10-28) is described only as "press both side buttons to sleep". Neither Amazon's copy nor the 5.20.1 pairing popup (`pillow.pageTurnerPairedPopup*`) mentions waking. Recheck after a firmware update ships with the Click, since a newer `input_manager` could add a wake path.
* Consequence: no firmware change on our side can wake a 5.20.1 Kindle. Keep the Down-hold sleep mapping.

**Other remote features in 5.20.1 (`input_manager` / `acsbtfd` strings, 2026-10-08):**
* Only 5 keys pass `capability_filter`, and only 3 actions have handlers (`system.sleep`, `display.brightness_up/down`). Any other key, consumer usage or System Wake Up is dropped. There are no hidden keys to unlock.
* The trigger engine supports `long_press`, `double_tap` and `chord` (`chord_keys`, `chord_window_ms`, `threshold_ms`), but `page_turner` has `trigger_detection: false`, so only `press` is used. The Kindle does not detect the Click's "both buttons" chord; the remote itself sends `0x0030`.
* Hold-to-repeat: `kernel_repeat` 500 ms / 333 ms, and brightness dispatches on `press` and `repeat`. Confirmed 2026-10-08: holding `0x006F` / `0x0070` for 2 s ramps brightness continuously. Page keys do not repeat: a 2 s page-forward hold turns exactly one page.
* Input stages: 50 ms debounce, 100 events/s (burst 10). At 180 degrees rotation, forward and back are swapped automatically.
* Remapping is done Kindle-side (`addMapping` / `swapEvents` over IPC, persisted to `/var/local/input_manager/mappings.ini`, max 8 overrides). That is how Settings swaps forward/back. The remote cannot set it.
* `acsbtfd` knows only two device types: `BleHidPageTurner` (matched by VID/PID) and A2DP `AudioSink`. It reads FW revision `0x2A26` for display. Battery comes from BAS notify and also from the HID host battery callback (`AceHidBatteryLevelCb`, raw/min/max normalized). Reconnect runs a bounded number of cycles (`max cycles reached -- stopping`). In practice this is not a problem: see the reconnect row in the results table.
* The folio (`FOLIO_TUTORIAL_SHOWN`, `folio_paired_dialog` via powerd `getPogoAccessory`) is the magnetic page-turn cover on pogo pins, not BLE.

Test-bench notes: the S3 board's serial is a WCH CH343 (`/dev/cu.usbmodem...`), so keep `ARDUINO_USB_CDC_ON_BOOT` off. Opening the port resets the board, so `protocol_test/tools/bridge.py` keeps it open and takes commands from `tools/cmd.txt`. A previously bonded Mac (board was "ESP32-S3-Synth") auto-reconnected and blocked advertising until it was forgotten. Claude Code cannot use CoreBluetooth on macOS (no usage description in its bundle); run `tools/ble_scan.py` from Terminal.app.

---

## 5. Hardware Architecture & Build Plan (revised 2026-10-08)

### 5.1 Enclosure (measured)
* **Enclosure:** Nintendo Game Controller gashapon keychain (NES-style D-pad).
* **Usable volume:** the removable plastic insert, **35 mm x 32 mm x 7 mm**. It is replaced by a 3D-printed **PETG** insert (PLA softens ~60 C and can warp in a hot car).
* The earlier `50 x 50 x 20 mm` / `46 x 46 x 15 mm` figures were incorrect.
* **D-pad contact:** the toy keeps its original rubber membrane with **conductive carbon pills** (measured 100-400 ohm probe-to-probe; lower when pressed flat).

### 5.2 Platform decision
| Option | Verdict | Reason |
|---|---|---|
| ESP32-C3 SuperMini (22.5 x 18 x 4.5) | Rejected | No LiPo charger; persistent BLE connection idles in the mA range; 100-250 mA radio peaks |
| XIAO ESP32-C3 + LiPo | Rejected | Fits, but same connected-idle power problem |
| ESP32 + coin cell | Not viable | Coin cell cannot source ESP32 radio peaks (brownout) |
| **Seeed XIAO nRF52840 (plain, not Sense/Plus) + ZMK** | **Chosen (prototype)** | 21 x 17.8 mm, built-in LiPo charger + BAT pads, UF2 drag-and-drop flashing, low connected-idle current |
| Insight SiP ISP1507 (8 x 8 x 1 mm) or Raytac MDBT42Q (10 x 16 x 2.2 mm) on custom PCB + CR2032/CR2016 | Future v2 | Smallest; nRF52 runs directly from a coin cell; needs LGA assembly (ISP1507) and an SWD programmer |

### 5.3 Battery
* **Chosen:** 401230 LiPo, 3.7 V 100 mAh with PCM (4.0 x 12 x 30 mm), e.g. generic 2-pack or Adafruit #1570 (3.8 x 11.5 x 31 mm).
* Cut off the Micro JST 1.25 connector, **verify polarity with a multimeter**, solder short leads to the XIAO BAT+/BAT- pads. Fold the PCM flat with Kapton.
* Measure the actual cell; the listed 30 mm often excludes the PCM.
* Leave the XIAO charge current at the default 50 mA.
* **Rejected:** 502030 (30 x 20 x 5, does not fit beside the XIAO in 32 mm); 601120 (6 mm thick, leaves ~1 mm in a 7 mm cavity with no swell margin).

### 5.4 Layout inside the 35 x 32 x 7 mm insert
* Battery long axis along the **35 mm** side (30 mm cell leaves ~5 mm for the PCM).
* XIAO beside it: 12 + 17.8 = 29.8 mm across the **32 mm** side (~2.2 mm spare, so keep any divider at ~0.8 mm or omit it).
* Height: 4 mm cell or ~3.5 mm board, plus insert skin and wiring, within 7 mm.
* Antenna end of the XIAO toward the cavity edge, away from the battery. Slot in the insert for USB-C charging access.

### 5.5 D-pad contacts: copper tape on the printed insert (prototype)
* Mark pill positions by pressing the membrane onto masking tape on the insert.
* Lay a **GND cross** of copper tape whose arms run under the inner half of each pad, then four **signal pads** on the outer halves.
* Cut a **0.3-0.5 mm zigzag gap** between signal and GND under each pill with a fresh #11 blade.
* **Solder on the bench, not on the insert:** tin the tape tails and solder 28 AWG wire (stranded preferred) on a ceramic tile, then peel and stick; route wires through ~1.2 mm holes to the pocket.
* Check each pad: open with no press, ~100-400 ohm when pressed.
* Bridged gap symptom (2026-10-12): with the board powered, a pad pin reads well under 3.3 V to GND (seen: 0.x V on D0/D1) instead of 3.3 V from the pull-up, and ZMK logs no events for that pin. Cause was an incomplete zigzag cut. Recut, clear copper slivers from the gap, and recheck open-circuit before reconnecting.
* Keep the insert top at the original height minus tape thickness so the D-pad is not pre-pressed.
* Electrical: internal pull-ups (~13 kohm on nRF52) read a pressed pad at well under 0.1 V; debounce 10-20 ms. Alternative: thin copper-clad FR4 with knife-cut pads. Durable v2: JLCPCB 0.8 mm ENIG interdigitated pads.
* Five wires total: D0, D1, D3, D5 (four directions) + GND.

### 5.6 D-pad button mapping
| D-pad | XIAO pin | Action | Key |
|---|---|---|---|
| Right | D0 | Page forward | `KEY_PAGEDOWN` (HID 0x4E) |
| Left | D3 | Page back | `KEY_PAGEUP` (HID 0x4B) |
| Up | D1 | Brightness up | Consumer 0x006F |
| Up (D1) | D1 | Brightness up | Consumer 0x006F |
| Down / Dimmer (tap) | D3 | Brightness down | Consumer 0x0070 |
| Down / Dimmer (hold ~1.5 s) | D3 | Kindle sleep | Consumer 0x0030 (`KEY_POWER`) |
| Left / Back | D5 | Page back | `KEY_PAGEUP` (HID 0x4B) |
Pins changed 2026-10-12 (was D0-D3 in order). Keymap position order stays forward, back, up, down; the overlay maps D0, D3, D1, D5 to those positions.
- **Build-time Mapping Customization:** `build.sh` accepts `EXTRA_CMAKE_ARGS` to override overlays or pass definitions (e.g. `EXTRA_CMAKE_ARGS="-DEXTRA_DTC_OVERLAY_FILE=..." ./build.sh`).

* The official remote has **four buttons, five actions**: sleep is a long press of both side buttons.
* The earlier "hold Up + Down" plan is likely impossible: a D-pad rocks on a center pivot, so opposite directions cannot close together. Hold-Down-to-sleep is used instead. Alternatives: hold a diagonal combo, or omit sleep (Kindle auto-sleeps).

---

## 6. Firmware: ZMK on XIAO nRF52840

ZMK is open-source keyboard firmware built on Zephyr. It provides BLE HID (keyboard + consumer), bonding, Battery Service reporting, low-power connected idle, debounce, hold-tap and combos. Build via a `zmk-config` GitHub repo + GitHub Actions, output `.uf2`, flash by double-tapping reset and dragging the file onto the USB drive. Board: `xiao_ble`.

### 6.1 `config/xiao_ble.conf` (draft; verify option names against the ZMK version used)
```
CONFIG_ZMK_KEYBOARD_NAME="Amazon Kindle Remote"
CONFIG_BT_DIS_PNP=y
CONFIG_BT_DIS_PNP_VID_SRC=1
CONFIG_BT_DIS_PNP_VID=0x0171
CONFIG_BT_DIS_PNP_PID=0x0443
CONFIG_BT_DIS_PNP_VER=0x0100
CONFIG_BT_DIS_MANUF="Amazon"
CONFIG_BT_DIS_MODEL="Kindle Remote"
CONFIG_BT_DIS_FW_REV=y
CONFIG_BT_DIS_FW_REV_STR="1.0.0"
CONFIG_ZMK_BATTERY_REPORTING=y

# Keep the BLE link up; if deep sleep is used, make it long
CONFIG_ZMK_SLEEP=n
# CONFIG_ZMK_SLEEP=y
# CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=1800000

# Put the onboard QSPI flash (P25Q16H) into deep power-down
CONFIG_FLASH=y
CONFIG_NORDIC_QSPI_NOR=y
CONFIG_PM_DEVICE=y
```

### 6.2 Keymap sketch
```
/ {
    behaviors {
        hold_sleep: hold_sleep {
            compatible = "zmk,behavior-hold-tap";
            #binding-cells = <2>;
            tapping-term-ms = <1500>;
            flavor = "tap-preferred";
            bindings = <&kp>, <&kp>;
        };
    };
    keymap {
        compatible = "zmk,keymap";
        default_layer {
            bindings = <&kp PG_DN &kp PG_UP &kp C_BRI_UP &hold_sleep C_POWER C_BRI_DN>;
        };
    };
};
```
`&kp C_BRI_UP` holds the usage while Up is held, so the Kindle's key repeat ramps brightness (500 ms delay, 333 ms period). Down cannot ramp: `hold_sleep` sends `C_BRI_DN` as a tap on release, and a hold becomes sleep.
Plus a direct-GPIO kscan in `config/xiao_ble.overlay` for D0-D3 (active low, internal pull-up).

**Required: manufacturer data in the advertisement.** The Kindle only discovers the remote if the primary advertising packet carries AD `0xFF` = `71 01 04 43` (validated on the ESP32-S3). Stock ZMK builds its advertising data (flags, name, appearance, service UUIDs) in `app/src/ble.c` with no config option for manufacturer data. This needs a small patch or a fork of ZMK that adds `BT_DATA(BT_DATA_MANUFACTURER_DATA, ...)` to the `zmk_ble_ad` array. Check the 31-byte budget: flags 3 + mfr 6 + name 22 = 31 if the name stays in the primary packet; otherwise move the name to the scan response.

### 6.2a Local build (2026-10-11)
* Config lives in `zmk-config/` (`config/xiao_ble.conf`, `.overlay`, `.keymap`, `patches/0001-kindle-remote-advertising.patch`); `zmk-config/build.sh` builds `kindle_remote.uf2` from a gitignored local workspace `zmk-workspace/` (ZMK main `4dd95d78`, Zephyr `v4.1.0+zmk-fixes`, SDK 0.17.0). Board target `xiao_ble//zmk`.
* The patch adds Kconfig `ZMK_BLE_KINDLE_REMOTE_ADV`: mfr data `71 01 04 43` in the primary packet, name moved to the scan response (`FORCE_NAME_IN_AD` dropped), `BT_DEVICE_NAME_MAX` 21 (Zephyr asserts name length < max).
* ZMK main now defaults `BT_DIS_PNP_VID_SRC` to 2 (commit `4dd95d78`, 2026-10-09); the conf sets 1 explicitly.
* `CONFIG_ZMK_USB=n`: with USB HID on, ZMK sends keys to the USB host whenever plugged in. Side effect: no USB serial; flash by double-tap reset.
* Keymap: Down tap sends `C_BRI_DN` through a 30 ms tap macro, matching the ESP32 test (shorter taps are untested against the Kindle's 50 ms debounce stage).
* Host gotchas: CMake 4.x breaks Zephyr 4.1's SDK lookup unless `ZEPHYR_TOOLCHAIN_VARIANT=zephyr` and `ZEPHYR_SDK_INSTALL_DIR` are set; `west sdk install` failed to list releases, so the SDK was downloaded from the sdk-ng v0.17.0 release.
* `CONFIG_PM_DEVICE` is not set in the current build; add it when measuring idle current (6.3).
* XIAO received: UF2 bootloader 0.6.1, SoftDevice S140 7.3.0, Board-ID `Seeed_XIAO_nRF52840_Sense` (same bootloader on plain and Sense boards).

### 6.3 QSPI flash deep power-down
* Zephyr's `xiao_ble` devicetree marks the P25Q16H `has-dpd`; with the flash driver and `CONFIG_PM_DEVICE` enabled it is held in deep power-down.
* Known trap: if the sleep pinctrl leaves QSPI chip-select floating, the flash wakes and current rises to ~4 mA. Current upstream `xiao_ble-pinctrl.dtsi` has `bias-pull-up` on `QSPI_CSN` (P0.25) in `qspi_sleep` group2. If ZMK's Zephyr lacks it, override `qspi_sleep` in `xiao_ble.overlay`.
* Verify by measuring idle current with and without the change.

---

## 7. Power Budget (estimate)
* Usable capacity: ~80 mAh of 100 mAh (PCM cutoff, aging margin).
* Page turns are negligible (~0.01 mAh/day at 1,000 turns).

| Avg connected-idle current | Runtime |
|---|---|
| 30 uA (well tuned) | ~110 days |
| 50 uA (typical) | ~65 days |
| 100 uA (poorly tuned) | ~33 days |

* Measured link: Kindle fixes the interval at 20 ms but accepts peripheral latency 30, so idle radio events every ~620 ms.
* Main factors: Kindle-chosen connection interval / peripheral latency, QSPI flash power-down, LEDs off, battery divider enabled only when sampling.
* Measure: multimeter in series with the battery on the uA range while paired and idle. Runtime = 80 mAh / measured current.
* Optional: ZMK deep sleep after 30-60 min idle gives single-digit uA, at the cost of a ~1 s reconnect on the first press.

---

## 8. Bill of Materials (prototype)
| Item | Qty | Approx. |
|---|---|---|
| Seeed Studio XIAO nRF52840 (plain) | 2 (1 spare) | ~$12 each |
| 401230 LiPo 3.7 V 100 mAh with PCM (or Adafruit #1570) | 1-2 | ~$6 |
| Conductive-adhesive copper tape, 5-10 mm (or single-sided copper-clad FR4) | 1 | ~$8 |
| 28 AWG wire (stranded preferred) | on hand | - |
| Kapton tape, 10 mm | 1 roll | ~$6 |
| 3D-printed PETG replacement insert | 1 | own printer or ~$3 |
| Tools: fine-tip iron, flux, flush cutters, #11 hobby knife, multimeter | - | - |

Not needed: ESP32 boards, 502030 / 601120 cells, SWD programmer (only for a future bare-module PCB).

---

## 9. Next Steps
1. Measure pill positions on the membrane; design the PETG insert (pad layout, wire holes, battery/XIAO pocket, USB-C slot).
2. Confirm with a multimeter whether opposite D-pad directions can close together (expected: no).
3. Measure the actual 401230 cell dimensions.
4. ~~Run the ESP32-S3 `protocol_test/` checklist against the Kindle~~ Done 2026-10-08, all pass (4.1), including connection-parameter test: target 20 ms / latency 30 / 6 s (ZMK default latency is 30; confirm ZMK sends the update request).
5. Create the `zmk-config` repo (conf, overlay, keymap above) plus the ZMK advertising patch for manufacturer data `71 01 04 43`; build and flash.
6. Pair the ZMK build with the Kindle (PnP source `0x01` is confirmed).
7. Measure connected-idle current; apply the QSPI flash fix if high.

---

## 10. Implementation Artifacts in Repository

| File | Description |
|---|---|
| `PROTOCOL_SPEC.md` | In-depth technical breakdown and specifications for Kindle OS input management. |
| `README.md` | Overview, ZMK build/flash guide, and safety instructions. |
| `zmk-config/` | Production ZMK configuration, keymap, and advertising patch. |
| `protocol_test/` | ESP32-S3 protocol test harness (NimBLE 1.4): serial-driven page/brightness/sleep/battery commands, PnP source toggle, connection-parameter logging. Fixes duplicate DIS/BAS services present in `legacy/src/main.cpp`. See its README for the test checklist. |
| `legacy/HARDWARE_GUIDE.md` | ESP32-era BOM and wiring; **superseded by sections 5 and 8**. |
| `legacy/src/main.cpp` | ESP32 NimBLE implementation; **superseded by ZMK (section 6)**. If reused, change PnP source to `0x01` and remove deep sleep between presses. |
| `legacy/platformio.ini` | PlatformIO config for the legacy ESP32 path (superseded). |
| `legacy/FINDINGS.orig.md` | Copy of this document before the 2026-10-08 revision. |
| `local_artifacts/` | Quarantined local firmware updates and extracted analysis images (gitignored). |

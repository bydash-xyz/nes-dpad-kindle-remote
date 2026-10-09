> **Superseded (2026-10-08):** this is the ESP32-era document with an incorrect enclosure size. See `CLAUDE.md` and `FINDINGS.md` for the current plan (XIAO nRF52840 + ZMK, 35 x 32 x 7 mm insert).

# Hardware Design & Build Guide: Miniature Kindle Remote

## 1. Enclosure Specification
- **Target Enclosure:** Nintendo Game Controller Gashapon Capsule Toy (NES Style D-Pad Keypad)
- **External Dimensions:** `50 mm × 50 mm × 20 mm` (2.0" × 2.0" × 0.8")
- **Internal Usable Volume:** Approximately `46 mm × 46 mm × 15 mm`
- **Control Interface:** 4-way D-Pad (Up, Down, Left, Right) with central pivot.

```
       +-----------------------------------+
       |            50 mm                  |
       |     +-----------------------+     |
       |     |          [UP]         |     |
 50 mm |     |   [LEFT]      [RIGHT] |     |
       |     |         [DOWN]        |     |
       |     +-----------------------+     |
       |                                   |
       +-----------------------------------+
                  Depth: 20 mm
```

---

## 2. Microcontroller Selection

### Recommended: **Seeed Studio XIAO ESP32-C3**
The **XIAO ESP32-C3** is the optimal choice for this build:

| Parameter | Specification | Advantage for this Build |
|---|---|---|
| **Board Dimensions** | **21.0 mm × 17.5 mm × 3.5 mm** | Takes up only 15% of the enclosure footprint |
| **Processor** | ESP32-C3 32-bit RISC-V @ 160 MHz | High performance, rapid wake-up (<50 ms) |
| **Connectivity** | Bluetooth 5.0 (BLE) + onboard antenna | Clean wireless link without external pigtails |
| **Deep Sleep Current** | **~44 µA** | Extends battery life to months on standby |
| **Power Management** | Integrated LiPo charge controller | Dedicated solder pads for 3.7V battery on underside |
| **Connector** | USB Type-C | Fast charging and firmware flashing |
| **Operating Voltage** | 3.3V (tolerant of 3.7V - 4.2V battery) | Native battery operation without extra LDO |

*Alternative:* **Seeed Studio XIAO ESP32-C6** (same 21 × 17.5 mm footprint, Bluetooth 5.3, ~15 µA deep sleep) or **Waveshare ESP32-C3-Zero** (23.5 × 18.0 mm).

---

## 3. Battery & Power Selection

- **Battery Type:** 3.7V Lithium-Polymer (LiPo) Rechargeable Pouch Cell
- **Recommended Model:** **502030** (5.0 mm thick × 20 mm wide × 30 mm long)
  - **Capacity:** ~250 mAh - 300 mAh
  - **Fit:** Fits flat alongside or beneath the 21 × 17.5 mm XIAO board within the 46 × 46 mm cavity.
- **Alternative Model:** **402030** (4.0 mm × 20 mm × 30 mm, ~200 mAh) for tighter internal clearance.
- **Battery Life Estimate:**
  - *Deep Sleep (~44 µA):* 250 mAh / 0.044 mA ≈ **5,600 hours (~7.5 months standby)**
  - *Active Transmit (~15 mA for 30 ms per turn):* **> 100,000 page turns** per charge.

---

## 4. Input Mechanism & Button Mapping

The capsule toy D-pad provides 4 distinct tactile directions. We map them directly to the Kindle capabilities discovered in firmware analysis:

| D-Pad Direction | XIAO GPIO | Function | Emulated HID Event | Firmware Action |
|---|---|---|---|---|
| **RIGHT** | `D0` (GPIO2) | Page Forward | `KEY_PAGEDOWN` (Usage `0x4E`) | `nav.page_forward` |
| **LEFT** | `D1` (GPIO3) | Page Back | `KEY_PAGEUP` (Usage `0x4B`) | `nav.page_back` |
| **UP** | `D2` (GPIO4) | Brightness Up | Consumer Usage `0x006F` | `display.brightness_up` |
| **DOWN** | `D3` (GPIO5) | Brightness Down | Consumer Usage `0x0070` | `display.brightness_down` |
| **HOLD UP+DOWN (2s)** | Dual GPIO | Sleep / Wake | Consumer Usage `0x0030` | `system.sleep` |

*Note on 180° Rotation:* Thanks to the Kindle's `orientation_swap_pairs` in `categories.json`, if you turn your Kindle upside down in bed, the Kindle automatically swaps Page Forward and Page Back so the right/forward button remains intuitive!

---

## 5. Wiring Schematic

```
          Seeed Studio XIAO ESP32-C3
             +-----------------+
             | [USB Type-C]    |
       D0 ---| GPIO2      5V   |
       D1 ---| GPIO3      GND  |--- Ground (Common to all switches)
       D2 ---| GPIO4      3V3  |
       D3 ---| GPIO5      D10  |
             |            D9   |
             | BAT-  BAT+ D8   |
             +---+----+--------+
                 |    |
                 |    +------------------- LiPo Battery (+) [Red Wire]
                 +------------------------ LiPo Battery (-) [Black Wire]

                      D-Pad Switch Circuit
                      --------------------
       D0 (GPIO2) -----------------[ Right Tact Switch ]----+----> GND
       D1 (GPIO3) -----------------[ Left Tact Switch  ]----+
       D2 (GPIO4) -----------------[ Up Tact Switch    ]----+
       D3 (GPIO5) -----------------[ Down Tact Switch  ]----+
```

All 4 buttons use the ESP32 internal pull-up resistors (`INPUT_PULLUP`), so no external resistors are required. When pressed, the button pulls the GPIO to Ground.

---

## 6. Assembly & Mechanical Integration

1. **Disassembling the Capsule Toy:**
   - Gently pop the top and bottom clamshell halves of the capsule toy using a plastic spudger.
   - Retain the D-pad plastic rocker, silicone rubber membrane, and center pivot post.
2. **Button Sub-Assembly:**
   - Cut a small piece of prototyping perfboard (approx. `35 mm × 35 mm`, 1.0 mm thickness) to match the internal floor.
   - Solder four low-profile SMD micro tactile buttons (e.g. 3 mm × 4 mm × 2 mm SMD momentary tact switches) positioned directly under the 4 arms of the D-pad rocker.
   - Connect one leg of each switch to a common Ground bus.
   - Connect the other leg of each switch to leads going to `D0`, `D1`, `D2`, and `D3`.
3. **Board & Battery Placement:**
   - Solder the LiPo battery red lead to the `BAT+` pad and black lead to the `BAT-` pad on the underside of the XIAO ESP32-C3.
   - Place the LiPo pouch cell flat at the bottom of the lower shell. Insulate with Kapton tape.
   - Place the XIAO ESP32-C3 above or beside the battery.
   - File a small neat slot (`9 mm × 3.5 mm`) on the side or bottom edge of the clamshell for the USB Type-C charging port.
4. **Final Assembly:**
   - Seat the perfboard with the switches, place the silicone membrane and D-pad rocker on top, and snap the clamshell together.

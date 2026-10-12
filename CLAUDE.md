# Kindle D-pad remote — project context for Claude Code

Read this first. `FINDINGS.md` is the source of truth for details; keep it updated when decisions or test results change.

## Goal
A BLE page-turner remote for the Kindle Scribe / Colorsoft (FW 5.20.1+) that the stock Kindle accepts as an official "Amazon Kindle Remote", built inside a Nintendo NES-style D-pad gashapon keychain. The 4-way D-pad must stay functional.

## Current status (2026-10-08)
- Firmware reverse engineering: done (FINDINGS.md sections 2-3).
- Platform decided: **Seeed XIAO nRF52840 (plain) + ZMK firmware**. The ESP32 path is superseded for the final build.
- Parts ordered: XIAO nRF52840 3-pack, 401230 100 mAh LiPo 2-pack (arrives ~Oct 16), Kirecoo 2" copper tape (conductive adhesive), polyimide/Kapton tape.
- Done: **protocol validated on ESP32-S3** with `protocol_test/` (2026-10-08): discovery, pairing, page fwd/back, brightness, sleep, battery warnings all pass. Remote cannot wake the Kindle. Results in FINDINGS.md 4.1.
- In progress: ZMK build in `zmk-config/` (local, `build.sh`), flashed to XIAO 2026-10-11; pairs with the Kindle and all keys work (FINDINGS 4.1). Battery reporting untested until the cell arrives.
- Not started: PETG insert CAD, copper-tape pad build, current measurement.

## Kindle protocol (must match exactly)
- **Advertising must carry manufacturer data AD `0xFF` = `71 01 04 43`** (company 0x0171 LE, PID 0x0443 big-endian) in the primary packet; without it the Kindle never lists the device (HID UUID alone is rejected). Name `Amazon Kindle Remote` (scan response OK); HID `0x1812` UUID.
- DIS PnP ID `0x2A50`: VID `0x0171`, PID `0x0443`, version `0x0100`, little-endian. Vendor-ID source byte `0x01` (Bluetooth SIG), confirmed working.
- DIS firmware revision `0x2A26` = "1.0.0" (read by `acsbtfd`), manufacturer "Amazon", model "Kindle Remote".
- Battery Service `0x180F` / `0x2A19` read+notify (Kindle warns < 20%, critical < 5%).
- Keys: keyboard `0x4E` PageDown (forward), `0x4B` PageUp (back); consumer `0x006F` bright+, `0x0070` bright-, `0x0030` power/sleep.
- Allowlist evidence: `extracted/etc/input_manager/devices.json`, `categories.json`; `acsbtfd` table at `0x1a23c`.
- Stay connected. Do not deep-sleep between presses (drops the link, loses the wake press, reconnect delay).

## Hardware decisions
- Usable volume: removable insert **35 x 32 x 7 mm** (measured). Replace with a 3D-printed **PETG** insert (not PLA). Ignore the old 50x50x20 figure in README.md / HARDWARE_GUIDE.md.
- Layout: 401230 cell (4 x 12 x 30) long axis along the 35 mm side; XIAO (21 x 17.8) beside it across the 32 mm side (29.8 mm total, thin or no divider). USB-C slot for charging. Antenna end toward the cavity edge.
- Battery: cut Micro JST off, verify polarity with a meter, solder to XIAO BAT pads, Kapton over the PCM. Leave charge current at 50 mA default.
- D-pad contacts: original rubber membrane has carbon pills (100-400 ohm). Pads = one piece of 2" copper tape cut to shape (GND cross + 4 pads + 5 tails); solder 28 AWG wires to tails on the bench (never on the plastic), stick down, then knife-cut a 0.3-0.5 mm zigzag across each pad to isolate the signal half. 5 wires: D0, D1, D3, D5 + GND.
- Mapping: Right D0 page forward, Left D3 page back, Up D1 bright+, Down D5 tap bright- / hold 1.5 s sleep. Up+Down together is not possible on a rocking D-pad.

## Firmware plan (ZMK)
- Board `xiao_ble`; config in a separate `zmk-config` GitHub repo built by GitHub Actions; flash UF2 by double-tap reset.
- Draft `.conf`, keymap (hold-tap `hold_sleep`) and QSPI flash deep-power-down notes are in FINDINGS.md section 6. Option names are unverified against the ZMK version in use; check before building.
- Keep `CONFIG_ZMK_SLEEP=n` or a long timeout (>= 30 min).
- Watch idle current: onboard P25Q16H flash must be in deep power-down; QSPI CSN (P0.25) needs pull-up in the sleep pinctrl state or current jumps to ~4 mA.
- Battery target: 30-50 uA connected idle gives ~2-3 months on ~80 mAh usable.

## Repository map
| Path | Notes |
|---|---|
| `FINDINGS.md` | Full findings, decisions, BOM, power budget, next steps. Update section 4.1 with test results. |
| `FINDINGS.orig.md` | Pre-revision copy. Do not edit. |
| `REVERSE_ENGINEERING.md`, `classify_*.txt`, `init_tables.txt` | Disassembly notes for `acsbtfd` / `input_manager`. |
| `zmk-config/` | ZMK config + Kindle advertising patch; `./build.sh` builds `kindle_remote.uf2` using gitignored `zmk-workspace/`. FINDINGS.md 6.2a. |
| `protocol_test/` | ESP32-S3 harness (PlatformIO, NimBLE-Arduino 1.4.x). Serial commands f/b/u/d/s, 0-9/B battery, x PnP source toggle, c clear bonds, i request long interval, ? status. Checklist in its README. |
| `src/main.cpp`, `platformio.ini` (root) | Original ESP32-C3 sketch. Superseded; has duplicate DIS/BAS services, PnP source 0x02, deep sleep. Do not build on it. |
| `README.md`, `HARDWARE_GUIDE.md` | ESP32-era docs with wrong enclosure size. Superseded by FINDINGS.md. |
| `extracted/`, `extracted_fw/`, `update_*.bin` (502 MB), `rootfs_filelist.txt` | Firmware artifacts. Large; do not read the .bin. |

## Next steps (in order)
1. Build and flash `protocol_test/` on the ESP32-S3 (`cd protocol_test && pio run -t upload && pio device monitor`). If serial is silent, enable `-DARDUINO_USB_CDC_ON_BOOT=1`. Fix any compile errors (code was API-checked against NimBLE-Arduino 1.4.3 but never compiled).
2. Run the 9-step checklist in `protocol_test/README.md`; record pairing result, PnP source that worked, connection interval/latency, sleep/wake behavior and battery warnings in FINDINGS.md 4.1.
3. Measure membrane pill positions; check opposite directions cannot close together.
4. Design the PETG insert (pad recesses, ~1.2 mm wire holes, battery/XIAO pocket, USB-C slot).
5. Create the `zmk-config` repo with the validated protocol values; flash the XIAO; test pads over USB power before the battery arrives.
6. Build the copper-tape pads; assemble; measure connected-idle current and fix flash power-down if high.

## Working conventions
- Keep answers and docs concise; no emoji.
- Prefer editing FINDINGS.md over creating new docs; mark superseded files rather than deleting them.

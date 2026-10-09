# Kindle page-turner protocol test (ESP32-S3)

Throwaway harness to validate the Kindle side before the XIAO nRF52840 + ZMK build.

## Build and flash
    cd protocol_test
    pio run -t upload
    pio device monitor

If the monitor shows nothing, uncomment `-DARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini`
(boards that only have the native USB port), or plug into the other USB port.

## Test checklist
| # | Test | How | Pass |
|---|---|---|---|
| 1 | Discovery and pairing (PnP source 0x01) | Kindle: Settings > Bluetooth / page turner, pair "Amazon Kindle Remote" | Pairs; "[SEC] encrypted=1 bonded=1"; Kindle shows "connected" popup |
| 2 | GATT reads | Watch serial log | Kindle reads PnP ID and firmware revision; subscribes to keyboard, consumer, battery |
| 3 | Page forward / back | `f`, `b` | Pages turn |
| 4 | Orientation swap | Rotate Kindle 180 degrees, `f` | Direction swaps as expected |
| 5 | Brightness | `u`, `d` | Frontlight steps |
| 6 | Sleep | `s`, then `s` again | Kindle sleeps; note whether the second press wakes it |
| 7 | Battery UI | `1` (10%), `0` (0%) | Low / critical battery warning |
| 8 | Reconnect | Reset the ESP32 | Reconnects without re-pairing |
| 9 | Connection parameters | Read the connect log; press `i` | Note interval/latency the Kindle uses and whether it accepts a longer one |

If test 1 fails: unpair on the Kindle, press `c`, press `x` (switch to PnP source 0x02), retry.

Record results in FINDINGS.md section 4.1.

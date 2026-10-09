# /// script
# dependencies = ["bleak"]
# ///
"""Scan BLE for 15 s, list the strongest advertisers, and dump the Kindle remote's
GATT table if found. Run from Terminal.app (needs macOS Bluetooth permission):
    uv run protocol_test/tools/ble_scan.py | tee protocol_test/tools/scan.log
"""
import asyncio

from bleak import BleakClient, BleakScanner

TARGET = "Amazon Kindle Remote"


async def main():
    found = await BleakScanner.discover(timeout=15, return_adv=True)
    print(f"total advertisers: {len(found)}")
    rows = sorted(found.values(), key=lambda da: -da[1].rssi)
    for dev, adv in rows[:20]:
        print(f"{dev.address} rssi={adv.rssi} name={adv.local_name!r} "
              f"uuids={adv.service_uuids} mfr={ {k: v.hex() for k, v in adv.manufacturer_data.items()} }")

    hits = [d for d, a in rows if a.local_name == TARGET or "00001812-0000-1000-8000-00805f9b34fb" in a.service_uuids]
    if not hits:
        print(f"\n{TARGET!r} / HID 0x1812 NOT FOUND")
        return
    dev = hits[0]
    print(f"\nconnecting to {dev.address} ...")
    async with BleakClient(dev) as c:
        for s in c.services:
            print(f"service {s.uuid} {s.description}")
            for ch in s.characteristics:
                val = ""
                if "read" in ch.properties:
                    try:
                        val = (await c.read_gatt_char(ch)).hex()
                    except Exception as e:  # HID reports may need encryption
                        val = f"<{type(e).__name__}>"
                print(f"  char {ch.uuid} {ch.properties} {val}")


asyncio.run(main())

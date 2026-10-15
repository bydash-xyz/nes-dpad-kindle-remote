"""Send serial commands to the test harness without resetting it.
Usage: uv run --with pyserial python protocol_test/tools/send.py f [b u ...]  (each arg sent 3 s apart)"""
import os, sys, time, serial
port = os.environ.get('SERIAL_PORT', '/dev/ttyACM0')
s = serial.Serial(); s.port = port; s.baudrate = 115200
out = b''
for c in sys.argv[1:]:
    s.write(c.encode()); t = time.time()
    while time.time() - t < 3: out += s.read(1024)
print(out.decode(errors='replace'))
open('protocol_test/tools/serial.log', 'ab').write(out)

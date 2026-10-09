"""Send serial commands to the test harness without resetting it.
Usage: uv run --with pyserial python protocol_test/tools/send.py f [b u ...]  (each arg sent 3 s apart)"""
import sys, time, serial
s = serial.Serial(); s.port = '/dev/ttyACM0'; s.baudrate = 115200
s.timeout = 0.3; s.dtr = False; s.rts = False; s.open()
out = b''
for c in sys.argv[1:]:
    s.write(c.encode()); t = time.time()
    while time.time() - t < 3: out += s.read(1024)
print(out.decode(errors='replace'))
open('protocol_test/tools/serial.log', 'ab').write(out)

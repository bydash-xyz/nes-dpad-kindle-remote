"""Keep the serial port open (opening it resets the board via the CH343 auto-reset
lines). Appends board output to tools/serial.log; sends any text appended to
tools/cmd.txt. Usage: uv run --with pyserial python protocol_test/tools/bridge.py"""
import os, sys, time, serial
D = os.path.dirname(os.path.abspath(__file__))
LOG, CMD = os.path.join(D, 'serial.log'), os.path.join(D, 'cmd.txt')
open(CMD, 'w').close()
port = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('SERIAL_PORT', '/dev/ttyACM0')
s = serial.Serial(); s.port = port; s.baudrate = 115200
s.timeout = 0.2; s.dtr = False; s.rts = False; s.open()
pos = 0
with open(LOG, 'a') as log:
    while True:
        d = s.read(1024)
        if d:
            log.write(d.decode(errors='replace')); log.flush()
        with open(CMD) as c:
            c.seek(pos); new = c.read(); pos = c.tell()
        if new:
            s.write(new.encode())

#!/usr/bin/env python3
"""Send a file to the card over the cable with the probe build's CMD:PUT.

  /Users/.../penv/bin/python tools/put_file.py /dev/cu.usbmodemXXXX local.bin /v1052/firmware.bin

The unit prints PUT:READY, takes the bytes in windows of 4096 (each answered by PUT:ACK), checks the
CRC32 and renames <path>.tmp over <path>. Exit 0 on PUT:OK, 1 on PUT:FAIL or a silence. Needs pyserial
(the PlatformIO python has it).
"""
import sys
import time
import zlib

import serial

WINDOW = 4096


def read_line(port, until, timeout):
    """Next line starting with one of `until` (other lines are the unit's log and are skipped)."""
    end = time.time() + timeout
    buf = b''
    while time.time() < end:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            text = line.decode('utf-8', 'replace').strip()
            if text.startswith(until):
                return text
    return None


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    device, local, remote = sys.argv[1:]
    data = open(local, 'rb').read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    port = serial.Serial(device, 115200, timeout=0.05, write_timeout=10)
    port.dtr = True  # the unit's CDC port stays closed to a host that has not raised DTR
    port.reset_input_buffer()
    port.write(f'CMD:PUT {remote} {len(data)} {crc:08x}\n'.encode())
    ready = read_line(port, ('PUT:READY', 'PUT:FAIL'), 10)
    if not ready or ready.startswith('PUT:FAIL'):
        print(ready or 'PUT: no answer', file=sys.stderr)
        sys.exit(1)
    start = time.time()
    sent = 0
    while sent < len(data):
        port.write(data[sent:sent + WINDOW])
        port.flush()
        sent += min(WINDOW, len(data) - sent)
        if sent < len(data):
            answer = read_line(port, ('PUT:ACK', 'PUT:FAIL'), 15)
            if not answer or answer.startswith('PUT:FAIL'):
                print(answer or f'PUT: no ack after {sent} bytes', file=sys.stderr)
                sys.exit(1)
    result = read_line(port, ('PUT:OK', 'PUT:FAIL'), 20)
    print(result or 'PUT: no result')
    print(f'{len(data)} bytes in {time.time() - start:.1f} s, crc {crc:08x}')
    sys.exit(0 if result and result.startswith('PUT:OK') else 1)


if __name__ == '__main__':
    main()

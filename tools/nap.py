#!/usr/bin/env python3
"""Flash a tenor/cross release onto an X3, X4, X4 Classic or X4 Pro over the USB cable.

  <penv>/bin/python tools/nap.py firmware.bin [--serial SERIAL] [--dry-run] [--log DIR]

The image is checked before any port is opened: an ESP app image (a merged or factory image is
refused), one board tag, the chip, and on the X4 Pro the cable flash channel, since an X4 Pro image
without it could never be flashed over the cable again. The device is then found by its USB
identity:

* X3, X4 (ESP32-C3) and X4 Classic answer as the chip's own USB-Serial/JTAG. esptool reads the
  partition table and otadata of the device and writes the image into the app slot it boots now.
* The X4 Pro has its USB download mode fused off. Its release firmware keeps the TCF1 channel
  (src/platform/CableFlash.h): HELLO, BEGIN, a yes on the device screen (or the recovery keys held
  at power on), the image in 1 KB frames, END. It then follows the device through its restart until
  the new build runs and has passed its trial boot, or has rolled back.

--dry-run checks the image, finds the device, asks the X4 Pro HELLO (read only) and prints the plan;
nothing is written and nothing is reset. Needs pyserial and esptool (the PlatformIO python has both).
Exit status: 0 the new build runs (or the plan, with --dry-run), 1 refused or failed, 2 the device
rolled back to its previous build, 3 written but not seen running (or not confirmed: every HELLO came
back cut).
"""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('package_tenor_ota', REPO / 'scripts/package_tenor_ota.py')
package = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(package)

ESPRESSIF_VID = 0x303A
CHIPS = {5: 'ESP32-C3', 9: 'ESP32-S3'}
# How each board takes an image: its chip's USB download mode, or the release's cable channel.
ROUTE = {'x4': 'rom', 'x4c': 'rom', 'x4pro': 'cable'}
NAMES = {'x4': 'X3/X4', 'x4c': 'X4 Classic', 'x4pro': 'X4 Pro'}
DATA, END, ABORT = 1, 2, 3
ACK_SECONDS = 5  # a frame not answered in this time is sent again
HELLO_TRIES = 5  # a HELLO that does not come back whole is asked again, this many times in all
HELLO_SECONDS = 3  # each one waits this long for its answer
# Every field of the device's HELLO (src/platform/CableFlashLink.cpp); a line cut short or with a log line
# glued on does not match, and nothing is concluded from it.
HELLO_LINE = re.compile(
    r'TCF1 HELLO board=(?P<board>[a-z0-9]{1,23}) chip=(?P<chip>\d+) version=(?P<version>[!-~]{1,31}) '
    r'cable=(?P<cable>1) state=(?P<state>valid|trial|new|undefined|invalid|none) elf=(?P<elf>[0-9a-f]{16}|-) '
    r'running=(?P<running>[\w-]+@0x[0-9a-f]+\+0x[0-9a-f]+) boot=(?P<boot>[\w-]+) '
    r'next=(?P<next>[\w-]+@0x[0-9a-f]+\+0x[0-9a-f]+) frame=(?P<frame>\d+)')


class Stop(Exception):
    def __init__(self, message, code=1):
        super().__init__(message)
        self.code = code


class Log:
    def __init__(self, folder):
        self.file = None
        if folder:
            Path(folder).mkdir(parents=True, exist_ok=True)
            self.file = (Path(folder) / 'nap.jsonl').open('a')

    def __call__(self, event, **fields):
        fields.update(event=event, t=round(time.time(), 3))
        if self.file:
            self.file.write(json.dumps(fields) + '\n')
            self.file.flush()


def say(text):
    print(text, flush=True)


# ---- the image -----------------------------------------------------------------------------

class Image:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        try:
            package.validate_image(self.data, limit=len(self.data))
        except ValueError as error:
            raise Stop(f'{self.path.name}: not an app image ({error}). Give the app .bin, not a merged or factory image.')
        self.chip = int.from_bytes(self.data[12:14], 'little')
        boards = re.findall(rb'CROSSPOINT-BOARD-V1:([a-z0-9]{1,23});', self.data)
        if len(boards) != 1 or self.data.count(b'CROSSPOINT-BOARD-V1:') != 1:
            raise Stop(f'{self.path.name}: no single tenor/cross board tag')
        self.board = boards[0].decode()
        if self.board not in ROUTE or CHIPS.get(self.chip) is None:
            raise Stop(f'{self.path.name}: board {self.board} on chip {self.chip} is not flashed by this tool')
        fields = package.tag_fields(self.data, self.board)
        versions = [f[len(b'TENOR-CROSS-VERSION-V1:'):] for f in fields if f.startswith(b'TENOR-CROSS-VERSION-V1:')]
        self.version = versions[0].decode() if len(versions) == 1 and 0 < len(versions[0]) < 32 else None
        self.cable = package.keeps_cable(self.data, self.board)
        if self.board in package.CABLE_BOARDS and not self.cable:
            raise Stop(f'{self.path.name}: this {NAMES[self.board]} image has no cable flash channel; once on the '
                       f'device it could not be flashed over the cable again. Refused.')
        if ROUTE[self.board] == 'cable' and not self.version:
            raise Stop(f'{self.path.name}: no product version in its tag')
        # esp_app_desc_t opens the first segment; its app_elf_sha256 is at offset 144.
        self.elf = self.data[176:208].hex() if self.data[32:36] == b'\x32\x54\xcd\xab' else ''
        self.sha = hashlib.sha256(self.data).hexdigest()

    def describe(self):
        return (f'{self.path.name}: {NAMES[self.board]} ({CHIPS[self.chip]}), {self.version or "no version"}, '
                f'{len(self.data)} B, sha256 {self.sha[:16]}, cable channel {"yes" if self.cable else "no"}')


# ---- the device on the cable ---------------------------------------------------------------

class Port:
    def __init__(self, device, serial, product, route):
        self.device, self.serial, self.product, self.route = device, serial, product, route

    def __repr__(self):
        return f'{self.device} serial={self.serial or "-"} product={self.product or "-"}'


def espressif_ports(list_ports):
    """Every Espressif USB port: the chip's own USB-Serial/JTAG (product 'USB JTAG/serial debug unit',
    VID/PID 303a:1001 on both C3 and S3) or the X4 Pro release's TinyUSB CDC (product CrossPoint_X4_Pro,
    the same VID/PID). Only the product string tells them apart."""
    found = []
    for p in list_ports():
        if p.vid != ESPRESSIF_VID:
            continue
        product = p.product or p.description or ''
        route = 'rom' if 'JTAG' in product else 'cable' if 'X4_Pro' in product else None
        found.append(Port(p.device, p.serial_number or '', product, route))
    return found


def choose(ports, image, serial):
    want = ROUTE[image.board]
    if serial:
        ports = [p for p in ports if p.serial.startswith(serial)]
    matches = [p for p in ports if p.route == want]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        raise Stop('Several devices could take this image: ' + '; '.join(map(repr, matches)) +
                   '. Pick one with --serial.')
    if not ports:
        raise Stop('No device found on the USB cable' + (f' with serial {serial}' if serial else '') +
                   '. An X4 Pro in USB Drive mode has no serial port: leave USB Drive first.')
    other = [p for p in ports if p.route]
    if other:
        raise Stop(f'This image is for the {NAMES[image.board]}; the device on the cable is '
                   f'{"an X4 Pro" if other[0].route == "cable" else "an X3, X4 or X4 Classic"} ({other[0]!r}).')
    raise Stop('Unknown Espressif device: ' + '; '.join(map(repr, ports)))


# ---- X4 Pro: the TCF1 cable channel --------------------------------------------------------

def frame(kind, session, seq, offset, payload=b''):
    head = b'TCF1' + bytes([kind, 0]) + struct.pack('<HIII', len(payload), session, seq, offset)
    return head + struct.pack('<I', zlib.crc32(payload, zlib.crc32(head)) & 0xFFFFFFFF) + payload


class Link:
    """Lines from the device; anything before "TCF1 " is its log and is kept apart (a log line cut short
    by the device loses its newline, and the TCF1 line that follows lands on the same line)."""

    def __init__(self, serial_port, log):
        self.s, self.log, self.buf = serial_port, log, b''

    def send(self, data):
        self.s.write(data if isinstance(data, bytes) else (data + '\n').encode())

    def expect(self, prefixes, timeout, skip=lambda line: False):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            while b'\n' in self.buf:
                raw, self.buf = self.buf.split(b'\n', 1)
                line = raw.decode('utf-8', 'replace').strip()
                at = line.rfind('TCF1 ')
                if at > 0:
                    self.log('device', text=line[:at])
                    line = line[at:]
                if at >= 0:
                    self.log('line', text=line)
                    if line.startswith(prefixes) and not skip(line):
                        return line
                elif line:
                    self.log('device', text=line)
            self.buf += self.s.read(self.s.in_waiting or 1)
        return None


def open_port(device, serial_module):
    s = serial_module.Serial()
    s.port, s.baudrate, s.timeout, s.write_timeout = device, 115200, 0.05, 10
    s.dtr, s.rts = True, False  # opens without a reset; the device's CDC talks to a host that raised DTR
    s.open()
    return s


def hello(link, tries=HELLO_TRIES):
    """(fields of a whole HELLO or None, how many came back cut)."""
    cut = 0
    for attempt in range(tries):
        if attempt:
            time.sleep(0.5)
        link.send('TCF1 HELLO')
        line = link.expect(('TCF1 HELLO',), HELLO_SECONDS)
        if line is None:
            continue
        match = HELLO_LINE.fullmatch(line)
        if match:
            return match.groupdict(), cut
        cut += 1
        link.log('cut-hello', text=line)
    return None, cut


MESSAGES = {
    'denied': 'The update was declined on the device, or nobody answered within 60 s.',
    'trial': 'The device runs a new build on trial. Wait until it has passed (about 30 s) or rolled back, then retry.',
    'busy': 'An update screen is already open on the device. Close it, then retry.',
    'no-cable': 'The device refused an image without the cable flash channel.',
    'wrong-board': 'The device refused an image for another board.',
    'wrong-chip': 'The device refused an image for another chip.',
    'silence': 'The device stopped hearing the computer.',
}


def flash_cable(image, port, args, log, serial_module, list_ports):
    link = Link(open_port(port.device, serial_module), log)
    status, cut = hello(link)
    if status is None and cut:
        raise Stop(f'The X4 Pro answers TCF1 HELLO, but {cut} of {HELLO_TRIES} answers came back cut short and none '
                   f'whole. Nothing was written; run the tool again.')
    if status is None:
        raise Stop(f'The X4 Pro does not answer TCF1 HELLO ({HELLO_TRIES} tries). It may be busy (an update '
                   f'download, sleep): wait until it shows Home, then retry. v1.0.58 and older have no cable flash '
                   f'channel; put v1.0.59 or newer on them from the card or over the network.')
    log('hello', **status)
    say(f'Device: X4 Pro {status.get("version")} ({status.get("state")}), running {status.get("running")}, '
        f'next slot {status.get("next")}')
    if status.get('board') != image.board or status.get('chip') != str(image.chip):
        raise Stop(f'The device is board {status.get("board")} chip {status.get("chip")}; refused before writing.')
    slot = status['next']
    slot_size = int(slot.split('+')[1], 16)
    if len(image.data) > slot_size:
        raise Stop(f'The image ({len(image.data)} B) is larger than the slot the device has ({slot_size} B).')
    if status.get('state') == 'trial':
        raise Stop(MESSAGES['trial'])
    if image.elf and status.get('elf') == image.elf[:16]:
        say('The device already runs this build; it is flashed again.')
    if args.dry_run:
        say(f'Plan: TCF1 BEGIN {len(image.data)} B into {slot}, ask on the device, '
            f'{-(-len(image.data) // 1024)} frames, END, restart on trial. Nothing written (--dry-run).')
        return 0
    link.send(f'TCF1 BEGIN {len(image.data)} {image.sha} {image.version}')
    answer = link.expect(('TCF1 WAIT', 'TCF1 READY', 'TCF1 FAIL'), 5)
    if answer and answer.startswith('TCF1 WAIT'):
        say(f'Tap "Update firmware" on the device ({answer.split()[2]} s).')
        answer = link.expect(('TCF1 READY', 'TCF1 FAIL'), int(answer.split()[2]) + 15)
    if not answer or answer.startswith('TCF1 FAIL'):
        why = answer.split()[2] if answer else 'no answer'
        raise Stop(MESSAGES.get(why, f'The device did not start the update: {why}.'))
    session, payload = int(answer.split()[2], 16), int(answer.split()[3])
    started = time.monotonic()
    offset = seq = 0
    shown = -1
    while offset < len(image.data):
        chunk = image.data[offset:offset + payload]
        for attempt in range(8):
            link.send(frame(DATA, session, seq, offset, chunk))
            reply = link.expect(('TCF1 ACK', 'TCF1 NAK', 'TCF1 FAIL'), ACK_SECONDS,
                                skip=lambda line: line.startswith('TCF1 ACK') and int(line.split()[2]) < seq)
            if reply and reply.startswith('TCF1 ACK') and int(reply.split()[2]) == seq:
                break
            if reply and reply.startswith('TCF1 FAIL'):
                why = reply.split()[2]
                raise Stop(MESSAGES.get(why, f'The device stopped the update: {why}.'))
            if reply and reply.startswith('TCF1 NAK') and int(reply.split()[2]) == seq + 1:
                break  # it has this frame already
            log('resend', seq=seq, reply=reply)
        else:
            link.send(frame(ABORT, session, seq, offset))
            raise Stop(f'No answer for the frame at {offset} after 8 tries; the update was stopped.')
        offset += len(chunk)
        seq += 1
        percent = offset * 100 // len(image.data)
        if percent // 10 != shown // 10:
            shown = percent
            say(f'  {percent}% ({offset} B, {time.monotonic() - started:.1f} s)')
    link.send(frame(END, session, seq, offset))
    done = link.expect(('TCF1 DONE', 'TCF1 FAIL'), 60)
    if not done or done.startswith('TCF1 FAIL'):
        why = done.split()[2] if done else 'no answer'
        raise Stop(MESSAGES.get(why, f'The device did not take the image: {why}.'))
    elapsed = time.monotonic() - started
    log('done', seconds=round(elapsed, 2), bytes=len(image.data))
    say(f'WRITTEN: {len(image.data)} B in {elapsed:.1f} s. The device restarts on trial.')
    link.s.close()
    return follow(image, port, slot.split('@')[0], log, serial_module, list_ports)


def follow(image, port, target, log, serial_module, list_ports, wait=90, cut_cap=3 * HELLO_TRIES):
    """After DONE the device restarts: wait for its serial to go and come back, then ask until its trial
    boot has passed (state valid) or it booted the previous build again. Only a whole HELLO counts: the
    new build runs when it runs from `target` (the slot just written) with the image's ELF hash."""
    start = time.monotonic()
    gone = trial_said = False
    cuts = 0
    while time.monotonic() - start < wait:
        time.sleep(1)
        present = [p for p in espressif_ports(list_ports) if p.serial == port.serial and p.route == 'cable']
        if not present:
            gone = True
            continue
        if not gone and time.monotonic() - start < 15:
            continue  # still the old build, before its restart
        try:
            link = Link(open_port(present[0].device, serial_module), log)
            status, cut = hello(link)
            link.s.close()
        except (OSError, serial_module.SerialException):
            continue
        cuts += cut
        if not status:
            if cuts >= cut_cap:
                break
            continue
        log('after', **status)
        running = status['running'].split('@')[0]
        if running != target or (image.elf and status['elf'] != image.elf[:16]):
            say(f'ROLLED BACK: the device runs {status["version"]} from {running} (elf {status["elf"]}), not the '
                f'image written into {target}.')
            return 2
        if status.get('state') == 'trial':
            if not trial_said:
                say('ON TRIAL: the new build is running its boot check.')
                trial_said = True
            continue
        say(f'VALID: the device runs {status.get("version")} ({status.get("state")}).')
        return 0
    if cuts:
        say(f'NOT CONFIRMED: the image was written; after its restart the device answered HELLO {cuts} times, every '
            f'answer cut short, so which build runs is not known. Ask again with --dry-run.')
        return 3
    say('NOT SEEN: the image was written but the device did not answer after its restart.')
    return 3


# ---- X3, X4 and X4 Classic: the chip's USB download mode -------------------------------------

def partitions(table):
    rows = {}
    for at in range(0, len(table) - 31, 32):
        entry = table[at:at + 32]
        if entry[:2] != b'\xaa\x50':
            break
        kind, subtype, offset, size = struct.unpack_from('<BBII', entry, 2)
        rows[entry[12:28].rstrip(b'\0').decode('ascii', 'replace')] = (kind, subtype, offset, size)
    return rows


def boot_slot(rows, otadata):
    """The app slot the bootloader picks: the otadata entry with a good CRC, not invalid or aborted, and
    the highest sequence, as src/network/OtaBootSwitch.cpp reads it."""
    apps = sorted((v for v in rows.values() if v[0] == 0 and 0x10 <= v[1] <= 0x1F), key=lambda v: v[1])
    if not apps:
        raise Stop('The partition table has no OTA app slot; this tool does not flash that layout.')
    best = None
    for at in (0, 0x1000):
        seq, state, crc = struct.unpack_from('<I20xII', otadata, at)
        if seq in (0, 0xFFFFFFFF) or state in (3, 4) or crc != zlib.crc32(struct.pack('<I', seq), 0xFFFFFFFF):
            continue
        if best is None or seq > best:
            best = seq
    return apps[0] if best is None else apps[(best - 1) % len(apps)]


def esptool(args, run):
    result = run([sys.executable, '-m', 'esptool', *args])
    if result.returncode != 0:
        raise Stop('esptool failed: ' + (result.stdout or '')[-400:])
    return result.stdout or ''


def flash_rom(image, port, args, log, run):
    say(f'Device: {port!r}, through the chip\'s USB download mode.')
    if args.dry_run:
        say(f'Plan: esptool reads the partition table and otadata of {port.device}, checks the chip is '
            f'{CHIPS[image.chip]}, writes {len(image.data)} B into the app slot the device boots now and resets it. '
            f'Nothing written (--dry-run).')
        return 0
    with tempfile.TemporaryDirectory() as folder:
        table_file, ota_file = Path(folder) / 'table.bin', Path(folder) / 'otadata.bin'
        out = esptool(['--port', port.device, '--before', 'default-reset', '--after', 'no-reset',
                       'read-flash', '0x8000', '0xc00', str(table_file)], run)
        chip = re.search(r'\b(ESP32-[CS]\d)\b', out)
        if not chip or chip.group(1) != CHIPS[image.chip]:
            raise Stop(f'The chip on the cable is {chip.group(1) if chip else "unknown"}; the image is for '
                       f'{CHIPS[image.chip]}. Nothing written.')
        rows = partitions(table_file.read_bytes())
        ota = [v for v in rows.values() if v[0] == 1 and v[1] == 0]
        if not ota:
            raise Stop('No otadata partition: this tool does not flash that layout. Nothing written.')
        esptool(['--port', port.device, '--before', 'default-reset', '--after', 'no-reset',
                 'read-flash', hex(ota[0][2]), hex(ota[0][3]), str(ota_file)], run)
        slot = boot_slot(rows, ota_file.read_bytes())
    log('rom-plan', chip=CHIPS[image.chip], slot=hex(slot[2]), size=slot[3])
    if len(image.data) > slot[3]:
        raise Stop(f'The image ({len(image.data)} B) is larger than the boot slot ({slot[3]} B). Nothing written.')
    say(f'Writing {len(image.data)} B at {hex(slot[2])} (the slot the device boots).')
    esptool(['--port', port.device, '--before', 'default-reset', '--after', 'hard-reset',
             'write-flash', hex(slot[2]), str(image.path)], run)
    say('DONE: written and verified by esptool; the device restarts.')
    return 0


def main(argv=None, list_ports=None, serial_module=None, run=None):
    cli = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    cli.add_argument('image', type=Path)
    cli.add_argument('--serial', help='USB serial number (or its start) of the device to flash')
    cli.add_argument('--dry-run', action='store_true', help='check and plan only; nothing is written or reset')
    cli.add_argument('--log', help='folder for nap.jsonl, every line the device answered')
    args = cli.parse_args(argv)
    if serial_module is None:
        import serial as serial_module
        import serial.tools.list_ports
        list_ports = serial.tools.list_ports.comports
    run = run or (lambda cmd: subprocess.run(cmd, capture_output=True, text=True, timeout=600))
    log = Log(args.log)
    try:
        image = Image(args.image)
        say(image.describe())
        log('image', board=image.board, chip=image.chip, version=image.version, size=len(image.data), sha=image.sha)
        port = choose(espressif_ports(list_ports), image, args.serial)
        log('port', device=port.device, serial=port.serial, product=port.product)
        if port.route == 'cable':
            return flash_cable(image, port, args, log, serial_module, list_ports)
        return flash_rom(image, port, args, log, run)
    except Stop as stop:
        log('stop', message=str(stop))
        say(f'STOPPED: {stop}')
        return stop.code


if __name__ == '__main__':
    sys.exit(main())

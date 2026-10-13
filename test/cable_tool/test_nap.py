"""tools/nap.py against fake USB ports, a fake X4 Pro that speaks TCF1 and a fake esptool."""
import contextlib
import hashlib
import importlib.util
import io
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('nap', REPO / 'tools/nap.py')
nap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nap)
nap.time.sleep = lambda s: None
nap.ACK_SECONDS = 0.2
nap.HELLO_SECONDS = 0.2

PRO_TAG = b'CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:v1.0.59;TENOR-CABLE-V1:1;'
OLD_PRO_TAG = b'CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:v1.0.58;'
C3_TAG = b'CROSSPOINT-BOARD-V1:x4;TENOR-CROSS-VERSION-V1:v1.0.59;'
# Raw lines the X4 Pro sent on 13/10 (cap59/may/b2-tu3-1, b2-ve-do59-s1/nap.jsonl): a HELLO cut at its
# 64th byte with the next log line glued on, and a log line cut at its 64th byte with the HELLO glued on.
CUT_HELLO = 'TCF1 HELLO board=x4pro chip=9 version=v1.0.58 cable=1 state=vali[12477] [INF] [BAT] soc=100 prev=100 task=loopTask\n'
GLUED_HELLO = ('[341535] [INF] [MEM] PSRAM: Free: 8274224 bytes, Total: 8388608 TCF1 HELLO board=x4pro chip=9 '
               'version=v1.0.59-rc.1 cable=1 state=valid elf=d1c24df6ec6a9ff3 running=app0@0x10000+0x7e0000 '
               'boot=app0 next=app1@0x7f0000+0x7e0000 frame=1024\n')


def image(tag=PRO_TAG, chip=9, body=5000, elf=b'\x11' * 32, desc=True):
    """An ESP app image: header, one segment opening with esp_app_desc_t, checksum, SHA-256."""
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into('<H', header, 12, chip)
    header[23] = 1
    segment = bytearray(body)
    if desc:
        struct.pack_into('<I', segment, 0, 0xABCD5432)
    segment[144:176] = elf
    segment[1000:1000 + len(tag)] = tag
    data = header + struct.pack('<II', 0x3C000020, len(segment)) + segment
    checksum = 0xEF
    for byte in segment:
        checksum ^= byte
    data += bytes(15 - len(data) % 16) + bytes([checksum])
    return bytes(data + hashlib.sha256(data).digest())


class ListedPort:
    def __init__(self, device, serial, product, vid=0x303A):
        self.device, self.serial_number, self.product, self.description, self.vid = device, serial, product, product, vid


class FakeX4Pro:
    """Answers TCF1 as src/platform/CableFlash.h does, with knobs for the cases under test."""

    def __init__(self, cable=True, state='valid', elf='aaaaaaaaaaaaaaaa', answer='yes', drop_ack=(), nak=(),
                 end='DONE', after=None, running='app0', hellos=()):
        self.cable, self.state, self.elf, self.answer = cable, state, elf, answer
        self.running, self.hellos = running, list(hellos)  # hellos: raw answers sent before whole ones
        self.drop_ack, self.nak, self.end, self.after = set(drop_ack), set(nak), end, after or []
        self.out = b''
        self.inbox = b''
        self.written = bytearray()
        self.frames = 0
        self.began = False
        self.lines = []

    def hello(self):
        if self.hellos:
            return self.hellos.pop(0)
        slots = {'app0': 'app0@0x10000+0x7e0000', 'app1': 'app1@0x7f0000+0x7e0000'}
        other = 'app1' if self.running == 'app0' else 'app0'
        return (f'TCF1 HELLO board=x4pro chip=9 version=v1.0.59 cable=1 state={self.state} elf={self.elf} '
                f'running={slots[self.running]} boot={self.running} next={slots[other]} frame=1024\n')

    def receive(self, data):
        self.inbox += data
        while self.inbox:
            if self.began and self.inbox.startswith(b'TCF1') and len(self.inbox) >= 5 and self.inbox[4] in (1, 2, 3):
                if len(self.inbox) < 24:
                    return
                length = struct.unpack_from('<H', self.inbox, 6)[0]
                if len(self.inbox) < 24 + length:
                    return
                frame, self.inbox = self.inbox[:24 + length], self.inbox[24 + length:]
                self.on_frame(frame)
                continue
            if b'\n' not in self.inbox:
                return
            line, self.inbox = self.inbox.split(b'\n', 1)
            self.on_line(line.decode())

    def on_line(self, line):
        self.lines.append(line)
        if not self.cable:
            return
        if line == 'TCF1 HELLO':
            self.out += self.hello().encode()
        elif line.startswith('TCF1 BEGIN '):
            self.size, self.sha, self.version = line.split()[2:]
            self.out += b'I (12) [CABLE] begin\n'  # the device log shares the line
            if self.answer == 'yes':
                self.out += b'TCF1 WAIT 60\nTCF1 READY 0000beef 1024\n'
                self.began = True
            else:
                self.out += f'TCF1 WAIT 60\nTCF1 FAIL {self.answer}\n'.encode()

    def on_frame(self, frame):
        kind, length, session, seq, offset, crc = frame[4], *struct.unpack_from('<HIIII', frame, 6)
        payload = frame[24:]
        assert session == 0xBEEF
        assert crc == zlib.crc32(payload, zlib.crc32(frame[:20])) & 0xFFFFFFFF
        if kind == 2:
            ok = self.end == 'DONE' and len(self.written) == int(self.size) and \
                hashlib.sha256(self.written).hexdigest() == self.sha
            self.out += (f'TCF1 DONE {len(self.written)}\n' if ok else f'TCF1 FAIL {self.end}\n').encode()
            return
        self.frames += 1
        if seq in self.nak:
            self.nak.discard(seq)
            self.out += f'TCF1 NAK {seq} crc\n'.encode()
            return
        if offset == len(self.written):
            self.written += payload
        if seq in self.drop_ack:
            self.drop_ack.discard(seq)
            return
        self.out += f'TCF1 ACK {seq} {len(self.written)}\n'.encode()


class FakeSerialModule:
    """pyserial as nap.py uses it; each open() of a device talks to the fake that owns it."""

    class SerialException(Exception):
        pass

    def __init__(self, devices):
        self.devices = devices
        self.opened = []
        test = self

        class Serial:
            def __init__(self):
                self.port = None
                self.dtr = self.rts = None

            def open(self):
                assert self.dtr is True and self.rts is False, 'opening must not reset the device'
                self.device = test.devices[self.port]
                if callable(self.device):
                    self.device = test.devices[self.port] = self.device()
                test.opened.append(self.port)

            def write(self, data):
                self.device.receive(bytes(data))

            @property
            def in_waiting(self):
                return len(self.device.out)

            def read(self, n):
                data, self.device.out = self.device.out[:n], self.device.out[n:]
                return data

            def close(self):
                pass

        self.Serial = Serial


class Ports:
    """USB listing that can change between calls (a device that restarts goes away, then comes back)."""

    def __init__(self, *frames):
        self.frames = list(frames)

    def __call__(self):
        return self.frames.pop(0) if len(self.frames) > 1 else self.frames[0]


PRO = ListedPort('/dev/cu.usbmodem1C2904243F2C2', '1C2904243F2C', 'CrossPoint_X4_Pro')
PRO2 = ListedPort('/dev/cu.usbmodemAA2', 'AA0011223344', 'CrossPoint_X4_Pro')
C3 = ListedPort('/dev/cu.usbmodem83301', '83301', 'USB JTAG/serial debug unit')


class NapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def file(self, data, name='firmware.bin'):
        path = Path(self.tmp.name) / name
        path.write_bytes(data)
        return str(path)

    def nap(self, argv, ports, devices=None, run=None):
        serial_module = FakeSerialModule(devices or {})
        out = io.StringIO()
        calls = []

        def esptool(cmd):
            calls.append(cmd)
            return run(cmd) if run else self.fail('esptool must not run')
        with contextlib.redirect_stdout(out):
            code = nap.main(argv, list_ports=ports, serial_module=serial_module, run=esptool)
        return code, out.getvalue(), serial_module, calls

    # -- the image, before any port

    def test_image_without_cable_channel_or_not_an_app_is_refused_offline(self):
        for data, why in ((image(OLD_PRO_TAG), 'no cable flash channel'),
                          (image(desc=False), 'not an app image'),
                          (image(PRO_TAG + b'CROSSPOINT-BOARD-V1:x4pro;'), 'no single'),
                          (image(b'CROSSPOINT-BOARD-V1:sticky;', chip=9), 'not flashed by this tool')):
            with self.subTest(why=why):
                code, out, serial_module, _ = self.nap([self.file(data)], Ports([PRO]), {PRO.device: FakeX4Pro})
                self.assertEqual(code, 1)
                self.assertIn(why, out)
                self.assertEqual(serial_module.opened, [])

    # -- which device

    def test_two_devices_with_one_usb_identity_need_a_serial(self):
        devices = {PRO.device: FakeX4Pro, PRO2.device: FakeX4Pro}
        code, out, serial_module, _ = self.nap([self.file(image()), '--dry-run'], Ports([PRO, PRO2]), devices)
        self.assertEqual(code, 1)
        self.assertIn('--serial', out)
        self.assertEqual(serial_module.opened, [])
        code, out, serial_module, _ = self.nap([self.file(image()), '--dry-run', '--serial', 'AA00'],
                                               Ports([PRO, PRO2]), devices)
        self.assertEqual(code, 0, out)
        self.assertEqual(serial_module.opened, [PRO2.device])

    def test_an_image_for_the_other_family_is_refused_before_any_port_opens(self):
        code, out, serial_module, _ = self.nap([self.file(image())], Ports([C3]))
        self.assertEqual(code, 1)
        self.assertIn('X3, X4 or X4 Classic', out)
        code, out, serial_module, _ = self.nap([self.file(image(C3_TAG, chip=5))], Ports([PRO]), {PRO.device: FakeX4Pro})
        self.assertEqual(code, 1)
        self.assertIn('an X4 Pro', out)
        self.assertEqual(serial_module.opened, [])

    # -- X4 Pro over TCF1

    def test_dry_run_asks_hello_and_writes_nothing(self):
        devices = {PRO.device: FakeX4Pro}
        code, out, serial_module, _ = self.nap([self.file(image()), '--dry-run'], Ports([PRO]), devices)
        self.assertEqual(code, 0, out)
        device = serial_module.devices[PRO.device]
        self.assertEqual(device.lines, ['TCF1 HELLO'])
        self.assertIn('Nothing written', out)

    def test_an_x4pro_that_does_not_answer_is_not_called_old(self):
        # Silence also comes from a busy device (an update download, sleep): say that first.
        code, out, _, _ = self.nap([self.file(image())], Ports([PRO]), {PRO.device: lambda: FakeX4Pro(cable=False)})
        self.assertEqual(code, 1)
        self.assertIn('does not answer', out)
        self.assertIn('busy', out)
        self.assertNotIn('its firmware has no cable flash channel', out)

    def test_a_whole_image_with_a_lost_ack_and_a_nak_reaches_valid(self):
        data = image(body=5000, elf=b'\x22' * 32)
        after = FakeX4Pro(elf='22' * 8, running='app1')
        first = FakeX4Pro(drop_ack={1}, nak={3})
        devices = {PRO.device: first}
        ports = Ports([PRO], [], [PRO])
        serial_module = FakeSerialModule(devices)

        def reopened(port_name):
            devices[PRO.device] = after
        orig_open = serial_module.Serial.open

        def open_after_restart(self):
            if first.frames and serial_module.opened:
                reopened(self.port)
            orig_open(self)
        serial_module.Serial.open = open_after_restart
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = nap.main([self.file(data)], list_ports=ports, serial_module=serial_module, run=None)
        self.assertEqual(code, 0, out.getvalue())
        self.assertEqual(bytes(first.written), data)
        self.assertIn('VALID', out.getvalue())
        self.assertIn('Tap "Update firmware"', out.getvalue())

    def test_a_refusal_on_the_device_is_reported(self):
        for answer, words in (('denied', 'declined'), ('trial', 'on trial'), ('busy', 'already open')):
            with self.subTest(answer=answer):
                code, out, _, _ = self.nap([self.file(image())], Ports([PRO]),
                                           {PRO.device: lambda a=answer: FakeX4Pro(answer=a)})
                self.assertEqual(code, 1)
                self.assertIn(words, out)

    def test_a_device_on_trial_is_not_written(self):
        code, out, serial_module, _ = self.nap([self.file(image())], Ports([PRO]),
                                               {PRO.device: lambda: FakeX4Pro(state='trial')})
        self.assertEqual(code, 1)
        self.assertNotIn('TCF1 BEGIN', ' '.join(serial_module.devices[PRO.device].lines))

    def test_a_rollback_after_the_restart_is_reported(self):
        data = image(elf=b'\x33' * 32)
        first = FakeX4Pro()
        devices = {PRO.device: first}
        serial_module = FakeSerialModule(devices)
        orig_open = serial_module.Serial.open

        def open_after_restart(self):
            if first.frames:
                devices[PRO.device] = FakeX4Pro(elf='aaaaaaaaaaaaaaaa')  # the old build again
            orig_open(self)
        serial_module.Serial.open = open_after_restart
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = nap.main([self.file(data)], list_ports=Ports([PRO], [], [PRO]), serial_module=serial_module)
        self.assertEqual(code, 2, out.getvalue())
        self.assertIn('ROLLED BACK', out.getvalue())

    # -- HELLO lines that reach the computer cut short or glued to the device log (measured 13/10)

    def test_a_cut_hello_is_asked_again_and_never_read_as_its_fields(self):
        devices = {PRO.device: lambda: FakeX4Pro(hellos=[CUT_HELLO])}
        code, out, serial_module, _ = self.nap([self.file(image()), '--dry-run'], Ports([PRO]), devices)
        self.assertEqual(code, 0, out)
        self.assertEqual(serial_module.devices[PRO.device].lines, ['TCF1 HELLO', 'TCF1 HELLO'])
        self.assertIn('next slot app1@0x7f0000+0x7e0000', out)

    def test_a_hello_after_a_cut_log_line_is_read(self):
        devices = {PRO.device: lambda: FakeX4Pro(hellos=[GLUED_HELLO])}
        code, out, serial_module, _ = self.nap([self.file(image()), '--dry-run'], Ports([PRO]), devices)
        self.assertEqual(code, 0, out)
        self.assertEqual(serial_module.devices[PRO.device].lines, ['TCF1 HELLO'])

    def test_only_cut_hellos_stop_without_saying_the_channel_is_missing(self):
        devices = {PRO.device: lambda: FakeX4Pro(hellos=[CUT_HELLO] * 50)}
        code, out, serial_module, _ = self.nap([self.file(image())], Ports([PRO]), devices)
        self.assertEqual(code, 1)
        self.assertIn('cut short', out)
        self.assertNotIn('no cable flash channel', out)
        self.assertNotIn('larger than the slot', out)
        self.assertFalse([line for line in serial_module.devices[PRO.device].lines if 'BEGIN' in line])
        self.assertEqual(len(serial_module.devices[PRO.device].lines), nap.HELLO_TRIES)

    def flash_then_restart(self, data, after):
        first = FakeX4Pro()
        devices = {PRO.device: first}
        serial_module = FakeSerialModule(devices)
        orig_open = serial_module.Serial.open

        def open_after_restart(self):
            if first.frames and devices[PRO.device] is first:
                devices[PRO.device] = after
            orig_open(self)
        serial_module.Serial.open = open_after_restart
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = nap.main([self.file(data)], list_ports=Ports([PRO], [], [PRO]), serial_module=serial_module)
        return code, out.getvalue()

    def test_a_cut_hello_after_the_restart_is_not_a_rollback(self):
        # b2-tu3-1: written, the new build valid on the device, the tool said ROLLED BACK (elf None).
        data = image(elf=b'\x44' * 32)
        code, out = self.flash_then_restart(data, FakeX4Pro(elf='44' * 8, running='app1', hellos=[CUT_HELLO]))
        self.assertEqual(code, 0, out)
        self.assertIn('VALID', out)
        self.assertNotIn('ROLLED BACK', out)

    def test_only_cut_hellos_after_the_restart_are_not_confirmed(self):
        data = image(elf=b'\x44' * 32)
        code, out = self.flash_then_restart(data, FakeX4Pro(elf='44' * 8, running='app1', hellos=[CUT_HELLO] * 500))
        self.assertEqual(code, 3, out)
        self.assertIn('NOT CONFIRMED', out)
        self.assertNotIn('ROLLED BACK', out)

    def test_the_same_build_flashed_again_is_judged_by_the_slot_it_runs_from(self):
        same = 'aaaaaaaaaaaaaaaa'  # the build the device runs already, its ELF hash unchanged
        data = image(elf=bytes.fromhex(same) + b'\x00' * 24)
        code, out = self.flash_then_restart(data, FakeX4Pro(elf=same, running='app1'))
        self.assertEqual(code, 0, out)
        code, out = self.flash_then_restart(data, FakeX4Pro(elf=same, running='app0'))
        self.assertEqual(code, 2, out)
        self.assertIn('ROLLED BACK', out)

    def test_frames_are_the_bytes_the_device_reads(self):
        # The same bytes test/cable_flash/SessionTest.cpp feeds the device's own receive().
        self.assertEqual(nap.frame(nap.DATA, 0x5EED1234, 0, 0, b'abc').hex(),
                         '54434631010003003412ed5e0000000000000000297516ab616263')

    # -- X3, X4, X4 Classic through esptool

    def table(self):
        rows = [(b'nvs', 1, 2, 0x9000, 0x5000), (b'otadata', 1, 0, 0xE000, 0x2000),
                (b'app0', 0, 0x10, 0x10000, 0x640000), (b'app1', 0, 0x11, 0x650000, 0x640000)]
        out = b''.join(b'\xaa\x50' + struct.pack('<BBII', k, s, o, n) + label.ljust(16, b'\0') + bytes(4)
                       for label, k, s, o, n in rows)
        return out + b'\xff' * (0xC00 - len(out))

    def otadata(self, seqs):
        out = bytearray(b'\xff' * 0x2000)
        for at, (seq, state, good) in zip((0, 0x1000), seqs):
            crc = zlib.crc32(struct.pack('<I', seq), 0xFFFFFFFF) ^ (0 if good else 1)
            struct.pack_into('<I20xII', out, at, seq, state, crc)
        return bytes(out)

    def esptool(self, chip, seqs):
        def run(cmd):
            args = cmd[3:]
            if 'read-flash' in args:
                start = args.index('read-flash')
                Path(args[start + 3]).write_bytes(self.table() if args[start + 1] == '0x8000' else self.otadata(seqs))
            return type('R', (), {'returncode': 0, 'stdout': f'Connecting...\nChip type: {chip} (QFN32)\n'})()
        return run

    def test_rom_path_writes_the_slot_the_device_boots(self):
        for seqs, offset in ((((2, 0xFFFFFFFF, True), (1, 0xFFFFFFFF, True)), '0x650000'),
                             (((1, 0xFFFFFFFF, True), (2, 3, True)), '0x10000'),       # the newer one is invalid
                             (((1, 0xFFFFFFFF, True), (2, 0xFFFFFFFF, False)), '0x10000'),  # bad CRC
                             ((), '0x10000')):                                           # otadata empty
            with self.subTest(seqs=seqs):
                code, out, _, calls = self.nap([self.file(image(C3_TAG, chip=5))], Ports([C3]),
                                               run=self.esptool('ESP32-C3', seqs))
                self.assertEqual(code, 0, out)
                write = [c for c in calls if 'write-flash' in c]
                self.assertEqual(len(write), 1)
                self.assertEqual(write[0][write[0].index('write-flash') + 1], offset)

    def test_rom_path_stops_on_the_wrong_chip_and_dry_run_runs_nothing(self):
        code, out, _, calls = self.nap([self.file(image(C3_TAG, chip=5))], Ports([C3]), run=self.esptool('ESP32-S3', ()))
        self.assertEqual(code, 1)
        self.assertFalse([c for c in calls if 'write-flash' in c])
        self.assertIn('ESP32-S3', out)
        code, out, _, calls = self.nap([self.file(image(C3_TAG, chip=5)), '--dry-run'], Ports([C3]))
        self.assertEqual(code, 0, out)
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()

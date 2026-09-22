"""Check final image bytes against the generated OTA partition table."""

import configparser
import hashlib
from pathlib import Path
import runpy
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FirmwareSizeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cross-image-size-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.image = self.directory / "firmware.bin"
        self.table = self.directory / "partitions.bin"
        script = ROOT / "scripts/check_firmware_size.py"
        self.api = runpy.run_path(str(script)) if script.exists() else {}

    def fixture(self, image_size, slots=(0x640000, 0x640000)):
        with self.image.open("wb") as stream:
            stream.truncate(image_size)
        # Include non-app storage to ensure it does not limit the application.
        entries = [struct.pack("<HBBII16sI", 0x50AA, 1, 2, 0x9000, 0x5000, b"nvs", 0)]
        offset = 0x10000
        for index, size in enumerate(slots):
            entries.append(struct.pack("<HBBII16sI", 0x50AA, 0, 0x10 + index,
                                       offset, size, f"app{index}".encode(), 0))
            offset += size
        payload = b"".join(entries)
        self.table.write_bytes(payload + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(payload).digest())

    def check(self):
        self.assertIn("check_image_size", self.api, "Final BIN size gate is missing")
        return self.api["check_image_size"](self.image, self.table)

    def test_required_footer_bytes_cannot_overflow_the_ota_slot(self):
        self.fixture(6554384)
        with self.assertRaisesRegex(ValueError, "784"):
            self.check()

    def test_exact_reserve_and_one_byte_short(self):
        self.fixture(0x640000 - 131072)
        self.check()
        self.fixture(0x640000 - 131072 + 1)
        with self.assertRaisesRegex(ValueError, "131072"):
            self.check()

    def test_exact_fit_and_one_byte_overflow_fail(self):
        self.fixture(0x640000)
        with self.assertRaises(ValueError):
            self.check()
        self.fixture(0x640001)
        with self.assertRaises(ValueError):
            self.check()

    def test_smaller_second_ota_slot_limits_the_image(self):
        self.fixture(0x630000 - 131072, slots=(0x640000, 0x630000))
        self.check()
        self.fixture(0x630000 - 131072 + 1, slots=(0x640000, 0x630000))
        with self.assertRaisesRegex(ValueError, "app1"):
            self.check()

    def test_empty_image_and_absent_app_partitions_fail(self):
        self.fixture(0)
        with self.assertRaises(ValueError):
            self.check()
        self.fixture(1, slots=())
        with self.assertRaises(ValueError):
            self.check()

    def test_truncated_or_corrupt_partition_table_fails(self):
        self.fixture(1)
        valid = self.table.read_bytes()
        for broken in (valid[:-1], b"bad" + valid[3:], valid[:-1] + bytes([valid[-1] ^ 1])):
            self.table.write_bytes(broken)
            with self.assertRaises(ValueError):
                self.check()

    def test_hardware_build_registers_gate_on_final_bin(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini")
        self.assertIn("post:scripts/check_firmware_size.py", config["base"]["extra_scripts"])
        self.assertIn("register_check", self.api)

        class BuildEnv:
            def __init__(self):
                self.actions = []
                self.dependencies = []

            def Depends(self, target, dependency):
                self.dependencies.append((target, dependency))

            def AddPostAction(self, target, action):
                self.actions.append((target, action))

        env = BuildEnv()
        # PlatformIO executes SCons extra scripts without Python's __file__ global.
        self.api["register_check"].__globals__.pop("__file__", None)
        self.api["register_check"](env)
        self.assertEqual(len(env.actions), 1)
        self.assertEqual(env.actions[0][0], "$BUILD_DIR/${PROGNAME}.bin")
        self.assertIn(("$BUILD_DIR/${PROGNAME}.bin", "$BUILD_DIR/partitions.bin"), env.dependencies)


if __name__ == "__main__":
    unittest.main(verbosity=2)

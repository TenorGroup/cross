"""Keep BLE beta profiles on the S3 USB-capable core and board libraries."""
import configparser
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).absolute().parents[2]
INI = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
INI.read(ROOT / "platformio.ini")


def option(section, key):
    if key in INI[section]:
        return re.sub(r"\$\{([^}]+)\}",
                      lambda m: option(*m[1].rsplit(".", 1)), INI[section][key])
    for parent in INI[section].get("extends", "").split(","):
        if parent.strip():
            value = option(parent.strip(), key)
            if value:
                return value
    return ""


class S3Profiles(unittest.TestCase):
    def test_beta_boards(self):
        for board, device in (("x4pro", "X4PRO"), ("x4c", "X4CLASSIC")):
            with self.subTest(board=board):
                section = "env:" + board + "-ble"
                self.assertIn(section, INI)
                self.assertEqual(option(section, "board_build.mcu"), "esp32s3")
                self.assertEqual(option(section, "board_build.arduino.memory_type"), "dio_opi")
                self.assertEqual(option(section, "custom_sdkconfig"), "")
                deps = option(section, "lib_deps")
                for dep in option("base", "lib_deps").splitlines() + option("ble_hid_beta", "lib_deps").splitlines():
                    if dep.strip():
                        self.assertIn(dep.strip(), deps)
                flags = option(section, "build_flags").split()
                for flag in option("ble_hid_beta", "build_flags").split():
                    self.assertIn(flag, flags)
                self.assertIn("-DFREEINK_DEVICE_" + device + "=1", flags)
                self.assertIn("-DFREEINK_CAP_USB_MSC=1", flags)
                # The beta image has no room for the German patterns.
                self.assertIn("-DTENOR_HYPH_NO_DE", flags)
                # v1.0.59: an X4 Pro image without the cable channel (the TENOR-CABLE-V1 board-tag field) is
                # refused by the unit's own SD_FLASH. The X4 Classic takes images over the chip's USB download
                # mode (tools/nap.py route "rom") and its envs carry no such flag.
                self.assertEqual("-DTENOR_CABLE_FLASH=1" in flags, board == "x4pro")
                # NimBLE's host allocated from PSRAM: the stack costs 46,508 B of internal RAM, not 68,152 B.
                self.assertIn("-include", flags)
                self.assertIn("scripts/s3_nimble_psram.h", flags)
                self.assertEqual(flags[flags.index("-include") + 1], "scripts/s3_nimble_psram.h")
                for plain in (board, board + "-gh_release", "x3-ble", "gh_release"):
                    self.assertNotIn("-DTENOR_HYPH_NO_DE", option("env:" + plain, "build_flags").split())
                    self.assertNotIn("scripts/s3_nimble_psram.h", option("env:" + plain, "build_flags").split())
                self.assertIn("-DBOARD_HAS_PSRAM", flags)
                self.assertNotIn("-DFREEINK_DEVICE_X3=1", flags)
                self.assertNotIn("-DTENOR_PRESS_PROBE", flags)
                self.assertNotIn("-DFREEINK_CAP_BLE_HID_HOST=1",
                                 option("env:" + board + "-gh_release", "build_flags").split())

    def test_probe(self):
        flags = option("env:x4pro-ble-probe", "build_flags").split()
        for flag in ("-DFREEINK_CAP_BLE_HID_HOST=1", "-DTENOR_PRESS_PROBE",
                     "-DBLE_PAGE_TURNER_PROBE", "-DBLE_PAGE_TURNER_STACK_PROBE"):
            self.assertIn(flag, flags)
        self.assertNotIn("-DTENOR_UI_ACCEPTANCE", flags)
        self.assertIn("scripts/s3_nimble_psram.h", flags)
        self.assertIn("-DTENOR_HYPH_NO_DE", flags)
        # Without German the probe fits with every language, so it carries the same strings as the beta.
        self.assertEqual(option("env:x4pro-ble-probe", "custom_i18n_builtin_langs"),
                         option("env:x4pro-ble", "custom_i18n_builtin_langs"))
        self.assertIn("-DARDUINO_USB_MODE=0", flags)
        # The cable is the only way to update a unit whose USB download mode is fused off: the CDC console
        # on the OTG port (PUT, SD_FLASH, OTA_STATE, USB_DRIVE) and the USB drive both have to be built in.
        for flag in ("-DARDUINO_USB_CDC_ON_BOOT=1", "-DFREEINK_DEVICE_X4PRO=1", "-DFREEINK_CAP_USB_MSC=1",
                     "-DENABLE_SERIAL_LOG"):
            self.assertIn(flag, flags)
        self.assertIn("-DARDUINO_USB_MODE=1", option("env:x4pro-ble-probe", "build_unflags"))

    def test_x4pro_probes_keep_the_cable_channel(self):
        for name in ("x4pro-ble", "x4pro-ble-probe", "x4pro-ble-probe-lite"):
            with self.subTest(env=name):
                self.assertIn("-DTENOR_CABLE_FLASH=1", option("env:" + name, "build_flags").split())

    def test_lite_probe(self):
        # Same cable channel and BLE commands as the probe, with the measurement buffers cut so the
        # radio is measured on a heap close to the beta's.
        section = "env:x4pro-ble-probe-lite"
        self.assertIn(section, INI)
        flags = option(section, "build_flags").split()
        probe = option("env:x4pro-ble-probe", "build_flags").split()
        for flag in probe + ["-DTENOR_PROBE_LITE"]:
            self.assertIn(flag, flags)
        self.assertEqual(option(section, "build_unflags"), option("env:x4pro-ble-probe", "build_unflags"))
        self.assertEqual(option(section, "lib_deps"), option("env:x4pro-ble-probe", "lib_deps"))
        self.assertEqual(option(section, "custom_i18n_builtin_langs"), option("env:x4pro-ble", "custom_i18n_builtin_langs"))
        self.assertIn("scripts/s3_nimble_psram.h", flags)
        for other in ("x4pro-ble", "x4pro-ble-probe", "x4c-ble", "x4pro-probe", "x4pro-gh_release", "x3-ble"):
            self.assertNotIn("-DTENOR_PROBE_LITE", option("env:" + other, "build_flags").split(), other)

    def test_psram_header(self):
        """The header lands after sdkconfig.h and flips NimBLE's allocator to PSRAM, tagging the build."""
        header = (ROOT / "scripts" / "s3_nimble_psram.h").read_text()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sdkconfig.h").write_text(
                "#pragma once\n#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL 1\n"
                "#define CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL\n")
            (root / "psram.h").write_text(header)
            result = subprocess.run(
                ["cc", "-E", "-dM", "-x", "c", "-I" + str(root), "-include", str(root / "psram.h"), "-"],
                input='#include "sdkconfig.h"\n', text=True, capture_output=True, check=True)
        macros = {line.split()[1]: line.split(maxsplit=2)[2] if len(line.split()) > 2 else ""
                  for line in result.stdout.splitlines() if line.startswith("#define ")}
        self.assertEqual(macros.get("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL"), "1")
        self.assertNotIn("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL", macros)
        self.assertEqual(macros.get("FREEINK_BLE_STACK_IN_PSRAM"), "1")


if __name__ == "__main__":
    unittest.main()

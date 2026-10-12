"""Keep BLE beta profiles on the S3 USB-capable core and board libraries."""
import configparser
from pathlib import Path
import re
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
                for plain in (board, board + "-gh_release", "x3-ble", "gh_release"):
                    self.assertNotIn("-DTENOR_HYPH_NO_DE", option("env:" + plain, "build_flags").split())
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
        self.assertIn("-DTENOR_HYPH_NO_DE", flags)
        # Without German the probe fits with every language, so it carries the same strings as the beta.
        self.assertEqual(option("env:x4pro-ble-probe", "custom_i18n_builtin_langs"),
                         option("env:x4pro-ble", "custom_i18n_builtin_langs"))
        self.assertIn("-DARDUINO_USB_MODE=0", flags)
        self.assertIn("-DARDUINO_USB_MODE=1", option("env:x4pro-ble-probe", "build_unflags"))


if __name__ == "__main__":
    unittest.main()

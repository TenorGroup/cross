"""Keep the S3 Bluetooth profiles on the S3 USB-capable core and board libraries."""
import configparser
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).absolute().parents[2]
INI = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
INI.read(ROOT / "platformio.ini")


def option(section, key):
    if section == "sysenv":
        return ""
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
                for dep in option("base", "lib_deps").splitlines() + option("ble_page_turner", "lib_deps").splitlines():
                    if dep.strip():
                        self.assertIn(dep.strip(), deps)
                flags = option(section, "build_flags").split()
                for flag in option("ble_page_turner", "build_flags").split():
                    self.assertIn(flag, flags)
                self.assertIn("-DFREEINK_DEVICE_" + device + "=1", flags)
                self.assertIn("-DFREEINK_CAP_USB_MSC=1", flags)
                # German patterns: every Bluetooth image carries them since v1.0.69 (Noto Serif left the image).
                self.assertNotIn("-DTENOR_HYPH_NO_DE", flags)
                # v1.0.59: an X4 Pro image without the cable channel (the TENOR-CABLE-V1 board-tag field) is
                # refused by the unit's own SD_FLASH. v1.1.0: the X4 Classic is built as the X4 Pro is (a domestic
                # unit may have its USB-Serial/JTAG fused off as the domestic X4 Pro has): TinyUSB CDC on the OTG
                # port from boot, the TCF1 channel, the boot trial.
                for name in (board, board + "-gh_release", board + "-gh_release_rc", board + "-ble"):
                    env_flags = option("env:" + name, "build_flags").split()
                    self.assertIn("-DTENOR_CABLE_FLASH=1", env_flags, name)
                    self.assertIn("-DARDUINO_USB_MODE=0", env_flags, name)
                    self.assertIn("-DARDUINO_USB_MODE=1", option("env:" + name, "build_unflags").split(), name)
                    self.assertIn("-DARDUINO_USB_CDC_ON_BOOT=1", env_flags, name)
                    # PlatformIO splits build_flags as a shell does (SCons ParseFlags, shlex): a name with spaces
                    # stays one define only inside quotes; \\" alone cut it at the first space (e5a0ac61: the core's
                    # USB.cpp then failed with "missing terminating \" character").
                    product = {"x4pro": "X4 Pro", "x4c": "X4 Classic"}[board]
                    tokens = shlex.split(option("env:" + name, "build_flags"))
                    self.assertIn('-DUSB_PRODUCT="tenor/cross ' + product + '"', tokens, name)
                    self.assertIn('-DUSB_MANUFACTURER="Tenor"', tokens, name)
                # NimBLE's host allocated from PSRAM: the stack costs 46,508 B of internal RAM, not 68,152 B.
                self.assertIn("-include", flags)
                self.assertIn("scripts/s3_nimble_psram.h", flags)
                self.assertEqual(flags[flags.index("-include") + 1], "scripts/s3_nimble_psram.h")
                for plain in (board, "x3-ble", "gh_release", board + "-gh_release"):
                    # Every release build carries German hyphenation (the X4 Pro again since v1.0.69).
                    self.assertNotIn("-DTENOR_HYPH_NO_DE", option("env:" + plain, "build_flags").split())
                # Bluetooth is in the base since CrossPoint 2.0.0, so the plain board env includes the header
                # too; the C3 envs include it as well, where it does nothing because they have no PSRAM
                # (test_psram_header).
                self.assertIn("scripts/s3_nimble_psram.h", option("env:" + board, "build_flags").split())
                for c3 in ("x3-ble", "gh_release", "gh_release_rc"):
                    c3_flags = option("env:" + c3, "build_flags").split()
                    self.assertIn("scripts/s3_nimble_psram.h", c3_flags)
                    self.assertNotIn("-DBOARD_HAS_PSRAM", c3_flags)
                self.assertIn("-DBOARD_HAS_PSRAM", flags)
                self.assertNotIn("-DFREEINK_DEVICE_X3=1", flags)
                self.assertNotIn("-DTENOR_PRESS_PROBE", flags)
                # The release and rc images carry the Bluetooth page turner, as upstream CrossPoint does: the X4 Pro
                # since v1.0.65, the X4 Classic since v1.0.69, in the 0x640000 slot of CrossPoint's partition table
                # (no slot override: the 128 KiB headroom gate checks that table).
                if board == "x4c":
                    for release in (board + "-gh_release", board + "-gh_release_rc"):
                        self.assertEqual(option("env:" + release, "custom_app_slot_bytes"), "")
                for release in (board + "-gh_release", board + "-gh_release_rc"):
                    released = option("env:" + release, "build_flags").split()
                    for flag in option("ble_page_turner", "build_flags").split():
                        self.assertIn(flag, released, release)
                    self.assertEqual(released[released.index("-include") + 1], "scripts/s3_nimble_psram.h")
                    self.assertNotIn("-DTENOR_PRESS_PROBE", released)
                    self.assertNotIn("-DTENOR_HYPH_NO_DE", released)
                    for dep in option("ble_page_turner", "lib_deps").splitlines():
                        if dep.strip():
                            self.assertIn(dep.strip(), option("env:" + release, "lib_deps"))

    def test_probe(self):
        flags = option("env:x4pro-ble-probe", "build_flags").split()
        for flag in ("-DFREEINK_CAP_BLE_HID_HOST=1", "-DTENOR_PRESS_PROBE",
                     "-DBLE_PAGE_TURNER_PROBE", "-DBLE_PAGE_TURNER_STACK_PROBE"):
            self.assertIn(flag, flags)
        self.assertNotIn("-DTENOR_UI_ACCEPTANCE", flags)
        self.assertIn("scripts/s3_nimble_psram.h", flags)
        self.assertNotIn("-DTENOR_HYPH_NO_DE", flags)
        # The probe carries the same languages and strings as the release.
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
        """The header lands after sdkconfig.h and, on a board with PSRAM only, flips NimBLE's allocator to
        PSRAM and tags the build. Every Bluetooth env includes it (the [ble_page_turner] section), the X3 too."""
        header = (ROOT / "scripts" / "s3_nimble_psram.h").read_text()

        def macros(*defines):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "sdkconfig.h").write_text(
                    "#pragma once\n#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL 1\n"
                    "#define CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL\n")
                (root / "psram.h").write_text(header)
                result = subprocess.run(
                    ["cc", "-E", "-dM", "-x", "c", *defines, "-I" + str(root), "-include", str(root / "psram.h"),
                     "-"], input='#include "sdkconfig.h"\n', text=True, capture_output=True, check=True)
            return {line.split()[1]: line.split(maxsplit=2)[2] if len(line.split()) > 2 else ""
                    for line in result.stdout.splitlines() if line.startswith("#define ")}

        s3 = macros("-DBOARD_HAS_PSRAM")
        self.assertEqual(s3.get("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL"), "1")
        self.assertNotIn("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL", s3)
        self.assertEqual(s3.get("FREEINK_BLE_STACK_IN_PSRAM"), "1")
        c3 = macros()
        self.assertEqual(c3.get("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL"), "1")
        self.assertNotIn("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL", c3)
        self.assertNotIn("FREEINK_BLE_STACK_IN_PSRAM", c3)

    def test_c3_sdkconfig_text_is_pinned(self):
        """The C3 envs' custom_sdkconfig text keys the framework libraries every tree on a machine shares
        (scripts/patch_pioarduino_cache.py). A change rebuilds them, so it is made on purpose: build the local
        x3-libs env first, then update c3_sdkconfig.txt. The X3 release, rc and x3-ble ask for the same text."""
        pinned = (ROOT / "test" / "ble_integration" / "c3_sdkconfig.txt").read_text().strip()
        for env in ("gh_release", "gh_release_rc", "x3-ble"):
            self.assertEqual(option("env:" + env, "custom_sdkconfig").strip(), pinned, env)


if __name__ == "__main__":
    unittest.main()

    unittest.main()

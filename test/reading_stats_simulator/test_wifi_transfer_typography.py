"""Wi-Fi transfer typography matrix on the X3 simulator framebuffer."""

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
ARTIFACTS = Path(os.environ.get("CROSSPOINT_TEST_ARTIFACTS", REPO.parent / "wifi-transfer-typography"))
SOURCE = REPO / "src/activities/network/CrossPointWebServerActivity.cpp"
LOCALES = ("VI", "EN", "ZH_HANS")
FOOTER_TOP = (752, 712, 702)
DEVICE_NAME = "W" * 31
SSID = "W" * 31
FAKE_NETWORKS = f"{SSID}:-40:0"
# Home Settings: 5 reading groups, then File transfer at the start of the device group.
TO_CHOOSER = "1500:UP;1800:RIGHT;2100:RIGHT;2400:RIGHT;2700:RIGHT;2900:RIGHT;3100:CONFIRM"


def black_bbox(image, bounds):
    crop = image.crop(bounds).convert("L")
    return crop.point(lambda pixel: 255 if pixel < 128 else 0).getbbox()


class WifiTransferTypographyTest(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.expected_sha = os.environ.get("TEST_PROGRAM_SHA256")
        ARTIFACTS.mkdir(parents=True, exist_ok=True)

    def fixture(self, locale, tier, mode):
        temporary = tempfile.TemporaryDirectory(prefix=f"cross-wifi-{mode.lower()}-")
        self.addCleanup(temporary.cleanup)
        sd = Path(temporary.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (store / "settings.json").write_text(json.dumps({
            "language": locale,
            "uiTheme": 4,
            "uiTextSize": tier,
            "deviceName": DEVICE_NAME,
            "sleepTimeout": 120,
            "globalStatusBarMode": 0,
        }))
        (store / "state.json").write_text(json.dumps({
            "openEpubPath": "",
            "lastSleepFromReader": False,
            "showBootScreen": False,
            "readerActivityLoadCount": 0,
        }))
        if mode == "STA":
            (store / "wifi.json").write_text(json.dumps({
                "lastConnectedSsid": SSID,
                "credentials": [{"ssid": SSID}],
            }))
        return sd

    def run_case(self, locale, tier, mode):
        sd = self.fixture(locale, tier, mode)
        stem = f"{mode.lower()}-{locale.lower()}-tier{tier}"
        bmp = ARTIFACTS / f"{stem}.bmp"
        png = ARTIFACTS / f"{stem}.png"
        log_path = ARTIFACTS / f"{stem}.log"
        input_path = ARTIFACTS / f"{stem}-input.txt"
        if mode == "AP":
            tail = "4200:RIGHT;4800:RIGHT;5600:CONFIRM;9000:QUIT"
            shot_time = 8200
        else:
            tail = "4400:CONFIRM;11500:QUIT"
            shot_time = 10200
        script = f"{TO_CHOOSER};{tail}"
        env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(sd),
            CROSSPOINT_SIM_WIFI_NETWORKS=FAKE_NETWORKS,
            CROSSPOINT_SIM_INPUT_SCRIPT=script,
            CROSSPOINT_SIM_SCREENSHOTS=f"{shot_time}:{bmp}",
        )
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=45)
        log = run.stdout + run.stderr
        log_path.write_text(log)
        input_path.write_text(script + "\n")
        self.assertEqual(run.returncode, 0, log[-5000:])
        chooser = "Entering activity: NetworkModeSelection"
        self.assertIn(chooser, log, log[-5000:])
        self.assertIn(f"Network mode: {mode}", log, log[-5000:])
        self.assertLess(log.index(chooser), log.index(f"Network mode: {mode}"), log[-5000:])
        self.assertTrue(bmp.exists(), f"Missing framebuffer capture: {bmp}")
        with Image.open(bmp) as raw:
            image = raw.convert("RGB")
        if image.width > image.height:
            image = image.rotate(90, expand=True)
        self.assertEqual(image.size, (528, 792))
        self.assertIsNotNone(ImageChops.difference(image, Image.new("RGB", image.size, "white")).getbbox())
        image.save(png)
        body = black_bbox(image, (0, 110, 528, FOOTER_TOP[tier]))
        self.assertIsNotNone(body, f"{stem}: transfer body is blank")
        self.assertLess(body[3] + 110, FOOTER_TOP[tier], f"{stem}: body touches footer")
        if self.expected_sha:
            self.assertEqual(hashlib.sha256(PROGRAM.read_bytes()).hexdigest(), self.expected_sha)

    def test_source_contract_ap_shows_ip_and_keeps_hostname_qr(self):
        source = SOURCE.read_text()
        render = source[source.index("void CrossPointWebServerActivity::renderServerRunning() const {"):]
        ap_start = render.index("if (isApMode) {")
        ap_end = render.index("} else {", ap_start)
        ap = render[ap_start:ap_end]
        self.assertIn("QrUtils::drawQrCode(renderer, qrBoundsUrl, hostnameUrl);", ap)
        self.assertIn("drawNetworkText(renderer, UI_12_FONT_ID, apDisplayIp.c_str()", ap)
        self.assertNotRegex(ap, r"drawNetworkText\([^\n]*hostnameUrl\.c_str\(\)")
        self.assertNotRegex(ap, r"drawNetworkText\([^\n]*ipUrl\.c_str\(\)")
        self.assertIn('const std::string apDisplayIp = "http://" + connectedIP;', render)
        sta = render[ap_end:]
        self.assertIn("QrUtils::drawQrCode(renderer, qrBounds, ipUrl);", sta)
        self.assertIn("drawNetworkText(renderer, UI_12_FONT_ID, ipUrl.c_str()", sta)
        self.assertIn("drawNetworkText(renderer, UI_12_FONT_ID, hostnameUrl.c_str()", sta)

    def test_ap_and_sta_locale_tier_matrix(self):
        for mode in ("AP", "STA"):
            for locale in LOCALES:
                for tier in range(3):
                    with self.subTest(mode=mode, locale=locale, tier=tier):
                        self.run_case(locale, tier, mode)


if __name__ == "__main__":
    unittest.main()

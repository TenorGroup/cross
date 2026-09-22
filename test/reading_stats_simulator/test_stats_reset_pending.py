"""Exercise committed reset recovery through the real Home confirmation flow."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
RESET_SCRIPT = (
    "1000:DOWN;1500:DOWN;2000:DOWN;"
    "2600:RIGHT;2900:RIGHT;3200:RIGHT;3500:RIGHT;"
    "3900:CONFIRM;4700:RIGHT;5200:CONFIRM;7500:QUIT"
)
EXPECTED_TIPS = {
    "VI": "Đã lưu yêu cầu xóa. Hãy kiểm tra thẻ SD.",
    "EN": "Reset saved. Please check the SD card.",
    "ZH_HANS": "重置已保存。请检查存储卡。",
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class StatsResetPendingTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.assertTrue(PROGRAM.is_file(), f"missing simulator: {PROGRAM}")
        output = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
        self.artifacts = Path(output) if output else None
        if self.artifacts:
            self.artifacts.mkdir(parents=True, exist_ok=True)

    def run_simulator(self, sd, script, screenshots=(), timeout=25):
        env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(sd),
            CROSSPOINT_SIM_INPUT_SCRIPT=script,
        )
        if screenshots:
            env["CROSSPOINT_SIM_SCREENSHOTS"] = ";".join(
                f"{when}:{sd / (name + '.bmp')}" for when, name in screenshots
            )
        return subprocess.run(
            [str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout
        )

    def fixture(self, root, language):
        store = root / ".crosspoint"
        books = store / "reading-stats"
        books.mkdir(parents=True)
        (store / "settings.json").write_text(
            json.dumps({"language": language, "uiTheme": 4, "sleepTimeout": 10}), encoding="utf-8"
        )
        state = b'{"openEpubPath":"/book.txt","readerActivityLoadCount":3,"showBootScreen":false}'
        (store / "state.json").write_bytes(state)
        main = (
            b'{"schema":3,"bookEpoch":0,"ngay":[[20260920,12,30]],'
            b'"lacPhut":3,"lacTrang":4,"habits":{"awarded":33},'
            b'"activeBook":{"bookEpoch":0,"path":"/book.txt","title":"book",'
            b'"minutes":12,"ms":0,"turns":30,"first":20260920,"last":20260920,'
            b'"days":1,"progress":42,"startProgress":5}}'
        )
        (store / "reading-stats.json").write_bytes(main)
        book_path = books / "tenor_0123456789abcdef.json"
        book = (
            b'{"bookEpoch":0,"path":"/archived.txt","title":"archived","minutes":5,'
            b'"ms":0,"turns":11,"first":20260919,"last":20260919,"days":1,'
            b'"progress":10,"startProgress":0}'
        )
        book_path.write_bytes(book)
        blocker = store / "reading-stats.json.bak"
        blocker.mkdir()
        (blocker / "keep").write_bytes(b"force backup removal failure\n")
        return store, book_path, main, book, state, blocker

    def assert_tip_source(self, language, expected):
        source = {
            "VI": REPO / "lib/I18n/translations/vietnamese.yaml",
            "EN": REPO / "lib/I18n/translations/english.yaml",
            "ZH_HANS": REPO / "lib/I18n/translations/chinese.yaml",
        }[language]
        self.assertIn(f'STR_STATS_RESET_PENDING: "{expected}"', source.read_text(encoding="utf-8"))

    def test_pending_preserves_bytes_then_reload_finalizes(self):
        footer_hashes = {}
        for language, expected_tip in EXPECTED_TIPS.items():
            with self.subTest(language=language), tempfile.TemporaryDirectory(
                prefix=f"cross-reset-pending-{language.lower()}-"
            ) as temporary:
                sd = Path(temporary)
                store, book_path, main_before, book_before, state_before, blocker = self.fixture(sd, language)
                before_name = f"{language.lower()}-before"
                pending_name = f"{language.lower()}-pending"
                run = self.run_simulator(
                    sd, RESET_SCRIPT, screenshots=((3700, before_name), (6500, pending_name))
                )
                log = run.stdout + run.stderr
                self.assertEqual(run.returncode, 0, log[-4000:])
                self.assertEqual(log.count("Entering activity: Confirmation"), 1, log[-4000:])
                self.assertIn("Reset committed; journal finalization pending", log)

                journal = store / "reading-stats.reset"
                self.assertTrue(journal.is_file(), "committed reset journal must remain pending")
                pending = json.loads(journal.read_text(encoding="utf-8"))
                self.assertEqual(pending.get("schema"), 4)
                self.assertEqual(pending.get("bookEpoch"), 1)
                self.assertNotIn("ngay", pending)
                self.assertEqual((store / "reading-stats.json").read_bytes(), main_before)
                self.assertEqual(book_path.read_bytes(), book_before)
                self.assertEqual((store / "state.json").read_bytes(), state_before)

                self.assert_tip_source(language, expected_tip)
                before = Image.open(sd / (before_name + ".bmp")).convert("RGB")
                after = Image.open(sd / (pending_name + ".bmp")).convert("RGB")
                scale = max(1, round(after.width / 528))
                footer = (0, after.height - 150 * scale, after.width, after.height)
                before_footer = before.crop(footer)
                pending_footer = after.crop(footer)
                self.assertIsNotNone(
                    ImageChops.difference(before_footer, pending_footer).getbbox(),
                    f"{language}: pending footer tip was not rendered",
                )
                footer_hashes[language] = hashlib.sha256(pending_footer.tobytes()).hexdigest()

                if self.artifacts:
                    (self.artifacts / f"{language.lower()}-pending.log").write_text(log, encoding="utf-8")
                    for name in (before_name, pending_name):
                        with Image.open(sd / (name + ".bmp")) as image:
                            image.save(self.artifacts / (name + ".png"))

                (blocker / "keep").unlink()
                blocker.rmdir()
                replay = self.run_simulator(sd, "2200:QUIT")
                replay_log = replay.stdout + replay.stderr
                self.assertEqual(replay.returncode, 0, replay_log[-4000:])
                self.assertFalse(journal.exists(), "reload must finalize the committed journal")
                final = json.loads((store / "reading-stats.json").read_text(encoding="utf-8"))
                self.assertEqual(final, {"schema": 4, "bookEpoch": 1})
                self.assertFalse(book_path.exists(), "all-reset cleanup runs after finalization")
                self.assertEqual((store / "state.json").read_bytes(), state_before)
                self.assertFalse((store / "reading-stats.json.bak").exists())

                if self.artifacts:
                    (self.artifacts / f"{language.lower()}-replay.log").write_text(replay_log, encoding="utf-8")
                    (self.artifacts / f"{language.lower()}-journal.sha256").write_text(
                        sha256(store / "reading-stats.json") + "  reading-stats.json\n", encoding="ascii"
                    )

        self.assertEqual(len(set(footer_hashes.values())), len(EXPECTED_TIPS), footer_hashes)


if __name__ == "__main__":
    unittest.main()

"""Keep every valid recent book reachable through the Home tab's button ring."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))


class RecentBooksSimulatorTest(unittest.TestCase):
    def test_utf8_recent_title_survives_reader_save_and_restart(self):
        from test_reader_page_fill import write_epub

        with tempfile.TemporaryDirectory(prefix="cross-recent-utf8-") as directory:
            sd = Path(directory)
            store = sd / ".crosspoint"
            store.mkdir()
            write_epub(sd / "book.epub")
            (store / "recent.json").write_text(json.dumps({
                "books": [{"path": "/book.epub", "title": "Initial title"}]
            }))
            (store / "settings.json").write_text(json.dumps({"language": "VI"}))
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith("CROSSPOINT_SIM_")}
            env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                       CROSSPOINT_SIM_INPUT_SCRIPT="1000:CONFIRM;4000:BACK;7000:QUIT")
            expected_title = "Ậ Ễ Ố đo mực thanh trạng thái"
            for boot in range(2):
                # Both boots must find the book through Home's persisted list.
                (store / "state.json").unlink(missing_ok=True)
                run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env,
                                     capture_output=True, text=True, timeout=20)
                log = run.stdout + run.stderr
                self.assertEqual(run.returncode, 0, log)
                self.assertIn("Recent books loaded from file (1 entries)", log,
                              f"boot {boot}: {log}")
                self.assertIn("Entering activity: EpubReader", log, f"boot {boot}: {log}")
                saved = (store / "recent.json").read_text()
                self.assertIn(expected_title, saved)
                book = json.loads(saved)["books"][0]
                self.assertEqual(book["title"], expected_title)
                self.assertEqual(book["path"], "/book.epub")

    def test_last_recent_is_one_backward_step_from_first_row(self):
        with tempfile.TemporaryDirectory(prefix="cross-recent-books-") as directory:
            sd = Path(directory)
            store = sd / ".crosspoint"
            store.mkdir()
            books = []
            for index in range(1, 11):
                path = f"/book{index}.txt"
                (sd / path[1:]).write_text("Recent books fixture.\n" * 100)
                books.append({"path": path, "title": f"Audit book {index}"})
            # A missing file must not become an extra row in the ring.
            books.insert(0, {"path": "/missing.txt", "title": "Missing"})
            (store / "recent.json").write_text(json.dumps({"books": books[:1] + books[2:]}))
            (store / "settings.json").write_text(json.dumps({"language": "EN"}))
            env = os.environ.copy()
            for key in list(env):
                if key.startswith("CROSSPOINT_SIM_"):
                    del env[key]
            # Ky vong 17/09 theo quyet dinh da chot: man chinh chi hien NAM sach gan nhat
            # (truoc day bai nay cho ca muoi cuon). Tep thieu khong chiem mot hang.
            # Front previous wraps directly from the first row to the last row.
            # Fixture: mot tep thieu roi book2..book10 -> nam hang hien ra la book2..book6,
            # nen hang cuoi la book6.
            env.update(
                SDL_VIDEODRIVER="dummy",
                CROSSPOINT_SIM_SD=str(sd),
                CROSSPOINT_SIM_INPUT_SCRIPT="1200:LEFT;1800:CONFIRM;4500:BACK;6000:QUIT",
            )
            run = subprocess.run(
                [str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=20
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("Entering activity: TxtReader", run.stdout + run.stderr)
            state = json.loads((store / "state.json").read_text())
            self.assertEqual(state["openEpubPath"], "/book6.txt")


if __name__ == "__main__":
    unittest.main()

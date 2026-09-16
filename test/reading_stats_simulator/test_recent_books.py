"""Keep every valid recent book reachable through the Home tab's button ring."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / ".pio/build/simulator_x3_uc8279/program"


class RecentBooksSimulatorTest(unittest.TestCase):
    def test_last_recent_is_one_backward_step_from_tab(self):
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
            env.update(
                SDL_VIDEODRIVER="dummy",
                CROSSPOINT_SIM_SD=str(sd),
                CROSSPOINT_SIM_INPUT_SCRIPT="1200:LEFT;2200:CONFIRM;4500:BACK;6000:QUIT",
            )
            run = subprocess.run(
                [str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=20
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("Entering activity: TxtReader", run.stdout + run.stderr)
            state = json.loads((store / "state.json").read_text())
            self.assertEqual(state["openEpubPath"], "/book10.txt")


if __name__ == "__main__":
    unittest.main()

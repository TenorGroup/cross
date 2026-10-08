"""Recent's next-book title uses its second line before shortening it."""

import json
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("HOME_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
ARTIFACTS = Path(os.environ.get("HOME_ARTIFACTS", tempfile.gettempdir()))


def capture(tail, tier=0, title="Chuyện kể dưới tán cây trong mùa "):
    with tempfile.TemporaryDirectory(prefix="home-title-v1054-") as tmp:
        folder = Path(tmp)
        sd = folder / "sd"
        store = sd / ".crosspoint"
        store.mkdir(parents=True)
        for name in ("first", "second"):
            (sd / f"{name}.txt").write_text("A reading fixture.\n" * 10)
        Image.new("1", (298, 450), 0).save(sd / "cover.bmp")
        (store / "recent.json").write_text(json.dumps({"books": [
            {"path": "/first.txt", "title": "A quiet book", "author": "Tenor", "coverBmpPath": "/cover.bmp"},
            {"path": "/second.txt", "title": title + tail}
        ]}))
        (store / "state.json").write_text(json.dumps({"openEpubPath": "", "showBootScreen": False}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "uiTextSize": tier}))
        shot = folder / "home.bmp"
        env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT="3000:QUIT", CROSSPOINT_SIM_SCREENSHOTS=f"2500:{shot}")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, (run.stdout + run.stderr)[-1500:]
        with Image.open(shot) as raw:
            image = raw.convert("L")
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        image.save(ARTIFACTS / f"home-next-title-{tail}-tier-{tier}.png")
        (ARTIFACTS / f"home-next-title-{tail}-tier-{tier}.log").write_text(run.stdout + run.stderr)
        return image


def main():
    for tier in range(3):
        title = "Chuyện kể dưới tán cây " if tier == 2 else "Chuyện kể dưới tán cây trong mùa "
        rain, sun = capture("mưa", tier, title), capture("nắng", tier, title)
        title_band = (40, 600, rain.width - 40, rain.height - 60)
        changed = ImageChops.difference(rain.crop(title_band), sun.crop(title_band))
        pixels = sum(value != 0 for value in changed.getdata())
        print(f"tier {tier} next-title suffix changed pixels: {pixels}")
        assert pixels > 0, "next-book title loses its second-line suffix"


if __name__ == "__main__":
    main()

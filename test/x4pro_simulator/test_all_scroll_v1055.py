"""Capture X4 Pro scroll regions, fitting lists and reader-page isolation."""

import argparse
import json
import re
import shutil
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image, ImageChops

from test_shared_scroll_v1055 import check_geometry, difference_outside, thumb
from test_thanh_day import run
from test_thanh_dong import toc_book
from test_menu_chu_14 import OPEN_BOOK as LEGACY_OPEN_BOOK
from test_dong_deu import root_files

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from generate_test_epubs import write_epub

OPEN_BOOK = LEGACY_OPEN_BOOK.replace("7000:TAP:240,68", "7000:TAP:240,130")
TEXT_MENU = OPEN_BOOK + ";9500:TAP:240,775"


def font_books(font):
    def prepare(folder):
        fonts = folder.parent / "fonts"
        fonts.mkdir()
        for index in range(14):
            family = fonts / f"ScrollFont{index:02d}"
            family.mkdir()
            shutil.copyfile(font, family / "ScrollFixture_18.cpfont")
    return prepare


def favorite_books(folder):
    store = folder.parent / ".crosspoint"
    records = store / "favorite-files"
    records.mkdir()
    pins = []
    for book in sorted(folder.glob("extra_*.epub")):
        path = "/sach/" + book.name
        value = 14695981039346656037
        for byte in path.encode():
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        key = f"bookid/{value:016x}"
        pins.append(key)
        (records / f"{key[7:]}.txt").write_text(path)
    (store / "menu-customization.json").write_text(json.dumps({"version": 1, "tabs": {}, "pins": pins}))


def recent_books(folder):
    books = [{"path": "/sach/" + book.name, "title": book.stem}
             for book in sorted(folder.glob("*.epub"))]
    (folder.parent / ".crosspoint/recent.json").write_text(json.dumps({"books": books}))


def opds_books(port):
    def prepare(folder):
        store = folder.parent / ".crosspoint"
        (store / "opds.json").write_text(json.dumps({"servers": [
            {"name": "Scroll fixture", "url": f"http://127.0.0.1:{port}/", "username": "", "password": ""}]}))
        (store / "wifi.json").write_text(json.dumps({"lastConnectedSsid": "ScrollSSID", "credentials": [
            {"ssid": "ScrollSSID", "password": ""}]}))
    return prepare


def unique_reader_book(folder, prepare=None):
    if prepare:
        prepare(folder)
    write_epub(folder / "test_dictionary_synonyms.epub", "Scroll55b unique pages",
               [f"Verification row {index:04d}. Each page keeps a distinct visible row number."
                for index in range(180)])


def cases(font, port):
    settings = "3000:TAP:455,754"
    reader_settings = settings + ";5000:TAP:240,241"
    return {
        "settings-single-fit": (reader_settings, 5000, False, 0, None, 32, 716),
        "settings-font-long": (reader_settings + ";7000:TAP:240,68", 7000, True, 0, font_books(font), 32, 716),
        "settings-font-fit": (reader_settings + ";7000:TAP:240,68", 7000, False, 0, None, 32, 716),
        "bluetooth-fit": (settings + ";5000:TAP:240,480;7000:TAP:240,254", 7000, False, 0, None, 32, 716),
        "file-folder-long": ("3000:TAP:148,754;5000:TAP:240,68", 5000, True, 24, None, 32, 716),
        "file-folder-fit": ("3000:TAP:148,754;5000:TAP:240,68", 5000, False, 0, None, 32, 716),
        "file-root-long": ("3000:TAP:148,754", 3000, True, 0, root_files, 32, 716),
        "file-root-fit": ("3000:TAP:148,754", 3000, False, 0, None, 32, 716),
        "favorites-long": ("3000:TAP:240,754", 3000, True, 24, favorite_books, 32, 716),
        "recent-fit": ("3000:TAP:56,754", 3000, False, 24, None, 32, 716),
        "recent-card-10": ("3000:TAP:56,754", 3000, False, 24, recent_books, 32, 716),
        "wifi-long": (settings + ";5000:TAP:240,480;7000:TAP:240,192", 7000, True, 0, None, 88, 684),
        "wifi-fit": (settings + ";5000:TAP:240,480;7000:TAP:240,192", 7000, False, 0, None, 88, 684),
        "opds-long": (settings + ";4200:SWIPE:240,600,240,520,650;5700:SWIPE:240,600,240,520,650;"
                      "7200:SWIPE:240,650,240,250,150;9500:TAP:240,620;11500:TAP:240,203",
                      12500, True, 0, opds_books(port), 32, 716),
        "opds-fit": (settings + ";4200:SWIPE:240,600,240,520,650;5700:SWIPE:240,600,240,520,650;"
                     "7200:SWIPE:240,650,240,250,150;9500:TAP:240,620;11500:TAP:240,203",
                     12500, False, 0, opds_books(port), 32, 716),
        "reader-text": (TEXT_MENU, 9500, True, 0, None, 402, 712),
        "reader-more": (TEXT_MENU + ";12000:TAP:416,754", 12000, True, 0, None, 402, 712),
        "reader-toc": (OPEN_BOOK + ";9500:TAP:240,775;12000:TAP:226,754", 12000, True, 0, toc_book, 402, 712),
        "reader-font-long": (TEXT_MENU.replace("5000:TAP:240,68", "5000:TAP:240,130") + ";12000:TAP:240,433",
                             12000, True, 0, font_books(font), 402, 688),
        "reader-font-fit": (TEXT_MENU + ";12000:TAP:240,433", 12000, False, 0, None, 402, 688),
        "reader-values-long": (TEXT_MENU + ";12000:TAP:416,754;14000:TAP:240,569", 14000, True, 0, None, 362, 712),
        "reader-values-fit": (TEXT_MENU + ";12000:TAP:240,619", 12000, False, 0, None, 362, 712),
    }


def capture(output, name, spec, shell, reuse=False):
    script, entry, scrollable, extra, books, top, bottom = spec
    folder = output / name
    if name.startswith("reader-") and books is not toc_book:
        original_prepare = books
        books = lambda folder: unique_reader_book(folder, original_prepare)
    script += ";" + ";".join(f"{entry + delay}:SWIPE:240,650,240,250,150"
                              for delay in (3200, 4100, 5000, 5900, 6800, 7700))
    shots = [entry + delay for delay in (900, 2600, 3900, 8400, 10100)]
    sim_env = {}
    if name.startswith("wifi-"):
        count = 30 if scrollable else 1
        sim_env["CROSSPOINT_SIM_WIFI_NETWORKS"] = ";".join(f"ScrollSSID{index:02d}:-45:open" for index in range(count))
    if name.startswith("opds-"):
        sim_env["CROSSPOINT_SIM_WIFI_NETWORKS"] = "ScrollSSID:-45:open"
    labels = ("shown", "hidden", "reshown", "end", "hidden-again")
    if reuse:
        images = [Image.open(folder / f"{label}.png").convert("L") for label in labels]
    else:
        images = run(folder, script, shots, extra_books=extra, write_books=books,
                     settings={"uiShell": shell}, sim_env=sim_env)
    for label, image in zip(labels, images):
        image.save(folder / f"{label}.png")
    montage = Image.new("L", (480 * len(images), 800), 255)
    for index, image in enumerate(images):
        montage.paste(image, (index * 480, 0))
    montage.save(folder / "ghep.png")
    log = (folder / "simulator.log").read_text()
    windows = [tuple(window[1:]) for window in re.findall(r"\[(\d+)\].*Window logical=(\d+),(\d+) 6x(\d+)", log)
               if int(window[0]) >= entry - 1000]
    if windows:
        top = int(windows[0][1])
        bottom = top + int(windows[0][2])
    failures = []
    if name.startswith("reader-") and "Entering activity: EpubReader" not in log:
        failures.append("fixture did not enter the EPUB reader")
    for index in (0, 2, 3):
        if scrollable:
            try:
                bounds = windows[-1] if index == 3 and windows else None
                end_top = int(bounds[1]) if bounds else top
                end_bottom = end_top + int(bounds[2]) if bounds else bottom
                check_geometry(images[index], end_top if index == 3 else top,
                               end_bottom if index == 3 else bottom, at_end=index == 3,
                               background=images[4 if index == 3 else 1] if name.startswith("reader-") else None)
            except AssertionError as error:
                failures.append(f"{labels[index]}: {error}")
        elif thumb(images[index], top, bottom):
            failures.append(f"{labels[index]}: fitting list has a scrollbar")
    for index in (1, 4):
        if thumb(images[index], top, bottom):
            failures.append(f"{labels[index]}: scrollbar did not hide")
    if scrollable and not windows:
        failures.append("no narrow idle refresh")
    if scrollable and not difference_outside(images[0], images[3], (469, 0, 475, 724)):
        failures.append("swipes did not move list content")
    for before, after in ((0, 1), (3, 4)):
        if difference_outside(images[before], images[after], (469, 0, 475, 724)):
            failures.append(f"{labels[after]}: pixels outside the scrollbar changed")
        if scrollable and ImageChops.difference(images[before], images[after]).getbbox() is None:
            failures.append(f"{labels[after]}: idle did not erase the scrollbar")
    if name.startswith("reader-") and len(re.findall(r"RDR_TRACE .*stage=applied", log)):
        failures.append("scrollbar journey applied a page turn")
    if name.startswith("reader-"):
        book = images[0].crop((0, 32, 469, 350))
        if any(ImageChops.difference(book, image.crop((0, 32, 469, 350))).getbbox() for image in images[1:]):
            failures.append("scrolling or idle changed the book above the sheet")
    result = {"case": name, "shell": shell, "scrollable": scrollable, "script": script,
              "windows": windows, "failures": failures}
    (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("RED" if failures else "GREEN", name, failures, flush=True)
    return result


def touch_during_reader_hide(output, reuse=False):
    folder = output / "reader-touch-during-hide"
    if reuse:
        image = Image.open(folder / "after-touch.png").convert("L")
    else:
        (image,) = run(folder, TEXT_MENU + ";11900:TAP:240,433", [12900], write_books=unique_reader_book,
                       sim_env={"CROSSPOINT_SIM_WINDOW_MS": "700", "CROSSPOINT_SIM_STRICT_SAMPLING": "1"})
    image.save(folder / "after-touch.png")
    log = (folder / "simulator.log").read_text()
    begin = re.search(r"WINDOW begin ms=(\d+)", log)
    end = re.search(r"WINDOW end ms=(\d+)", log)
    failures = []
    if not begin or not end or not (int(begin[1]) < 11900 < int(end[1])):
        failures.append("reader tap did not land during the narrow refresh")
    if "lost press" in log:
        failures.append("reader hide dropped a touch")
    reference = Image.open(output / "reader-font-fit/shown.png").convert("L")
    box = (32, 402, 448, 688)
    if ImageChops.difference(image.crop(box), reference.crop(box)).getbbox():
        failures.append("tap during reader hide did not open the Font sheet")
    result = {"case": "reader-touch-during-hide", "failures": failures}
    (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print("RED" if failures else "GREEN", result, flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--font-fixture", type=Path, required=True)
    parser.add_argument("--case")
    parser.add_argument("--shell", type=int, default=0)
    parser.add_argument("--reuse", action="store_true", help="Validate existing screenshots and simulator logs")
    args = parser.parse_args()
    assert args.font_fixture.is_file(), args.font_fixture
    class Feed(BaseHTTPRequestHandler):
        def do_GET(self):
            count = 30 if active_case[0] == "opds-long" else 1
            entries = "".join(f'<entry><id>urn:scroll:{index}</id><title>Scroll book {index:02d}</title>'
                              f'<link rel="http://opds-spec.org/acquisition" type="application/epub+zip" '
                              f'href="/book{index}.epub"/></entry>' for index in range(count))
            body = ('<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom">'
                    '<id>urn:scroll:feed</id><title>Scroll fixture</title>' + entries + '</feed>').encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/atom+xml")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    active_case = [""]
    server = ThreadingHTTPServer(("127.0.0.1", 0), Feed)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        selected = cases(args.font_fixture, server.server_port)
        if args.case:
            selected = {args.case: selected[args.case]}
        args.output.mkdir(parents=True, exist_ok=True)
        results = []
        for name, spec in selected.items():
            active_case[0] = name
            results.append(capture(args.output, name, spec, args.shell, args.reuse))
        if not args.case and args.shell == 0:
            results.append(touch_during_reader_hide(args.output, args.reuse))
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    assert not any(result["failures"] for result in results), results


if __name__ == "__main__":
    main()

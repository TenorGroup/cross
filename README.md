# tenor/cross

tenor/cross is e-reader firmware for the Xteink X3 and X4. It is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) by Dave Allie and contributors, and it runs on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk). The CrossPoint history is kept in this repository, so every upstream commit keeps its original author.

The latest release is v1.0.17. The X4 Pro has no firmware in any release yet.

## What it adds to CrossPoint

Everything below has shipped in a tagged release. The version in brackets is the first release that had it.

### Moving around without buttons

- Tilt page turning comes from CrossPoint and works on the X3, which has the motion sensor. tenor/cross adds two more gestures on top of it: a sideways flick switches tabs (v1.0.8) and an up or down flick moves the selection one row in lists and menus, like the up and down buttons (v1.0.9). Each has Off, Normal and Reversed. Picking the device up no longer triggers a stray tab switch or line jump (v1.0.16).
- Side flick strength and Up/down flick strength can each be Light, Medium or Strong (v1.0.12).
- Hard shake (X3) can Refresh the screen, Sleep, Turn the page, go Back or Select, and works on every screen. Off by default, with a Shake strength of Light, Medium or Strong (v1.0.16).
- A Motion sensor tab in Settings holds every tilt and shake row (v1.0.17).
- Face down and face up (X3) can each run an action you choose: Refresh, Sleep, Turn the page, Back or Select. Off by default (v1.0.17).
- Double tap on the back (X3) runs an action you choose, Off by default (v1.0.17).

### Bluetooth page turners

- Remote page turns go through the reader's normal input handling for EPUB, TXT and XTC, so they update the page, the reading statistics and the sleep timer the same way a button press does (v1.0.7).
- Four binding rows: next page, previous page, next chapter and previous chapter. Select a row and press the remote button you want for it. A button that sends a different code when held can have a separate hold action (v1.0.14).
- Reading with Bluetooth on no longer re-indexes the chapter on every page turn, and a low-memory chapter build no longer restarts the reader (v1.0.9). With Bluetooth on, the reader lays out the next two pages ahead (v1.0.12).
- The Free3 remote's page buttons were tested on an X3 (v1.0.8). Its third button goes to the next chapter on a tap and back one chapter on a hold (v1.0.14); that button was checked with recorded remote reports.
- Bluetooth switches itself off after five minutes with no connected device (v1.0.7).
- Bluetooth page turner support is no longer labelled beta (v1.0.16).

### Buttons and text entry

- Buttons are sampled every 10 ms, and presses made while a page is drawing are queued, so quick presses each turn a page (v1.0.12). Two or three quick page-turn presses now become one refresh straight to the target page (v1.0.16).
- Holding a side button turns one page and then jumps by chapter while held (v1.0.3).
- Holding Confirm while reading runs a quick action you choose, such as the reader menu, File Transfer or switching tilt page turning (v1.0.12).
- The power button's quick press can also go Back or Select, from the same action list as hard shake (v1.0.16).
- Back now closes a stalled File Transfer upload in about 0.2 seconds, down from as long as 5, and cancels an in-progress firmware update download (v1.0.16).
- The on-screen keyboard is driven by the front buttons, with a choice of which pair moves across and which moves between rows, straight or staggered key rows, and a hold for a quick space (v1.0.0).

### Reading

- Font pack with Literata, Atkinson Hyperlegible Next, Be Vietnam Pro, Geist and Livvic in sizes 12 to 26, with several weights (v1.0.0 and v1.0.2). Reading text has four ink levels, from 0 to +3 (v1.0.8).
- Line spacing, paragraph spacing, letter spacing and paragraph indent each have five levels (v1.0.3).
- A large initial letter at the start of each chapter, with Vietnamese diacritics (v1.0.0).
- The reader status bar can show the chapter name, the chapter page count and the book percentage, each switched on or off by itself (v1.0.13).
- Select text and save it as a quotation. Saved quotations stay highlighted in the book and can be browsed by book, edited or deleted (v1.0.9 to v1.0.11).
- Books with 5,000 chapters open without restarting the reader (v1.0.14).
- A 5,000-chapter book with a cover now opens in about 9 seconds, down from about 28, with the Table of Contents built in the background while the book stays open (v1.0.16).
- A book on the SD card opens for the first time in about 1.5 seconds, down from about 5 (v1.0.16).
- The status bar's battery icon shows a lightning bolt while charging (v1.0.16).
- X3 battery percent follows the real 650 mAh battery: the firmware loads that capacity into the battery chip, which otherwise counts against a 3,000 mAh default (v1.0.17).

### Home, statistics and sleep

- Home has tabs, and any row can be pinned to Favorites (v1.0.0). On the X3 the side buttons switch Home tabs, and holding one moves the current tab (v1.0.1).
- The Recent tab shows one book at a time with its cover, reading time, reading days, progress and an expected finish date (v1.0.11 and v1.0.14).
- A Book + quotation sleep screen draws a random saved quotation with the book's cover (v1.0.11).
- On the X3, sleep can end with a full black and white refresh, which keeps grey levels on the sleep screen from drifting darker (v1.0.12).
- Wake into the book opens the book you were reading instead of Home (v1.0.6).

### Interface languages

English, Vietnamese and Simplified Chinese (v1.0.1 and v1.0.4).

## Known limits

- File Transfer has no password. While its screen is open, anyone who can reach the device's page can read, upload or delete files on the card. Close the screen when you are done.
- Timings in the release notes were measured on an X3 on USB power.
- Some Bluetooth page turners connect but send reports the reader cannot decode.
- A book with every chapter merged into one very large file can take close to two minutes to open the first time.

Each release lists its own known issues.

## Install

Download a release from [Releases](https://github.com/TenorGroup/cross-releases/releases) or from [cross.tenor.vn](https://cross.tenor.vn). The SD card package holds `firmware.bin` and the font set. Devices already on tenor/cross update over the air from cross.tenor.vn.

## Build

```sh
git clone --recursive https://github.com/TenorGroup/cross.git
cd cross
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio==6.1.19
pio run -e gh_release
```

The application image is written to `.pio/build/gh_release/firmware.bin`. Host tests and further build notes are in [docs/](docs/).

## Attributions

tenor/cross exists because of the work of others.

### Project ancestry

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) by Dave Allie and contributors, MIT. This repository branches from CrossPoint commit `c33a8b0e` (13 September 2026), and the full CrossPoint history up to that commit is kept here with its original authors. Most of the reader (EPUB, TXT and XTC reading, KOReader sync, Calibre wireless, tilt page turning, the web file manager) is CrossPoint's.
- [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk), MIT, included as the `freeink-sdk` submodule. The tenor/cross SDK branch starts from FreeInk commit `fde240f` and keeps FreeInk's history. The SDK NOTICE keeps the Open X4 E-Paper Contributors attribution.
- Upstream fixes taken into tenor/cross after the branch point are cherry-picked with their original author. So far: "clear ligature views when releasing SD font caches" by Sung-jin Brian Hong (CrossPoint #3581).

### Fonts

- Literata, Atkinson Hyperlegible Next, Be Vietnam Pro, Geist, Livvic, Noto Sans, Noto Serif, Noto Sans Arabic, Noto Sans Hebrew, Noto Sans SC and OpenDyslexic: SIL Open Font License 1.1.
- Ubuntu: Ubuntu Font Licence.

Each family's licence text and copyright notice is kept next to its source files or in `licenses/fonts/`.

### Libraries

wolfSSL (GPL version 3 or later), NimBLE-Arduino and Apache Mynewt NimBLE (Apache 2.0), FreeType (FreeType Project Licence), ArduinoJson and QRCode (MIT), JPEGDEC and PNGdec (Apache 2.0), SdFat, WebSockets (LGPL), and the typst/hypher hyphenation patterns. Lucide icons are used under their ISC and MIT notices. Full terms are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.

## Licence

The application code is under the MIT licence in [LICENSE](LICENSE). Dependencies and fonts keep their own licences. Firmware built with the bundled wolfSSL is distributed under the GPL route, so each release ships its corresponding source.

tenor/cross is made by Tenor Group JSC. It is not affiliated with Xteink or with the CrossPoint project.

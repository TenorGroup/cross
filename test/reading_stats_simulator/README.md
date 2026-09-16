Integration checks run the actual X3 simulator with a temporary SD directory.
They use Python plus Pillow for image comparisons and take about five minutes.

```sh
pio run -e simulator_x3_uc8279
python3 -m unittest discover -s test/reading_stats_simulator -v
```

The tests verify that dated and undated history survives two process starts, successful
page turns accumulate, and rejected backward turns at the beginning of a book add zero.
The loader also keeps the newest 30 dates from longer input, merges duplicate dates,
and rejects negative, oversized, boolean and string counters.
Each run must enter and exit the real TXT reader. Existing SD data is left untouched.

Recent-book coverage uses ten source entries, including a missing file, and opens the
last valid book with one backward step from the tab band. Wake coverage observes
the real renderer's requested refresh modes, a complete Home sleep/wake cycle, and
EPUB resume with Tenor and Quick Resume sleep screens. EPUB uses B/W text for this
test so its anti-aliasing passes do not obscure the activity-level refresh count.
The simulator cannot reproduce the panel's waveform or physical ghosting. Those
require the USB-connected device and a visual check.

Reading statistics checkpoint the active book and the daily total together. The
suite interrupts the latest checkpoint and verifies recovery from the previous
snapshot. Nine seconds spent in the reader menu must not count as reading time.

Quotation tests select actual EPUB words through the reader menu, save their source
position, restart, and verify duplicate saves create one record. Preview tests turn
pages and return to the browser for EPUB, TXT and XTC while hashing progress,
bookmarks and JSON stores before and after. A further restart preserves them.

`WakeButtonsTest` in the host CMake suite covers all seven individual keys in all
four wake groups, short taps, interrupted holds, switching keys, a stuck side key,
and timer wrap. ADC sampling, light-sleep power and physical panel behavior need
hardware validation.

Serial diagnostics in logging builds: `CMD:HOME`, `CMD:READ_RECENT`,
`CMD:BOOK_STATS`, `CMD:QUOTES`, `CMD:CLOCK_SYNC`, `CMD:MEMORY`,
`CMD:BUTTON_ADC`, `CMD:SETTINGS_READER`, `CMD:STATUS_BAR_SETTINGS`, and `CMD:SCREENSHOT`. Navigation follows the normal activity
lifecycle. Reading and clock sync can update the corresponding real stores.

Typography coverage changes the indentation choice with paragraph spacing still
enabled, compares real page images across warm-cache reloads, and checks that the
menu updates its preview and persists the choice across restart. The clock test
waits 65 seconds between captures: the idle frame and refresh count must stay
unchanged, then returning to the same page must update only the footer. Set
`CROSSPOINT_TEST_ARTIFACTS` to a temporary output directory to retain these images.

Font boundary coverage uses the existing converter and bundled Noto Sans to create a temporary ASCII cpfont. Install `freetype-py` in the Python test environment. It exercises 16, 17 and 130 total families through discovery, editor navigation, applying the final font and returning to the book. It also verifies that selecting the sole size, or confirming the current popup size and returning, does not reload the EPUB section.

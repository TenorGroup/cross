# Settings Sleep tab regression

Run the bounded production harness:

```sh
python3 test/menu_sleep_split/run.py --repo . --output /tmp/menu-sleep-split --sanitize
```

The harness compiles unmodified production category rebuilding, favorite key lookup, the Home settings branch, web settings serializer, SettingsTabs, and MenuCustomization persistence. Rendering, radio, storage, GPIO, dictionaries and font discovery use explicit host boundaries. Storage faults run against an in-memory filesystem. This test does not claim a physical-device journey.

Coverage: 12 board/IMU/optional-row cases plus 1 persistence case, unique row ownership, Sleep ID7, existing tab IDs, Home entries, old pin routing, the retired wake row and its old pin, conditional light row, default order, legacy order migration, save/reload, invalid/duplicate IDs, backup recovery, reordering and failed-save rollback. The web fixture hashes symbolic string IDs so translation regeneration does not change the category/key/value contract. The serializer body is production code. Comparing its output before and after menu navigation also checks byte equivalence within each run.

`web-schema-sha256.json` records pre-split serializer output for those 12 cases. Change it only for a deliberate web schema/value change, with independent review. `--web-baseline` additionally compares a directory of complete recorded `*-web.json` outputs.

All 12 hashes include the header clock's 3 values in 82183a56 (Hide, Time, Time and date): `clockShowHeader` is an
enum of STR_HIDE, STR_CLOCK_HEADER_TIME and STR_CLOCK_HEADER_TIME_DATE instead of a toggle. Putting only that row
back to the toggle reproduces each earlier hash.

The start-screen regression adds 12 `-ugly` cases with the same board, sensor and optional-row matrix. The 12 cross hashes stay unchanged. Each ugly schema removes `wakeIntoBook` and appends `uglyStartScreen` with Book, Diary, Recent and Desk, default Diary (1). With `--web-baseline`, the test derives this exact change from the recorded cross schemas before checking the new hashes. Device rows and pinned settings also expose only the start row for the active shell.

The 4 Pro hashes include the dynamic bar default change in 3afef460: `homeButtonDoubleTapAction` is Ignore (1). Replacing only that value with the earlier ToggleFrontlight (10) reproduces each earlier hash; all other fields retain the same oracle. The harness also asserts Ignore directly.

Recommended CTest registration: `MenuSleepSplitRegression`, `RUN_SERIAL TRUE`, `TIMEOUT 180`. Root owns CMake registration.

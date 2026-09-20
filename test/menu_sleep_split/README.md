# Settings Sleep tab regression

Run the bounded production harness:

```sh
python3 test/menu_sleep_split/run.py --repo . --output /tmp/menu-sleep-split --sanitize
```

The harness compiles unmodified production category rebuilding, favorite key lookup, the Home settings branch, web settings serializer, SettingsTabs, and MenuCustomization persistence. Rendering, radio, storage, GPIO, dictionaries and font discovery use explicit host boundaries. Storage faults run against an in-memory filesystem. This test does not claim a physical-device journey.

Coverage: 24 board/theme/IMU/optional-row cases, unique row ownership, Sleep ID7, existing tab IDs, Home entries, old pin routing, conditional wake/light rows, wake hint predicate, default order, legacy order migration, save/reload, invalid/duplicate IDs, backup recovery, reordering and failed-save rollback. The web fixture hashes symbolic string IDs so translation regeneration does not change the category/key/value contract. The serializer body is production code. Comparing its output before and after menu navigation also checks byte equivalence within each run.

`web-schema-sha256.json` records pre-split serializer output for those 24 cases. Change it only for a deliberate web schema/value change, with independent review. `--web-baseline` additionally compares a directory of complete recorded `*-web.json` outputs.

Recommended CTest registration: `MenuSleepSplitRegression`, `RUN_SERIAL TRUE`, `TIMEOUT 180`. Root owns CMake registration.

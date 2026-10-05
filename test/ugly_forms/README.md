# Ugly answer-sheet acceptance

These checks exercise production code. Helpers are tested through the production QuestionSheet.cpp with an owned fixture catalog and synthetic font/drawing boundaries. The component and queue build uses actual production I18n.h, I18nKeys.h, I18n.cpp and I18nStrings.cpp, including its profile guards and real English strings. apply.py extracts unchanged shared applySettingValue/saveSettings methods and the actual Back branch; hardware/font/save lifecycle boundaries are injected. Synthetic glyph widths establish input/geometry contracts, not actual-font pixel acceptance.

```sh
python3 test/ugly_forms/component.py --source-root . --output ../evidence/component
python3 test/ugly_forms/apply.py --output ../evidence/apply
python3 test/ugly_forms/component.py --queue --source-root . --output ../evidence/queue
python3 test/ugly_forms/switch.py --source-root . --dependency-root /absolute/main/source --output ../evidence/switch
python3 test/ugly_forms/red_route.py --program /absolute/frozen-5a/program --output ../evidence/red
python3 test/ugly_forms/inventory.py --source-root . --output ../evidence/catalog
```

red_route.py is a frozen old-route evidence capture, requiring preserved5a binary SHA807439c0b23f470e2ee0731082e9292afa20a7baeb5a0f5fe39efb27010967de. It deliberately records the old standard Settings Display screen from both ugly levels, 2 launches. Do not treat its successful subprocess return as new-form acceptance.

Catalog inventory uses the existing settings_catalog/category production harness. First run category/run.py with --repo . --output ../evidence/catalog --sanitize and --i18n-dir pointing to the current generated I18n directory; supply the existing ArduinoJson include directory via CPLUS_INCLUDE_PATH when this lightweight checkout has no dependencies. Then run inventory.py --source-root . --output ../evidence/catalog with the same include environment. It compiles the identical harness with an inventory formatter that adds each real row's kind and option count. Fixture includes128SD fontfamilies and8dictionaries; dynamic option counts describe that explicit fixture.

Latest actual-I18n host results: component X3 15 checks and X4 Pro 52 checks (67 combined), apply 24 checks, queue 9 checks per profile (18 combined), all with 0 failures. ASan/UBSan enabled. Queue checks extract the actual queueForm/handleCustomInput methods and cover busy frame, unavailable mutex, paper paint, stale tap/hold epoch, and lock release. Mutations for preview commits, paper Back, no-op save, invalid option, and ignored save error all failed as expected.

Measured source catalog groups stable IDs0..8: X3 65 rows, X4 51, X4 Pro50 when dictionary/footnote rows available. RTC availability0/1 tested. Full category harness covers24capability combinations.

component.py defaults to both compile profiles: FREEINK_DEVICE_X4PRO=0 and =1. --profile selects a single profile. --source-root selects the complete production snapshot, including helper headers, I18n, UglyInk navRow, and Settings queue methods. --source only overrides the helper .cpp for mutation testing. production-manifest.json records exact production file SHA-256 values. Outputs are split under x3/ and x4pro/. The former fake I18n stub was removed. Baseline X3 compilation with real I18n reproduced 2 missing STR_UGLY_X4_MORE errors, while its X4 Pro build passed 52 checks. Shared STR_UGLY_MORE passes both profiles.

switch.py compiles actual UglySwitch.cpp with actual Screen, Activity, Shell, Logic and I18n headers plus an existing native simulator SDK, using no test stubs. It performs syntax-only checks for X3 and X4 Pro; it neither invokes PIO nor links or exercises the Activity lifecycle. --dependency-root supplies existing SDK headers when the isolated source has no dependencies. commands and transitive production/header SHA manifests are kept per profile. Baseline X3 reproduced 3 missing STR_UGLY_X4_SHELL_* errors; X4 Pro compiled. Shared shell keys compiled on both profiles. Component assertions establish helper behavior; complete adapter runtime acceptance remains separate.

Acceptance still requiring fresh simulator: all visible group routes on X3/X4 Pro, actual VI/EN labels, both ugly levels, actual question/answer/comment/page geometry, candidate quip updates and queued-input/popup boundary handling. Module2 Text and Status adapters will be accepted separately after module1 freeze. Real power/RAM/render timing and firmware size are owned by the build/resource lane.

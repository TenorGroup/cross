# Reader build memory lifecycle

`run_tests.py` compiles the production background scheduler, foreground partial extension,
heap gates, suspension helper and EPUB page-turn function. The queued-turn methods come
verbatim from `ReaderActivity.cpp`. Heap, Section, render lock and platform calls are
instrumented host boundaries. Fifteen BLE-capable cases and twelve cases without BLE capability verify release under the lock, preservation of
cached position, no restart churn, foreground progress, and queued external-turn draining.
This projection tests control flow; it does not measure ESP32 heap or panel AA.

`SectionBuildBudgetTest` compiles the actual Section, parser, page and CSS implementations
against the established section-cache storage/render stubs. It creates a partial cache,
starts a shorter rebuild, suspends it, and checks exact old cache bytes, page count, position,
visible offset and page load. It then extends past the watermark on demand and checks that
the requested page remains readable after suspension.

The 64 KiB free / 32 KiB largest-block start gate is a conservative pre-parser budget. The
32 KiB / 16 KiB resident tick budget stays unchanged. R1 measured deferred start at
53,428 / 42,996 bytes before parser/CSS allocation and eventually 29,100 / 17,396 bytes.
The new start budget excludes that observed envelope. Hardware R3 must establish the
resulting AA headroom; these values are not an optimized allocation model.

Standalone:

```sh
cmake -S test/reader_build_budget -B /tmp/reader-build-budget -DGTEST_SOURCE=/path/to/googletest
cmake --build /tmp/reader-build-budget -j 4
ctest --test-dir /tmp/reader-build-budget --output-on-failure
```

The enclosing test tree uses `add_subdirectory(reader_build_budget)`.

R3 exposed a separate phase overlap: a healthy parser remained alive after the cold first
page, and BLE post-init failed at 33,268 free / 30,708 largest bytes. On BLE-capable targets,
`blePageTurnerEnabled` now defers background starts and releases the completed foreground
page build before painting, so the later radio init gets the parser/CSS allocation back.
Foreground requests still build past partial watermarks. Disabling BLE restores healthy
prefetch; low-heap suspension retains its existing latch. The extra real-Section test
verifies that a cold first page survives suspension and reopen before any paint.

Capability correction: firmware sets `FREEINK_CAP_BLE_HID_HOST=1` in `platformio.ini`.
`CROSSPOINT_BLE_HID_HOST` is local to `main.cpp` and cannot enable reader code. The earlier
R4 projection injected that local macro, so its BLE-enabled green result did not establish
production capability selection. The harness now passes the actual firmware flag and
records `compile-command.json`; separate CTests cover absent and explicit-zero capability.
Before the production guard correction, the real enabled flag reproduced four failures;
afterward the enabled/absent/zero matrix passes 15/12/12 cases.

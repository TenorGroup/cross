# Reader build memory lifecycle

`run_tests.py` compiles the production background scheduler, foreground partial extension,
heap gates, suspension helper and EPUB page-turn function. The queued-turn methods come
verbatim from `ReaderActivity.cpp`. Heap, Section, render lock and platform calls are
instrumented host boundaries. The production projection verifies release under the lock,
preservation of cached position, no restart churn, foreground progress, and queued
external-turn draining. Regression cases also cover a 900-15,000 ms partial-cache restore:
a successful first tick stays silent, repeated useful ticks paint progress once, and a
failed first tick does not paint an extra popup or reset refresh cadence. A failed
background tick must preserve the current page as its bounded retry target; the existing
start heap gate must still block an unsafe retry build.
The same popup contracts run against the exact initial/resume loop predicate extracted
from `renderBook`; its cache loading and `startBuild` inputs remain instrumented boundaries.
A background tick failure also latches speculative work across the section recreation
performed by `renderBook`. Thirty background ticks must stay idle, while an explicit
foreground watermark target still extends the cache and releases its parser before render.
A fresh reader instance restores healthy background admission after the failed visit ends.
With parser parking enabled, low-heap and BLE gates keep completed pages readable while
the parser is released. A parked heap-latched build stays idle without a zero-delay loop,
foreground demand resumes it, and the latch still blocks later background ticks. A BLE-only
park resumes background work only after the user disables BLE. The existing cases leave
parking disabled in the Section stub and continue to cover the partial-commit fallback.
This projection tests control flow; it does not measure ESP32 heap or panel AA.

`SectionBuildBudgetTest` compiles the actual Section, parser, page and CSS implementations
against the established section-cache storage/render stubs. Its ISO-8859-1 fixture uses
the supported cold fallback because checkpoint resume currently accepts UTF-8 and US-ASCII.
It builds a partial cache across bounded ticks, starts a shorter rebuild, suspends it,
and checks exact old cache bytes, page count, position,
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

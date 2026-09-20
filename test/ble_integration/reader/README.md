# BLE reader and main-loop integration

This suite compiles production function bodies with fake hardware and scheduling boundaries. It covers BLE-01, BLE-04 and BLE-05 with 50 scenarios. The runner extracts current source every time and records SHA-256 values for the inputs beside the generated executable and logs.

The exercised chain is the main BLE queue pump, ActivityManager::pageTurn, ReaderActivity's external input queue and drain, the EPUB/TXT/XTC page mutation, repaint requests and reading-stat recording. The suite uses the real TXT/XTC base reader loop. For EPUB it preserves the source order of the real overlay, end-menu, Confirm, Back and external-drain branches from the larger loop. Other EPUB branches remain outside this projection. The main projection contains the radio lifecycle/input section and both inactivity-clock updates. Static main-loop state becomes per-fixture state so scenarios remain isolated.

Fake boundaries include radio reports and async-start result, renderer lock ownership, physical input, time, book sections, repaint notifications and stats persistence. The preview handler is also extracted from production. The lock throws if a path attempts a recursive or blocking acquisition. AddressSanitizer and UndefinedBehaviorSanitizer instrument every run.

Checks cover page/stats/repaint agreement, rendering contention, generation changes, input bursts, chapter boundaries, end-of-book navigation, menu and overlay ownership, the EPUB manual-turn guard, physical taps during render in normal and preview readers, Back/Confirm priority, mapped versus unmapped reports, a tab tilt snapshot carried to both inactivity clocks, remote-only reading and inactivity after reports stop. Idle-off tests exercise all four page buttons, touch, a failed rearm and a connected remote.

This is a host integration projection. It does not render a framebuffer, run the FreeRTOS scheduler or emulate NimBLE. Simulator and device acceptance remain separate gates.

Run directly from the repository root:

```sh
python3 test/ble_integration/reader/run_reader_tests.py . /tmp/ble-reader-tests
```

CTest invokes the same runner with the configured C++ compiler. Generated C++, binary, source hashes and logs stay in the build directory.

# BLE reader and main-loop integration

This suite compiles production function bodies with fake hardware and scheduling boundaries. It covers BLE-01, BLE-04, BLE-05 and BLE-06 and the status bar's link note with 77 scenarios. The runner extracts current source every time and records SHA-256 values for the inputs beside the generated executable and logs.

The exercised chain is the main loop's page turner pass (the Scene it builds and `bleturner::tick`, with the module's real `Runtime.cpp` compiled alongside over a fake radio port and the firmware's delivery functions from `src/BlePageTurnerHost.cpp` and `main.cpp`), ActivityManager::pageTurn, ReaderActivity's external input queue and drain, the EPUB/TXT/XTC page mutation, repaint requests, the link note in the status bar's title slot (`ReaderActivity::linkNoteTitle` and the redraw in `onTick`) and reading-stat recording. The suite uses the real TXT/XTC base reader loop. For EPUB it preserves the source order of the real overlay, end-menu, Confirm, Back and external-drain branches from the larger loop. Other EPUB branches remain outside this projection. The main projection contains the page turner pass and both inactivity-clock updates. A radio start runs on its own task on the device; the fixture queues it and runs it between passes, so the pass that asks for a start does not yet see the radio running. Static main-loop state becomes per-fixture state so scenarios remain isolated.

Fake boundaries include radio reports and async-start result, renderer lock ownership, physical input, time, book sections, repaint notifications and stats persistence. The preview handler is also extracted from production. The lock throws if a path attempts a recursive or blocking acquisition. AddressSanitizer and UndefinedBehaviorSanitizer instrument every run.

Checks cover page/stats/repaint agreement, rendering contention, generation changes, input bursts, chapter boundaries, end-of-book navigation, menu and overlay ownership, the EPUB manual-turn guard, physical taps during render in normal and preview readers, Back/Confirm priority, mapped versus unmapped reports, a tab tilt snapshot carried to both inactivity clocks, remote-only reading and inactivity after reports stop. After idle-stop, all 4 page buttons, touch and unrelated paint keep the radio off. Explicit connect and a new book visit can restart it. A build suspension resumes after its page is painted. Tests also cover a failed explicit start and a connected remote remaining on.

This is a host integration projection. It does not render a framebuffer, run the FreeRTOS scheduler or emulate NimBLE. Simulator and device acceptance remain separate gates.

Run directly from the repository root:

```sh
python3 test/ble_integration/reader/run_reader_tests.py . /tmp/ble-reader-tests
```

CTest invokes the same runner with the configured C++ compiler. Generated C++, binary, source hashes and logs stay in the build directory.

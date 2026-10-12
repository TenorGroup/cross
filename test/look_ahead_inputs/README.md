The 6 host scenarios cover look-ahead scheduling during an ADC contact, a scheduled probe contact, a raw HID notification and an X4 Pro GPIO7 contact, plus quiet completion and BLE input arriving during the completing quantum.

Run from the repository root:

```sh
python3 test/look_ahead_inputs/run_tests.py . /path/to/output
```

Pass `--reader-source /path/to/baseline` to run the same tests against an earlier reader implementation. The output contains the compiled projection, compiler command, source hashes and results.

The reader scheduler and heap gates are extracted verbatim. The fixture imports the existing reader build-budget hardware model. Input decoding uses production `InputManager` ADC/GPIO reads and debounce, `HalGPIO` press plans and raw input checks, the full SDK HID host and key decoder, and the page-turner runtime. The radio port's polling and queue forwarding functions come from `SdkRadio.cpp`. A raw 8-byte HID frame enters through the fake peripheral notification callback; the runtime delivers it through production `bleDeliver`, `ActivityManager::pageTurn` and the reader's external-turn queue.

A build quantum injects each contact while the main loop is occupied. The next quantum requires an intervening main-loop input poll. GPIO contacts use the production 5 ms debounce, so an X4 Pro contact can take 2 polls to produce an edge. The X4 Pro case asserts that the raw-input helper misses GPIO7 and the SDK polls detect it. Every accepted input pass must skip layout, and incomplete or interrupted work must keep `lookAheadPage` at -1. Quiet work must reach the page target over multiple main passes before setting the latch.

The main-loop harness follows the production input-poll, BLE-tick and reader-loop order; extraction asserts that order in `main.cpp`. The mapped input edge boundary forwards the SDK's any-pressed/any-released result. The test covers scheduler interruption and BLE queue delivery. It does not cover panel timing, real parser duration or checkpoint replay during parking, RF delivery, touch, the ADC sampling timer, page-turn mapping or rendering after the input. Those require their own tests or device measurements.

CMake registration is `add_subdirectory(look_ahead_inputs)` from `test/CMakeLists.txt`. It adds `look_ahead_real_input_routes`.

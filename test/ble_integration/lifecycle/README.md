The runtime harness compiles the page turner module as the firmware links it:
`lib/BlePageTurner/src/Runtime.cpp` and `SdkRadio.cpp` (the wrapper over the SDK's
BleKeyboardHost), plus `FileTransferState.h` and the firmware's own host functions for heap,
cache release, file transfer and CPU speed, extracted unchanged from `src/BlePageTurnerHost.cpp`,
with the ESP/FreeRTOS path enabled. A queued scheduler and SDK callbacks reproduce cancellation
before worker execution and suspension during initialization. The main power branch is
extracted unchanged from `src/main.cpp` and executes in the same harness. Eleven scenarios cover
ownership, power locks, teardown, failed initialization and idle-stop status.

The sleep harness executes the complete production `goToSleep` and
`enterDeepSleep` bodies. Ten scenarios cover deferred completion, deadline
cancellation, abort before hardware shutdown, successful shutdown ordering,
retry ownership after accepted input and inactivity arithmetic through long uptime
and timer wrap, explicit sleep after failure, and a tab tilt carried to the next
main-loop pass. Shared sleep-transition publication requires RenderLock in the
fixture. The accepted-input clocks and autosleep gate execute their unchanged
production source.
Its activity loop, BLE availability and hardware boundaries are controlled fakes.

Run from the repository root:

```sh
python3 test/ble_integration/lifecycle/run_runtime.py --source . --output /tmp/ble-runtime-test
python3 test/ble_integration/lifecycle/sleep/run.py --source . --output /tmp/ble-sleep-test
```

Both scripts accept `--compiler /path/to/c++`. CTest uses the configured compiler
and keeps generated source, binaries, logs and SHA-256 manifests in the binary
tree. These host regressions complement full simulator and physical-device
acceptance; they do not execute NimBLE or a physical power controller.

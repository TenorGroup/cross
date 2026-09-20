# X4 sleep clock checks

`run.py --output <directory>` compiles the complete current `HalPowerManager.cpp`
and pinned SDK `PowerManager.cpp`. The fixture replaces GPIO, board detection,
button reads, and final ESP-IDF sleep boundaries. It checks C3 X4 supply retention,
C3 X3 SD cutoff, other-board isolation, S3 rails, hold ordering, power-button release,
and wake arming. Both builds use ASan/UBSan and warnings as errors.

`wiring.py` extracts `sleepWithConfiguredButtons()` verbatim from current
`src/main.cpp`, compiles it with narrow global-object stubs, and checks eight
board/epoch/wake-mode combinations. It also checks structurally that the three
device sleep routes use this helper and no direct device sleep call bypasses it.
This does not compile all of main.cpp or simulate those three complete journeys.

Both scripts accept `--source-root` to replay immutable production sources.
The power runner accepts the baseline two-argument API and demonstrates a runtime
retention failure. The helper stub also accepts the old API, so its baseline
failure is behavioral rather than a missing-signature compiler error.

Hardware acceptance remains separate: battery-only sleep and wake, actual epoch
advance through sleep, power-button behavior, USB detach, overnight drift and
sleep current. Full power loss may invalidate time. These host checks do not
measure battery current, RTC timer behavior, or physical supply retention.

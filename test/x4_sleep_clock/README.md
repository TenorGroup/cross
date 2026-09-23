# X4 sleep clock checks

`run.py --output <directory>` compiles the complete current `HalPowerManager.cpp`
and pinned SDK `PowerManager.cpp`. The fixture replaces GPIO, board detection
and final ESP-IDF sleep boundaries. It checks C3 X4 supply retention,
C3 X3 SD cutoff, other-board isolation, S3 rails, hold ordering, power-button release,
and wake arming. Both builds use ASan/UBSan and warnings as errors.

`wiring.py` extracts `sleepUntilPowerButton()` verbatim from current
`src/main.cpp`, compiles it with narrow global-object stubs, and checks four
board/epoch combinations. It also checks structurally that the three device
sleep routes use this helper, that no direct device sleep call bypasses it, and
that no setting reaches it. This does not compile all of main.cpp or simulate
those three complete journeys.

`journey.py --output <directory>` compiles that same helper with the complete
`HalPowerManager.cpp` and SDK `PowerManager.cpp`, for X3 and X4 with and without
a valid clock, while the settings boundary holds each value 0 to 3 that a card
may still store under the retired `wakeButtons` key. Every recorded call
sequence must equal `power-only-trace.json`, recorded from the b2e808f sources
with `wakeButtons=0`. Replaying b2e808f with `--source-root` fails for values 1
to 3 on X3, where that tree entered the light-sleep button loop.

All three scripts accept `--source-root` to replay immutable production sources.

Hardware acceptance remains separate: battery-only sleep and wake, actual epoch
advance through sleep, power-button behavior, USB detach, overnight drift and
sleep current. Full power loss may invalidate time. These host checks do not
measure battery current, RTC timer behavior, or physical supply retention.

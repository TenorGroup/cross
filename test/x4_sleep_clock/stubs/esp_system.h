#pragma once
#include <cstdlib>

// The SDK's deepSleep() records a rejected sleep entry in RTC memory, then
// resets. The harness's esp_deep_sleep_start() never returns, so this reset
// is never reached; it aborts loudly if it ever is.
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
[[noreturn]] inline void esp_restart() { std::abort(); }

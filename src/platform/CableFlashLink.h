#pragma once
// The X4 Pro end of the cable flash channel (CableFlash.h): the TCF1 lines the main loop passes on,
// the session over the real cable into the OTA slot that is not running.
#include <cstddef>

#include "platform/CableFlash.h"

class GfxRenderer;
class MappedInputManager;

namespace cableflash {

// A line from the host that starts with "TCF1 ": HELLO and STATUS answer at once; BEGIN opens the
// update screen, which runs the session with no question to the user.
void onLine(const char* line, GfxRenderer& renderer, MappedInputManager& input);
// The session: blocks until DONE or FAIL. `progress` hears the bytes written so far.
Result run(const Begin& begin, void (*progress)(size_t written, size_t total, void* ctx), void* ctx);
// One line to the host.
void say(const char* line);

}  // namespace cableflash

#pragma once
// The X4 Pro end of the cable flash channel (CableFlash.h): the TCF1 lines the main loop passes on,
// the session over the real cable into the OTA slot that is not running, and the recovery consent.
#include <cstddef>

#include "platform/CableFlash.h"

class GfxRenderer;
class MappedInputManager;

namespace cableflash {

// Power on with the recovery keys held: the first BEGIN within kConsentMs needs no yes on the screen
// (a touch screen that no longer answers can still be reflashed).
void grantRecoveryConsent();
// A line from the host that starts with "TCF1 ": HELLO and STATUS answer at once; BEGIN opens the
// update screen, which asks the user and then runs the session.
void onLine(const char* line, GfxRenderer& renderer, MappedInputManager& input);
// The session: blocks until DONE or FAIL. `progress` hears the bytes written so far.
Result run(const Begin& begin, void (*progress)(size_t written, size_t total, void* ctx), void* ctx);
// One line to the host.
void say(const char* line);

}  // namespace cableflash

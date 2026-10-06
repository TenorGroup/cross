#pragma once
class GfxRenderer;

namespace ugly {
// Draws the sleep screen into the portrait frame: one of 8 doodles and a line of abuse (src/shells/ugly/UglySleepSet.h).
// False when the frame is too small; the caller shows it.
bool drawSleep(GfxRenderer& renderer);
// The boot screen (src/shells/ugly/UglySleepSet.h). False when the frame is too small; the caller shows it.
bool drawBoot(GfxRenderer& renderer);
// The notice shown while the device gets ready to sleep ("Yawning...").
const char* sleepNotice();
// The notice over the sleep screen while the device wakes, and the one before a quiet restart.
const char* wakeNotice();
const char* loadingNotice();
}  // namespace ugly

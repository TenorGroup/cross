#pragma once
class GfxRenderer;

namespace ugly {
// Draws the sleep screen into the portrait frame: one of 8 doodles and a line of abuse (src/shells/ugly/UglySleepSet.h).
// False when the frame is too small; the caller shows it.
bool drawSleep(GfxRenderer& renderer);
}  // namespace ugly

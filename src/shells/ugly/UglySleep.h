#pragma once
class GfxRenderer;

namespace ugly {
// Draws the sleep screen into the X3 portrait framebuffer: the doodle, then the line of abuse the day
// picks. False, with the frame untouched, when the frame is not the X3's. The caller shows it.
bool drawSleep(GfxRenderer& renderer);
}  // namespace ugly

#pragma once
class GfxRenderer;
namespace readingstatsview {
constexpr int HEIGHT = 430;
constexpr int BADGE_HEIGHT = 40;
void draw(const GfxRenderer& renderer, int top, bool sleep = false, bool compact = false);
void drawSleep(const GfxRenderer& renderer);
}  // namespace readingstatsview

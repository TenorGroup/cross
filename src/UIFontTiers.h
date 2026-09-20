#pragma once
#include <cstdint>
#include <EpdFontFamily.h>

// Families have static lifetime. The render owner applies all three roles
// together under RenderLock, then invalidates fallback and layout caches.
enum class UIFontRole : uint8_t { Caption, Subtitle, Body };
const EpdFontFamily& uiFontTierFamily(UIFontRole role, uint8_t size);

class GfxRenderer;
// Caller holds RenderLock and republishes layout/hit tables after success.
// Returns false before changing anything if the three UI aliases are absent
// or point at SD fonts. Reader family, size and preference remain independent.
bool applyUiFontSize(GfxRenderer& renderer, uint8_t size);

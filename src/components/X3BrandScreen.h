#pragma once
class GfxRenderer;
// Approved native X3 artwork. Returns false so callers can draw their fallback.
bool renderX3BrandScreen(GfxRenderer& renderer, bool boot);

#pragma once
class GfxRenderer;
// Approved native X3 artwork and X4 Pro boot artwork. Returns false so callers can draw their fallback.
bool renderBrandScreen(GfxRenderer& renderer, bool boot);

#pragma once
#include <InlineSymbols.h>
namespace buttonSymbols {
void install();
inlineSymbols::Spec resolve(int id);
bool drawLabel(const GfxRenderer& renderer, const char* label, int x, int y);
}  // namespace buttonSymbols

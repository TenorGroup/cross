#pragma once
#include <InlineSymbols.h>
namespace buttonSymbols {
struct SymbolBounds {
  int left;
  int right;
};
void install();
inlineSymbols::Spec resolve(int id);
// `net` is the target pixel height of a button symbol, matched to the status-bar battery box.
bool drawLabel(const GfxRenderer& renderer, const char* label, int x, int y, int net = 14);
SymbolBounds horizontalBounds(const char* label, int net = 14);
}  // namespace buttonSymbols

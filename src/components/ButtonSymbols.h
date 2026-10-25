#pragma once
#include <InlineSymbols.h>
namespace buttonSymbols {
struct SymbolBounds {
  int left;
  int right;
};
void install();
// What holding the two side buttons does in a book list: PageForward (hold = preview) and PageBack (hold = delete) take
// the buttons the side layout gives them (MappedInputManager::mapButton): Prev/Next and Next/Prev one each, Next/Next only
// preview on both, Prev/Prev only delete on both, a disabled layout none.
enum class SideHold { Both, PreviewOnly, DeleteOnly, None };
SideHold sideHold();
inlineSymbols::Spec resolve(int id);
// `net` is the target pixel height of a button symbol, matched to the status-bar battery box.
bool drawLabel(const GfxRenderer& renderer, const char* label, int x, int y, int net = 14);
SymbolBounds horizontalBounds(const char* label, int net = 14);
// The key a hint label stands for: 0 Select, 1 Back, 2 Up, 3 Down, 4 Left, 5 Right; -1 for any other word.
int labelId(const char* label);
}  // namespace buttonSymbols

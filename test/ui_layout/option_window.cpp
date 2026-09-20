#include <cassert>
#include <initializer_list>
#include <cstdio>
#if __has_include("components/OptionPopupLayout.h")
#include "components/OptionPopupLayout.h"
#else
struct OptionPopupWindow { int first, count; bool paged; };
OptionPopupWindow optionPopupWindow(int total, int, int, int, int) { return {0, total > 16 ? 16 : total, false}; }
#endif
int main() {
 for (int total : {3, 16, 17, 1000}) for (int selected = 0; selected < total; ++selected) {
  const auto w = optionPopupWindow(total, selected, 420, 85, 65);
  assert(w.first <= selected && selected < w.first + w.count);
  assert(w.count > 0 && w.count + (w.paged ? 2 : 0) <= 16);
  assert(85 + (w.count + (w.paged ? 2 : 0)) * 65 <= 420);
 }
 const auto empty = optionPopupWindow(0, 0, 420, 85, 65);
 assert(empty.count == 0);
 puts("PASS: option window keeps all 1036 selections reachable and inside bounds");
}

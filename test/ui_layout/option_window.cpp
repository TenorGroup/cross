#include <cassert>
#include <initializer_list>
#include <cstdio>
#if __has_include("components/OptionPopupLayout.h")
#include "components/OptionPopupLayout.h"
#else
struct OptionPopupWindow { int first, count; bool paged, next; };
OptionPopupWindow optionPopupWindow(int total, int, int, int, int) { return {0, total > 16 ? 16 : total, false, false}; }
#endif
int main() {
 for (int total : {3, 7, 10, 16, 17, 1000}) for (int selected = 0; selected < total; ++selected) {
  const auto w = optionPopupWindow(total, selected, 420, 85, 65);
  const int buttons = w.paged ? 1 + (w.next ? 1 : 0) : 0;  // "previous page", and "next page" unless a lone option took it
  assert(w.first <= selected && selected < w.first + w.count);
  assert(w.count > 0 && w.count + buttons <= 16);
  assert(85 + (w.count + buttons) * 65 <= 420);
  // Founder 06/10/2026: no page of a single option; it stands in the place of the "next page" button before it.
  assert(w.count >= 2 || total == 1);
 }
 const auto empty = optionPopupWindow(0, 0, 420, 85, 65);
 assert(empty.count == 0);
 puts("PASS: option window keeps all 1053 selections reachable and inside bounds");
}

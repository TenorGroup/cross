#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "components/FileSearchReveal.h"

namespace {

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << message << '\n';
  std::exit(1);
}

filesearch::Drag downward(int deltaY = 60, unsigned long heldMs = 301) {
  filesearch::Drag drag;
  drag.startX = 200;
  drag.startY = 113;
  drag.deltaY = deltaY;
  drag.heldMs = heldMs;
  drag.contacts = 1;
  drag.insideList = true;
  return drag;
}

void revealThresholds() {
  filesearch::Reveal reveal;
  check(!reveal.visible(true, false), "search starts hidden");
  check(!reveal.update(downward(59), true, true, false), "59 px stays hidden");
  check(!reveal.visible(true, false), "59 px does not reveal");
  check(reveal.update(downward(60), true, true, false), "60 px reveals");
  check(reveal.visible(true, false), "60 px is visible");
  check(!reveal.update(downward(120), true, true, false), "visible state changes only once");

  reveal.reset();
  check(reveal.update(downward(120), true, true, false), "120 px reveals");
  reveal.reset();
  check(!reveal.update(downward(60, 300), true, true, false), "300 ms stays hidden");
  check(reveal.update(downward(60, 301), true, true, false), "301 ms reveals");
  reveal.reset();
  check(!reveal.update(downward(120, 120), true, true, false), "fast downward drag changes nothing");
  check(!reveal.visible(true, false), "fast downward drag stays hidden");
}

void revealOriginAndContext() {
  filesearch::Reveal reveal;
  auto drag = downward();
  drag.startY = 112;
  check(!reveal.update(drag, true, true, false), "top band is excluded");

  drag = downward();
  drag.insideList = false;
  check(!reveal.update(drag, true, true, false), "origin outside list is excluded");

  drag = downward();
  check(!reveal.update(drag, true, false, false), "drag away from list top is excluded");
  check(!reveal.update(drag, false, true, false), "non-browser list is excluded");
  check(!reveal.visible(false, true), "query cannot expose search outside a browser");

  drag = downward();
  drag.blocked = true;
  check(!reveal.update(drag, true, true, false), "scrollbar drag is excluded");
  check(!reveal.update(drag, true, true, false), "popup drag is excluded");

  drag = downward();
  drag.contacts = 2;
  check(!reveal.update(drag, true, true, false), "multi-contact drag is excluded");
}

void hideAndQueryPersistence() {
  filesearch::Reveal reveal;
  check(reveal.update(downward(), true, true, false), "setup reveal succeeds");
  auto upward = downward(-1, 50);
  check(reveal.update(upward, true, false, false), "valid upward drag hides empty search");
  check(!reveal.visible(true, false), "empty search is hidden");
  check(!reveal.update(upward, true, true, false), "hidden state does not change twice");

  check(reveal.visible(true, true), "query keeps search visible");
  check(!reveal.update(upward, true, true, true), "query blocks hide");
  check(reveal.visible(true, true), "query remains visible after upward drag");
  check(!reveal.update(downward(), true, true, true), "visible query does not report a state change");
}

}  // namespace

int main() {
  static_assert(sizeof(filesearch::Reveal) == sizeof(bool));
  static_assert(std::is_trivially_destructible_v<filesearch::Reveal>);
  check(filesearch::SEARCH_HEIGHT == 60, "search height is 60 px");
  check(filesearch::SEARCH_GAP == 8, "search gap is 8 px");
  revealThresholds();
  revealOriginAndContext();
  hideAndQueryPersistence();
  std::cout << "file search reveal: PASS\n";
  return 0;
}

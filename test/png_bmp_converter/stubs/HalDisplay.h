#pragma once
struct TestDisplay {
  int getDisplayHeight() const { return 480; }
  int getDisplayWidth() const { return 800; }
};
inline TestDisplay display;

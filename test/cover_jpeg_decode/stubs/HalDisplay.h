#pragma once
// The X3 panel as the cover decoder asks for it: a portrait screen cover is its height by its width.
struct TestDisplay {
  int getDisplayHeight() const { return 528; }
  int getDisplayWidth() const { return 792; }
};
inline TestDisplay display;

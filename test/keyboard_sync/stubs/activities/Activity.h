#pragma once
#include <GfxRenderer.h>
#include <MappedInputManager.h>
#include <string>
#include <utility>
#include "activities/RenderLock.h"

struct KeyboardResult { std::string text; };
struct ActivityResult { bool isCancelled = false; };
namespace keyboard_test {
extern thread_local bool renderLockHeld;
extern bool completedUnderLock;
extern std::string completedText;
}
class Activity {
 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
 public:
  Activity(const char*, GfxRenderer& r, MappedInputManager& i): renderer(r), mappedInput(i) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void loop() {}
  virtual bool saveInputBeforeHome() { return false; }
  virtual void render(RenderLock&&) {}
  void requestUpdate() {}
  void setResult(KeyboardResult r) { keyboard_test::completedText = std::move(r.text); }
  void setResult(ActivityResult) {}
  void finish() { keyboard_test::completedUnderLock |= keyboard_test::renderLockHeld; }
};

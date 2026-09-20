#pragma once
#include <GfxRenderer.h>
#include <MappedInputManager.h>
#include <string>
#include <utility>
#include "activities/RenderLock.h"

struct KeyboardResult { std::string text; bool timedOut = false; };
struct ActivityResult {
  bool isCancelled = false;
  KeyboardResult data;
  ActivityResult() = default;
  ActivityResult(KeyboardResult result) : data(std::move(result)) {}
};
namespace keyboard_test {
extern thread_local bool renderLockHeld;
extern bool completedUnderLock;
extern bool cancelled;
extern bool finished;
extern bool resultSet;
extern bool timedOut;
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
  void setResult(KeyboardResult r) {
    keyboard_test::completedText = std::move(r.text);
    keyboard_test::timedOut = r.timedOut;
    keyboard_test::resultSet = true;
  }
  void setResult(ActivityResult r) {
    keyboard_test::cancelled = r.isCancelled;
    keyboard_test::timedOut = r.data.timedOut;
    keyboard_test::resultSet = true;
  }
  void finish() {
    keyboard_test::completedUnderLock |= keyboard_test::renderLockHeld;
    keyboard_test::finished = true;
  }
};

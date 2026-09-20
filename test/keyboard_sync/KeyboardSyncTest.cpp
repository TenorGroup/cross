#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "components/UITheme.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using Button = MappedInputManager::Button;
using namespace std::chrono_literals;
namespace fui = freeink::ui;

namespace keyboard_test {
thread_local bool renderLockHeld = false;
bool completedUnderLock = false;
bool cancelled = false;
bool finished = false;
bool resultSet = false;
bool timedOut = false;
unsigned long nowMs = 10000;
std::string completedText;
std::mutex renderMutex;
}

// Model the production nonrecursive render mutex, fail immediately on nesting.
RenderLock::RenderLock() {
  if (keyboard_test::renderLockHeld) throw std::runtime_error("recursive RenderLock acquisition");
  keyboard_test::renderMutex.lock();
  keyboard_test::renderLockHeld = isLocked = true;
}
RenderLock::RenderLock(Activity&) : RenderLock() {}
RenderLock::RenderLock(TryTake) {
  isLocked = keyboard_test::renderMutex.try_lock();
  if (isLocked) keyboard_test::renderLockHeld = true;
}
RenderLock::~RenderLock() { unlock(); }
void RenderLock::unlock() {
  if (!isLocked) return;
  isLocked = keyboard_test::renderLockHeld = false;
  keyboard_test::renderMutex.unlock();
}
bool RenderLock::peek() { return false; }

namespace keyboard_layouts {
uint16_t enabled() { return 1; }
fui::KeyboardLayoutId startingLayout() { return fui::KeyboardLayoutId::QwertyEn; }
fui::KeyboardLayoutId next(fui::KeyboardLayoutId id) { return id; }
}

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct Fixture {
  GfxRenderer renderer;
  MappedInputManager input;
  KeyboardEntryActivity activity;
  explicit Fixture(std::string text, InputType type = InputType::Text, bool backCancels = false,
                   uint32_t idleTimeoutMs = 0, uint32_t startMs = 10000)
      : activity(renderer, input, "Keyboard", std::move(text), 0, type, backCancels, idleTimeoutMs) {
    keyboard_test::completedUnderLock = false;
    keyboard_test::cancelled = false;
    keyboard_test::finished = false;
    keyboard_test::resultSet = false;
    keyboard_test::timedOut = false;
    keyboard_test::completedText.clear();
    keyboard_test::nowMs = startMs;
    activity.onEnter();
  }
  void render() {
    RenderLock lock;
    activity.render(std::move(lock));
  }
  void press(Button button) {
    input.reset();
    input.pressed[static_cast<size_t>(button)] = true;
    input.held[static_cast<size_t>(button)] = true;
    activity.loop();
  }
  void release(Button button) {
    input.reset();
    input.released[static_cast<size_t>(button)] = true;
    activity.loop();
  }
  void tap(Button button) { press(button); release(button); }
  void hold(Button button, unsigned long ms = 600) {
    press(button);
    input.reset();
    input.held[static_cast<size_t>(button)] = true;
    input.heldMs = ms;
    activity.loop();
    release(button);
  }
  std::string completed() {
    activity.saveInputBeforeHome();
    require(!keyboard_test::completedUnderLock, "completion/navigation ran under RenderLock");
    return keyboard_test::completedText;
  }

  // Block the REAL render() after acquiring its lock and before its first read.
  // The mutation must wait until this frame releases the lock. The baseline
  // deterministically fails without performing undefined simultaneous accesses.
  void againstPausedRender(const std::function<void()>& mutation, std::chrono::milliseconds blockedFor = 80ms) {
    std::mutex gate;
    std::condition_variable changed;
    bool framePaused = false, proceed = false;
    renderer.frameStarted = [&] {
      std::unique_lock<std::mutex> lock(gate);
      framePaused = true;
      changed.notify_all();
      changed.wait(lock, [&] { return proceed; });
    };
    auto frame = std::async(std::launch::async, [&] { render(); });
    {
      std::unique_lock<std::mutex> lock(gate);
      require(changed.wait_for(lock, 2s, [&] { return framePaused; }), "render never reached test barrier");
    }
    std::promise<void> attempted;
    auto edit = std::async(std::launch::async, [&] { attempted.set_value(); mutation(); });
    attempted.get_future().wait();
    const bool escaped = edit.wait_for(blockedFor) == std::future_status::ready;
    {
      std::lock_guard<std::mutex> lock(gate);
      proceed = true;
    }
    changed.notify_all();
    frame.get();
    edit.get();
    renderer.frameStarted = {};
    require(!escaped, "input mutated keyboard state while render owned the nonrecursive lock");
  }
};

static void run(const std::string& test) {
  if (test == "enter") {
    Fixture f("aế中", InputType::Password);
    f.againstPausedRender([&] { f.activity.onEnter(); });
    require(f.completed() == "aế中", "onEnter changed initial text");
  } else if (test == "insert") {
    const std::string initial(4096, 'x');
    Fixture f(initial);
    f.press(Button::Confirm);
    f.againstPausedRender([&] { f.release(Button::Confirm); });
    require(f.completed() == initial + "1", "insert/reallocation lost bytes");
  } else if (test == "backspace") {
    Fixture f("aế中");
    f.press(Button::Left);
    f.input.reset(); f.input.held[static_cast<size_t>(Button::Left)] = true; f.input.heldMs = 600;
    f.againstPausedRender([&] { f.activity.loop(); });
    require(f.completed() == "aế", "backspace split UTF-8 or erased wrong code point");
  } else if (test == "clear") {
    Fixture f(std::string(4096, 'x'));
    for (int i = 0; i < 3; ++i) f.tap(Button::Down);
    f.tap(Button::Left);  // Shift column wraps to Delete.
    f.press(Button::Confirm);
    f.input.reset(); f.input.held[static_cast<size_t>(Button::Confirm)] = true; f.input.heldMs = 1600;
    f.againstPausedRender([&] { f.activity.loop(); });
    require(f.completed().empty(), "held Delete did not clear the field");
    f.render();  // Empty text and end cursor must remain a valid render.
  } else if (test == "cursor") {
    Fixture f("aế中");
    f.hold(Button::Up);  // Cursor mode, at end of UTF-8 text.
    f.press(Button::Left);
    f.againstPausedRender([&] { f.release(Button::Left); });
    f.hold(Button::Right);  // Insert at the cursor after the multibyte move.
    require(f.completed() == "aế 中", "cursor moved inside UTF-8 or to wrong character");
  } else if (test == "password") {
    Fixture f("aế中", InputType::Password);
    f.hold(Button::Up);
    f.hold(Button::Right);  // Select the reveal control.
    f.press(Button::Confirm);
    f.againstPausedRender([&] { f.release(Button::Confirm); });
    f.render();
    bool visible = false;
    for (const auto& s : f.renderer.drawnText) visible |= s == "aế中";
    require(visible, "password visibility changed without matching rendered text");
    require(f.completed() == "aế中", "password toggle changed input bytes");
  } else if (test == "password_utf8") {
    Fixture f("aế中", InputType::Password);
    f.render();
    require(!f.renderer.invalidUtf8Seen, "masked password sent a partial UTF-8 character to the renderer");
    bool revealedWholeCharacter = false;
    for (const auto& s : f.renderer.drawnText) revealedWholeCharacter |= s == "****中";
    require(revealedWholeCharacter, "masked password did not reveal the whole preceding code point");
    f.hold(Button::Up);
    f.tap(Button::Left);
    f.render();
    require(!f.renderer.invalidUtf8Seen, "masked cursor rendering split a UTF-8 character");
    require(f.completed() == "aế中", "masking changed actual password bytes");
  } else if (test == "utf8_wrap") {
    std::string mixed, originalRedTrigger;
    for (int i = 0; i < 40; ++i) { mixed += "aéế中😀"; originalRedTrigger += "ế中"; }
    for (const auto& text : {originalRedTrigger, mixed, std::string(""), std::string("é"),
                             std::string("ế"), std::string("😀")}) {
      for (int advance : {8, 240}) {
        Fixture f(text);
        f.renderer.advancePerByte = advance;
        f.render();
        require(!f.renderer.invalidUtf8Seen, "line-wrap measurement passed a partial UTF-8 prefix to the renderer");
        std::string rendered;
        for (const auto& s : f.renderer.drawnText) {
          rendered += s;
          size_t codepoints = 0;
          for (unsigned char c : s) codepoints += (c & 0xc0) != 0x80;
          // The test theme gives a 476px field. Oversized glyphs must occupy
          // a complete line by themselves and let the remaining text advance.
          require(s.size() * advance <= 476 || codepoints == 1, "wrapped line exceeded the field with multiple glyphs");
        }
        require(rendered == text, "UTF-8 wrapping changed or dropped text");
      }
    }
  } else if (test == "input_time_short" || test == "input_time_long") {
    for (unsigned long capturedMs : test == "input_time_short" ? std::initializer_list<unsigned long>{0, 40} :
                                                                std::initializer_list<unsigned long>{600}) {
      Fixture f("aế中");
      f.hold(Button::Up);  // Same cursor-mode path as the X3 failure.
      f.input.reset();
      f.input.pressed[static_cast<size_t>(Button::Left)] = true;
      f.input.held[static_cast<size_t>(Button::Left)] = true;
      f.input.heldMs = capturedMs;
      f.againstPausedRender([&] {
        f.input.liveHeldClock = true;
        f.input.heldClockStart = std::chrono::steady_clock::now();
        f.activity.loop();
      }, 600ms);
      // The next main update observes the physical release that happened while
      // the previous input frame waited for the renderer.
      f.release(Button::Left);
      require(f.completed() == (capturedMs < 500 ? "aế中" : "aế"),
              "render wait changed the sampled tap/hold action");
      if (capturedMs < 500) {
        f.hold(Button::Right);
        require(f.completed() == "aế 中", "short left release failed to move the UTF-8 cursor exactly once");
      }
    }
  } else if (test == "touch") {
    Fixture f("aế中");
    f.render();  // Publish actual FreeInkUI key regions.
    f.input.reset(); f.input.tap = true; f.input.touchX = 40; f.input.touchY = 588;
    f.againstPausedRender([&] { f.activity.loop(); });
    require(f.completed() != "aế中", "touch did not route a real keyboard key");
  } else if (test == "completion") {
    for (int mode = 0; mode < 3; ++mode) {
      keyboard_test::completedText.clear();
      Fixture f("aế中");
      if (mode == 0) f.tap(Button::Back);
      if (mode == 1) {
        for (int i = 0; i < 4; ++i) f.tap(Button::Down);
        f.tap(Button::Left);  // Last key in bottom row: OK.
        f.tap(Button::Confirm);
      }
      if (mode == 2) f.activity.saveInputBeforeHome();
      require(keyboard_test::completedText == "aế中", "completion dropped edited text");
      require(!keyboard_test::completedUnderLock, "Back/OK/Home completed while holding RenderLock");
    }
  } else if (test == "viewport") {
    for (uint8_t tier : {1, 2}) {
      SETTINGS.uiTextSize = tier;
      const std::string initial = "BEGIN" + std::string(1000, 'x') + "END";
      Fixture f(initial);
      f.renderer.uiTier = tier;
      f.render();
      bool endVisible = false;
      for (const auto& run : f.renderer.runs) {
        require(run.y == 5 + UITheme::getInstance().getMetrics().headerHeight + 16,
                "enlarged field escaped its one-line viewport");
        endVisible |= run.text.find("END") != std::string::npos;
      }
      require(endVisible, "cursor end is outside viewport");
      f.hold(Button::Up);
      for (size_t i = 0; i < initial.size(); ++i) f.tap(Button::Left);
      f.render();
      bool startVisible = false;
      for (const auto& run : f.renderer.runs) startVisible |= run.text.find("BEGIN") != std::string::npos;
      require(startVisible, "moving cursor to start did not scroll field viewport");
      require(f.completed() == initial, "viewport changed stored text");
      require(!f.renderer.invalidUtf8Seen, "viewport split UTF-8");
    }
    SETTINGS.uiTextSize = 0;
  } else if (test == "cancel") {
    for (int mode = 0; mode < 3; ++mode) {
      keyboard_test::cancelled = false;
      keyboard_test::completedText.clear();
      Fixture f("secret", InputType::Password, true);
      if (mode == 0) f.tap(Button::Back);
      if (mode == 1) {
        for (int i = 0; i < 4; ++i) f.tap(Button::Down);
        f.tap(Button::Left);
        f.tap(Button::Confirm);
      }
      if (mode == 2) f.activity.saveInputBeforeHome();
      require(keyboard_test::cancelled == (mode != 1), "Wi-Fi Back/Home must cancel, OK must submit");
      require(mode == 1 ? keyboard_test::completedText == "secret" : keyboard_test::completedText.empty(),
              "Wi-Fi cancellation submitted password");
      require(!keyboard_test::timedOut, "normal keyboard completion was marked as timed out");
      require(!keyboard_test::completedUnderLock, "cancel callback ran under RenderLock");
    }
  } else if (test == "timeout_disabled") {
    Fixture f("query", InputType::Text, true);
    keyboard_test::nowMs = 10000u + 24u * 60u * 60u * 1000u;
    f.input.reset();
    f.activity.loop();
    require(!keyboard_test::finished && !keyboard_test::resultSet,
            "default keyboard timeout must remain disabled");
  } else if (test == "timeout_wrap") {
    constexpr uint32_t timeoutMs = 5u * 60u * 1000u;
    constexpr uint32_t startMs = UINT32_MAX - 1000u;
    Fixture f("query", InputType::Text, true, timeoutMs, startMs);
    keyboard_test::nowMs = static_cast<uint32_t>(startMs + timeoutMs - 1u);
    f.input.reset();
    f.activity.loop();
    require(!keyboard_test::finished, "opt-in keyboard timeout expired before wrapped deadline");
    keyboard_test::nowMs = static_cast<uint32_t>(startMs + timeoutMs);
    f.activity.loop();
    require(keyboard_test::finished && keyboard_test::resultSet && keyboard_test::cancelled,
            "opt-in keyboard did not cancel at wrapped deadline");
    require(keyboard_test::timedOut, "timeout result did not distinguish expiry from Back");
    require(!keyboard_test::completedUnderLock, "timeout completed while holding RenderLock");
  } else if (test == "timeout_input_reset") {
    constexpr uint32_t timeoutMs = 5u * 60u * 1000u;
    Fixture f("query", InputType::Text, true, timeoutMs, 1000u);
    keyboard_test::nowMs = 300000u;
    f.press(Button::Left);
    f.input.reset();
    keyboard_test::nowMs = 599999u;
    f.activity.loop();
    require(!keyboard_test::finished, "keyboard expired before reset deadline");
    keyboard_test::nowMs = 600000u;
    f.activity.loop();
    require(keyboard_test::finished && keyboard_test::cancelled && keyboard_test::timedOut,
            "actual keyboard input did not reset the timeout deadline");
  } else if (test == "timeout_normal_cancel") {
    constexpr uint32_t timeoutMs = 5u * 60u * 1000u;
    Fixture f("query", InputType::Text, true, timeoutMs, 1000u);
    keyboard_test::nowMs = 2000u;
    f.tap(Button::Back);
    require(keyboard_test::finished && keyboard_test::resultSet && keyboard_test::cancelled,
            "Back did not cancel an opt-in keyboard");
    require(!keyboard_test::timedOut, "Back cancellation was reported as an idle timeout");
  } else if (test == "stress") {
    Fixture f("aế中", InputType::Password);
    std::atomic<bool> stop{false};
    std::atomic<unsigned> frames{0};
    auto painter = std::async(std::launch::async, [&] {
      while (!stop.load()) { f.render(); ++frames; std::this_thread::yield(); }
    });
    for (int i = 0; i < 2000; ++i) {
      f.tap(Button::Confirm);
      f.hold(Button::Left);
    }
    stop = true;
    painter.get();
    require(f.completed() == "aế中", "concurrent edit/render stress changed UTF-8 payload");
    require(frames > 0, "stress did not execute any frame");
    std::cout << "frames=" << frames << " edits=4000\n";
  } else {
    throw std::runtime_error("unknown case");
  }
}

int main(int argc, char** argv) {
  try {
    require(argc == 2, "usage: KeyboardSyncTest CASE");
    run(argv[1]);
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}

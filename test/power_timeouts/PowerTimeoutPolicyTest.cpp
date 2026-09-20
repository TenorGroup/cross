#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

@SERVER_POLICY@

@CALIBRE_POLICY@

@FONT_POLICY@

@OPDS_POLICY@

@OTA_POLICY@

@CLOCK_POLICY@

@KOREADER_POLICY@

@KEYBOARD_POLICY@

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

template <typename Tick>
void exerciseRadioIdleDeadline(Tick tick, const char* prefix) {
  uint32_t since = 77u;
  bool started = true;
  require(!tick(100u, false, true, false, since, started), prefix);
  require(!started, "busy phase did not disarm idle timer");

  require(!tick(200u, true, false, false, since, started), "radio-off wait armed idle timer");
  require(!started, "radio-off wait left timer armed");

  constexpr uint32_t nearWrap = UINT32_MAX - 1000u;
  require(!tick(nearWrap, true, true, false, since, started), "wait phase expired while arming");
  require(started && since == nearWrap, "wait phase did not arm at current time");
  require(!tick(nearWrap + 299999u, true, true, false, since, started), "wait phase expired before deadline");
  require(tick(nearWrap + 300000u, true, true, false, since, started), "wraparound wait missed deadline");

  require(!tick(0u, false, true, false, since, started), "busy phase requested exit after wrap test");
  require(!started, "busy phase did not disarm after wrap test");
  require(!tick(1000u, true, true, false, since, started), "wait phase expired while rearming");
  require(!tick(2000u, true, true, true, since, started), "interaction did not reset deadline");
  require(since == 2000u, "interaction reset to the wrong time");
  require(!tick(301999u, true, true, false, since, started), "reset wait phase expired before deadline");
  require(tick(302000u, true, true, false, since, started), "idle wait phase missed deadline");
}

void exerciseKeyboardIdleDeadline() {
  uint32_t since = 1000u;
  require(!keyboard_power::idleTimeoutDue(1000u + 24u * 60u * 60u * 1000u, 0u, false, since),
          "default keyboard timeout was enabled");
  require(since == 1000u, "disabled keyboard timeout changed its deadline");

  constexpr uint32_t timeoutMs = 5u * 60u * 1000u;
  constexpr uint32_t nearWrap = UINT32_MAX - 1000u;
  since = nearWrap;
  require(!keyboard_power::idleTimeoutDue(nearWrap + timeoutMs - 1u, timeoutMs, false, since),
          "keyboard expired before wrapped deadline");
  require(keyboard_power::idleTimeoutDue(nearWrap + timeoutMs, timeoutMs, false, since),
          "keyboard missed wrapped deadline");

  since = 1000u;
  require(!keyboard_power::idleTimeoutDue(2000u, timeoutMs, true, since),
          "keyboard input requested timeout");
  require(since == 2000u, "keyboard input reset to the wrong time");
  require(!keyboard_power::idleTimeoutDue(301999u, timeoutMs, false, since),
          "keyboard expired before reset deadline");
  require(keyboard_power::idleTimeoutDue(302000u, timeoutMs, false, since),
          "keyboard missed reset deadline");
}
}  // namespace

int main() {
  using power_timeout::SESSION_IDLE_TIMEOUT_MS;
  using power_timeout::WebSessionLifecycle;

  require(SESSION_IDLE_TIMEOUT_MS == 5u * 60u * 1000u, "session timeout must be five minutes");

  WebSessionLifecycle session;
  session.start(100u);
  require(session.running(), "start must activate the session");
  require(session.lastActivityTime() == 100u, "start must establish the activity baseline");
  require(!session.idleExpired(100u + SESSION_IDLE_TIMEOUT_MS - 1u), "session expired before deadline");

  session.noteHttpRequest(true, true, 200u);
  require(session.lastActivityTime() == 100u, "status polling extended the session");
  session.noteHttpRequest(false, false, 300u);
  require(session.lastActivityTime() == 100u, "unauthorized HTTP extended the session");
  session.noteWebSocketPing(400u);
  require(session.lastActivityTime() == 100u, "WebSocket ping extended the session");
  session.noteHttpRequest(true, false, 500u);
  require(session.lastActivityTime() == 500u, "meaningful HTTP did not extend the session");

  session.noteTransferBytes(0u, 700u);
  require(session.lastActivityTime() == 500u, "empty transfer callback extended the session");
  session.noteTransferBytes(4096u, 800u);
  require(session.lastActivityTime() == 800u, "transfer bytes did not extend the session");
  require(!session.idleExpired(800u + SESSION_IDLE_TIMEOUT_MS - 1u), "active transfer expired early");
  session.noteTransferBytes(4096u, 800u + SESSION_IDLE_TIMEOUT_MS - 1u);
  require(!session.idleExpired(800u + 2u * SESSION_IDLE_TIMEOUT_MS - 2u),
          "progressing transfer did not retain its fresh deadline");
  require(session.idleExpired(800u + 2u * SESSION_IDLE_TIMEOUT_MS - 1u),
          "stalled transfer remained active forever");

  constexpr uint32_t nearWrap = UINT32_MAX - 1000u;
  session.start(nearWrap);
  require(!session.idleExpired(nearWrap + SESSION_IDLE_TIMEOUT_MS - 1u), "wraparound session expired early");
  require(session.idleExpired(nearWrap + SESSION_IDLE_TIMEOUT_MS), "wraparound session missed deadline");

  session.stop();
  require(!session.running(), "stop left the session active");
  require(!session.idleExpired(nearWrap + 2u * SESSION_IDLE_TIMEOUT_MS), "stopped session still expired");

  std::vector<std::string> shutdownOrder;
  calibre_power::stopBeforeFinish([&] { shutdownOrder.emplace_back("stop"); },
                                  [&] { shutdownOrder.emplace_back("finish"); });
  require(shutdownOrder == std::vector<std::string>({"stop", "finish"}),
          "Calibre finished before stopping its server");

  require(font_power::preventsAutoSleep(font_power::Phase::LoadingManifest), "manifest load allowed sleep");
  require(font_power::preventsAutoSleep(font_power::Phase::Downloading), "font download allowed sleep");
  require(!font_power::preventsAutoSleep(font_power::Phase::Complete), "font COMPLETE blocked sleep");
  require(!font_power::preventsAutoSleep(font_power::Phase::Error), "font ERROR blocked sleep");

  exerciseRadioIdleDeadline(opds_power::idleExitDue, "OPDS busy phase requested exit");
  exerciseRadioIdleDeadline(ota_power::idleExitDue, "OTA busy phase requested exit");
  exerciseRadioIdleDeadline(clock_sync_power::idleExitDue, "Clock busy phase requested exit");
  exerciseRadioIdleDeadline(koreader_sync_power::idleExitDue, "KOReader busy phase requested exit");
  exerciseKeyboardIdleDeadline();

  std::cout << "power timeout lifecycle cases passed\n";
  return 0;
}

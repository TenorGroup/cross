#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Idle work ("Việc lúc rảnh"): a job the device does while nobody uses it, in steps a key can cut.
// v1 runs at Home only, standing still or on USB, with the CPU held at full speed for a step.
// Pure rules, a fixed queue and the hour's budget: the host passes in the clock, the keys and the
// place the budget lives, so the tests run them with pretend ones.
namespace idlework {

enum class Where : uint8_t { Home, Elsewhere };
enum class Why : uint8_t { Ok, NotHome, TooSoon, BatteryLow, HourCap };

// Home's idle wait before a missing card cover is decoded (HomeActivity CARD_THUMB_IDLE_MS).
inline constexpr uint32_t kQuietMs = 3000;
inline constexpr int kMinBatteryPercent = 30;
// On battery, an hour holds this much step time. A step is charged what it ran (a key that cuts
// it short leaves the rest of the hour), and at least kMinChargeMs.
inline constexpr uint64_t kHourUs = 3600ull * 1000 * 1000;
inline constexpr uint32_t kHourBudgetMs = 60u * 1000u;
inline constexpr uint32_t kMinChargeMs = 250;
// Steps an hour, on the cable too: a step that keeps ending the same way (no heap) stays rare.
inline constexpr uint16_t kHourMaxSteps = 120;
// A step stops after this long whatever it is doing: the loop and the sleep timer wait for it.
inline constexpr uint32_t kStepCapMs = 6000;

struct IdleInputs {
  Where where;
  uint32_t quietMs;  // since the last key or frame
  bool usb;
  int batteryPercent;
  uint32_t hourUsedMs;
  uint16_t hourSteps;
};

// The ONE answer to "may an idle step run now".
constexpr Why verdict(const IdleInputs& in) {
  if (in.where != Where::Home) return Why::NotHome;
  if (in.quietMs < kQuietMs) return Why::TooSoon;
  if (in.hourSteps >= kHourMaxSteps) return Why::HourCap;
  if (in.usb) return Why::Ok;  // on the cable: neither the battery nor the hour applies
  if (in.batteryPercent < kMinBatteryPercent) return Why::BatteryLow;
  if (in.hourUsedMs >= kHourBudgetMs) return Why::HourCap;
  return Why::Ok;
}

// The hour's budget. The caller keeps it where it outlives deep sleep (RTC memory) and reads the
// clock that runs through sleep (the RTC clock): a wake is a restart, so RAM alone would forget it.
struct Budget {
  static constexpr uint32_t kMagic = 0x49444C45;  // "IDLE"
  uint32_t magic;
  uint32_t usedMs;
  uint64_t windowUs;
  uint32_t steps;
  constexpr bool live(const uint64_t nowUs) const {
    return magic == kMagic && nowUs >= windowUs && nowUs - windowUs < kHourUs;
  }
  constexpr uint32_t used(const uint64_t nowUs) const { return live(nowUs) ? usedMs : 0; }
  constexpr uint16_t stepsIn(const uint64_t nowUs) const {
    return live(nowUs) ? static_cast<uint16_t>(steps < 0xFFFF ? steps : 0xFFFF) : 0;
  }
  constexpr void charge(const uint64_t nowUs, const uint32_t ranMs) {
    if (!live(nowUs)) {
      magic = kMagic;
      usedMs = 0;
      windowUs = nowUs;
      steps = 0;
    }
    ++steps;
    const uint32_t ms = ranMs < kMinChargeMs ? kMinChargeMs : ranMs;
    usedMs = usedMs + ms < usedMs ? UINT32_MAX : usedMs + ms;
  }
};

// Asked by a step between pieces of its work: a key is down, or the step ran its time.
struct Stop {
  bool (*keyDown)(void* user) = nullptr;
  void* user = nullptr;
  uint32_t (*clockMs)() = nullptr;
  uint32_t startedMs = 0;
  uint32_t capMs = kStepCapMs;
  mutable bool timedOut = false;
  bool operator()() const {
    if (clockMs && clockMs() - startedMs >= capMs) timedOut = true;
    return timedOut || (keyDown && keyDown(user));
  }
};

enum class Step : uint8_t {
  Done,     // finished, or nothing to do: leaves the queue
  Stopped,  // cut short: stays at the front for the next idle moment
};

struct Job {
  uint32_t key = 0;
  Step (*run)(const Job&, const Stop&) = nullptr;
  void* owner = nullptr;
  int16_t arg[4]{};
};

// A fixed queue, no allocation. One job per key; a key whose job finished is not queued again
// until clear(true) (Home calls it when it comes back to the front, onResume; a new Home starts
// with an empty queue), so a done job costs nothing on repaints.
template <size_t N = 4, size_t Settled = 8>
class Queue {
 public:
  bool push(const Job& job) {
    for (size_t i = 0; i < settledCount; ++i)
      if (settled[i] == job.key) return false;
    for (size_t i = 0; i < count; ++i)
      if (slots[i].key == job.key) return false;
    if (count == N) return false;
    slots[count++] = job;
    return true;
  }
  bool empty() const { return count == 0; }
  size_t size() const { return count; }
  const Job& front() const { return slots[0]; }
  // Runs the front job once; a job that finished leaves the queue.
  Step runFront(const Stop& stop) {
    if (!count) return Step::Done;
    const Job job = slots[0];
    const Step step = job.run(job, stop);
    if (step == Step::Done) {
      settled[settledNext++ % Settled] = job.key;
      if (settledCount < Settled) ++settledCount;
      for (size_t i = 1; i < count; ++i) slots[i - 1] = slots[i];
      --count;
    }
    return step;
  }
  void clear(const bool forget = false) {
    count = 0;
    if (forget) settledCount = settledNext = 0;
  }

 private:
  std::array<Job, N> slots{};
  size_t count = 0;
  std::array<uint32_t, Settled> settled{};
  size_t settledCount = 0, settledNext = 0;
};

}  // namespace idlework

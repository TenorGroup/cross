// Idle work: the verdict as a table with a pretend clock, the hour's budget across a pretend
// sleep, the step's stop, and the fixed queue.
#include <IdleWork.h>

#include <cassert>
#include <cstdio>

using namespace idlework;

namespace {
uint32_t now = 0;
uint32_t clock() { return now; }
int ran = 0;
Step result = Step::Done;
Step job(const Job&, const Stop&) {
  ++ran;
  return result;
}
Job make(uint32_t key) {
  Job j;
  j.key = key;
  j.run = job;
  return j;
}
}  // namespace

int main() {
  // The verdict: Home, quiet 3 s, then the cable, then the battery and the hour.
  struct Case { IdleInputs in; Why want; } cases[] = {
      {{Where::Elsewhere, 60000, true, 100, 0, 0}, Why::NotHome},
      {{Where::Home, 2999, true, 100, 0, 0}, Why::TooSoon},
      {{Where::Home, 3000, false, 29, 0, 0}, Why::BatteryLow},
      {{Where::Home, 3000, true, 5, kHourBudgetMs, kHourMaxSteps - 1}, Why::Ok},
      {{Where::Home, 3000, true, 100, 0, kHourMaxSteps}, Why::HourCap},  // steps are capped on the cable too
      {{Where::Home, 3000, false, 30, kHourBudgetMs - 1, 0}, Why::Ok},
      {{Where::Home, 3000, false, 80, kHourBudgetMs, 0}, Why::HourCap},
  };
  for (const auto& c : cases) assert(verdict(c.in) == c.want);
  std::puts("GREEN verdict: Home only, 3 s of quiet, USB lifts battery and hour's time but not its 120 steps");

  // The budget lives in memory the caller keeps across sleep, read with a clock that runs through it.
  Budget b{};  // never written (RTC memory after a power loss): nothing used
  assert(b.used(5) == 0);
  b.charge(1000, 100);  // a step a key cut after 100 ms costs the floor
  assert(b.used(2000) == kMinChargeMs);
  b.charge(2000, 4000);
  assert(b.used(3000) == kMinChargeMs + 4000);
  const uint64_t wake = 20ull * 60 * 1000 * 1000;  // asleep 20 minutes, the RTC clock went on
  assert(b.used(wake) == kMinChargeMs + 4000);
  assert(b.used(1000 + kHourUs) == 0);  // the hour passed
  b.charge(wake, 1);
  assert(b.used(10) == 0);  // a clock behind the window (a reset): not trusted
  Budget full{};
  for (int i = 0; i < 300; ++i) full.charge(1000, 1);  // a key at every start: 300 starts
  assert(full.used(1000) == 300 * kMinChargeMs && full.used(1000) > kHourBudgetMs);
  assert(full.stepsIn(1000) == 300 && full.stepsIn(1000 + kHourUs) == 0 && Budget{}.stepsIn(5) == 0);
  std::puts("GREEN budget: what a step ran, at least 250 ms, kept across sleep, reset after an hour or a clock reset");

  // The stop: a key, or the step's own time; the time cap says which.
  Stop s;
  s.clockMs = clock;
  s.startedMs = now = 100;
  bool key = false;
  s.keyDown = [](void* u) { return *static_cast<bool*>(u); };
  s.user = &key;
  assert(!s());
  key = true;
  assert(s() && !s.timedOut);
  key = false;
  now = 100 + kStepCapMs;
  assert(s() && s.timedOut);
  std::puts("GREEN stop: a key, or the step cap; timedOut tells them apart");

  // The queue: 4 places, one per key, a stopped job stays first, a done one is not queued again.
  Queue<> q;
  for (uint32_t k = 1; k <= 5; ++k) q.push(make(k));
  assert(q.size() == 4 && !q.push(make(2)));
  result = Step::Stopped;
  assert(q.runFront(s) == Step::Stopped && q.size() == 4 && q.front().key == 1);
  result = Step::Done;
  assert(q.runFront(s) == Step::Done && q.size() == 3 && q.front().key == 2);
  assert(!q.push(make(1)));  // done this visit
  q.clear();
  assert(q.empty() && !q.push(make(1)) && q.push(make(2)));
  q.clear(true);
  assert(q.push(make(1)) && ran == 2);
  std::puts("GREEN queue: 4 places, no duplicate, stopped stays first, done not queued again until a new visit");
  return 0;
}

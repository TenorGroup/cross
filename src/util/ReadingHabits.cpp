#include "ReadingHabits.h"

#include <algorithm>
#include <limits>

#include "NgayGio.h"
namespace habits {
namespace {
void add(uint32_t& target, uint32_t value) { target += std::min(value, UINT32_MAX - target); }
void inc(uint16_t& target) {
  if (target < UINT16_MAX) ++target;
}
bool weekend(uint32_t day) { return day && ((day - 1) % 7 < 2); }  // 2000-01-01: Saturday.
}  // namespace
uint32_t ordinal(uint16_t year, uint8_t month, uint8_t day) {
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || !day || day > ngaygio::soNgayTrongThang(year, month))
    return 0;
  uint32_t result = day;
  for (uint16_t y = 2000; y < year; ++y) result += ngaygio::laNamNhuan(y) ? 366 : 365;
  for (uint8_t m = 1; m < month; ++m) result += ngaygio::soNgayTrongThang(year, m);
  return result;
}
uint32_t dateKey(uint32_t day) {
  if (!day || day > 36525) return 0;
  uint16_t year = 2000;
  uint8_t month = 1;
  while (day > static_cast<uint32_t>(ngaygio::laNamNhuan(year) ? 366 : 365))
    day -= ngaygio::laNamNhuan(year++) ? 366 : 365;
  while (day > ngaygio::soNgayTrongThang(year, month)) day -= ngaygio::soNgayTrongThang(year, month++);
  return year * 10000u + month * 100u + day;
}
Day* Ledger::dayFor(uint32_t day) {
  if (!day) return nullptr;
  uint32_t latest = 0;
  for (auto& d : days) {
    if (d.day == day) return &d;
    latest = std::max(latest, d.day);
  }
  if (latest >= day + DAYS) return nullptr;
  auto* result = &*std::min_element(days.begin(), days.end(), [](const Day& a, const Day& b) { return a.day < b.day; });
  *result = {};
  result->day = day;
  return result;
}
void Ledger::finish() {
  if (session.visibleMs >= 30000) {
    if (auto* d = dayFor(session.last.day)) {
      inc(d->sessions);
      const bool useful =
          !session.uncertain && session.turns >= 2 &&
          static_cast<uint64_t>(session.activeMs) * 100 >= static_cast<uint64_t>(session.visibleMs) * 80;
      if (!useful)
        inc(d->uncertain);
      else if (session.visibleMs >= 120000 && session.visibleMs <= 600000)
        inc(d->shortSessions);
      if (useful && session.activeMs >= 1500000) inc(d->longSessions);
    }
  }
  session = {};
  hasMonotonic = false;
}
void Ledger::settle(Stamp now) {
  if (!session.visibleMs) return;
  if (restored && !session.clean) {
    session.uncertain = true;
    finish();
    restored = false;
    return;
  }
  if (!now.day || !session.last.day || now.offset != session.last.offset || now.utcMinute < session.last.utcMinute) {
    session.uncertain = true;
    finish();
    return;
  }
  if (now.utcMinute - session.last.utcMinute > 5) finish();
}
void Ledger::observe(uint32_t elapsed, uint16_t turns, Stamp stamp, uint32_t now) {
  if (stamp.day) {
    if (!clockObserved || stamp.utcMinute != clockMinute) {
      clockObserved = true;
      clockMinute = stamp.utcMinute;
      clockMonotonic = now;
    } else if (now - clockMonotonic > 90000)
      stamp = {};
  }
  if (restored) {
    restored = false;
    if (!session.clean) {
      session.uncertain = true;
      finish();
    } else
      settle(stamp);
  }
  if (hasMonotonic && now - lastMonotonic > 300000u) finish();
  if (session.visibleMs && stamp.day && session.last.day) {
    if (stamp.utcMinute < session.last.utcMinute || stamp.offset != session.last.offset) {
      session.uncertain = true;
      qualityGapDay = stamp.day;
      finish();
    } else if (stamp.utcMinute > session.last.utcMinute + 5)
      finish();
  }
  if (!stamp.day)
    clockLost = true;
  else if (clockLost) {
    qualityGapDay = stamp.day;
    clockLost = false;
  }
  if (!elapsed && !turns) return;
  if (stamp.day && (!firstDay || stamp.day < firstDay)) firstDay = stamp.day;
  session.clean = false;
  // Long scheduler stalls cannot be attributed to one clock bucket reliably.
  if (elapsed > 5000) session.uncertain = true;
  const uint32_t credit = std::min<uint32_t>(elapsed, 300000u - std::min<uint32_t>(session.idleMs, 300000u));
  add(session.visibleMs, elapsed);
  add(session.activeMs, credit);
  add(session.idleMs, elapsed);
  add(session.turns, turns);
  if (turns) session.idleMs = 0;
  if (!stamp.day) session.uncertain = true;
  if (auto* d = dayFor(stamp.day)) {
    add(d->activeMs, credit);
    if (stamp.minute >= 23 * 60 || stamp.minute < 5 * 60) {
      add(d->nightMs, credit);
      if (credit)
        if (auto* night = dayFor(stamp.minute < 5 * 60 ? stamp.day - 1 : stamp.day)) night->night = true;
    }
    if (stamp.minute >= 5 * 60 && stamp.minute < 9 * 60) add(d->earlyMs, credit);
  }
  session.last = stamp;
  lastMonotonic = now;
  hasMonotonic = true;
}
Summary Ledger::summarize(uint32_t today) const {
  Summary s;
  if (!today || !firstDay || today < firstDay) return s;
  const bool provisional = today - firstDay < 2;
  s.coverage = std::min<uint32_t>(28, today - firstDay + (provisional ? 1 : 0));
  uint32_t weekendBits = 0;
  for (uint32_t i = 0; i < s.coverage; ++i) {
    const uint32_t offset = i + (provisional ? 0 : 1);
    const auto date = today - offset;
    if (weekend(date))
      ++s.weekendCount;
    else
      ++s.weekdayCount;
    for (const auto& d : days)
      if (d.day == date) {
        add(s.activeMs, d.activeMs);
        add(s.nightMs, d.nightMs);
        add(s.earlyMs, d.earlyMs);
        s.sessions += d.sessions;
        s.shortSessions += d.shortSessions;
        s.longSessions += d.longSessions;
        s.uncertain += d.uncertain;
        if (d.activeMs >= 120000) {
          ++s.days;
          if (offset <= 14) ++s.recentDays;
        }
        s.nights += d.night;
        s.mornings += d.earlyMs >= 120000;
        s.shortDays += d.shortSessions >= 2;
        s.longDays += d.longSessions > 0;
        if (weekend(date)) {
          add(s.weekendMs, d.activeMs);
          if (d.activeMs >= 120000) weekendBits |= 1u << ((date - 1) / 7 - (today - s.coverage - 1) / 7);
        } else
          add(s.weekdayMs, d.activeMs);
      }
  }
  while (weekendBits) {
    s.weekends += weekendBits & 1;
    weekendBits >>= 1;
  }
  // The denominator includes short and low-signal sessions. Never select only the flattering samples.
  const bool clockSafe = !clockLost && (!qualityGapDay || today > qualityGapDay + 7);
  const bool enough = clockSafe && s.coverage >= 1 && s.days >= 1 && s.sessions >= 1 && s.activeMs >= 600000 &&
                      static_cast<uint32_t>(s.uncertain) * 5 <= s.sessions;
  if (enough) {
    if (s.nights >= std::min<unsigned>(4, s.days) &&
        static_cast<uint64_t>(s.nightMs) * 100 >= static_cast<uint64_t>(s.activeMs) * 60)
      s.eligible |= 1 << NIGHT;
    if (s.shortDays >= 1 && s.shortSessions >= 3 &&
        static_cast<uint32_t>(s.shortSessions) * 100 >= static_cast<uint32_t>(s.sessions) * 70)
      s.eligible |= 1 << SHORT;
    if (s.mornings >= std::min<unsigned>(4, s.days) &&
        static_cast<uint64_t>(s.earlyMs) * 100 >= static_cast<uint64_t>(s.activeMs) * 60)
      s.eligible |= 1 << EARLY;
    if (s.longDays >= 1 && static_cast<uint32_t>(s.longSessions) * 2 >= s.sessions) s.eligible |= 1 << LONG;
  }
  // Repeated reading dates remain useful even when some session lengths are uncertain.
  // Require at least one reliable session so two idle book openings earn no suggestion.
  if (clockSafe && s.coverage >= 2 && s.recentDays >= std::min<unsigned>(10, s.coverage) && s.activeMs >= 600000 &&
      s.sessions > s.uncertain)
    s.eligible |= 1 << REGULAR;
  // Weekend-only readers have their own gate rather than the five-active-day gate.
  if (clockSafe && s.coverage == 28 && s.weekends >= 3 && s.sessions >= 6 && s.weekendMs >= 3600000 &&
      static_cast<uint32_t>(s.uncertain) * 5 <= s.sessions && s.weekendCount && s.weekdayCount &&
      static_cast<uint64_t>(s.weekendMs) * s.weekdayCount >= static_cast<uint64_t>(s.weekdayMs) * s.weekendCount * 2)
    s.eligible |= 1 << WEEKEND;
  return s;
}
void Ledger::evaluate(uint32_t today, bool refresh) {
  if (!today || today < lastEvaluated || (!refresh && today == lastEvaluated)) return;
  const Summary s = summarize(today);
  // Initial suggestions include today, then use the completed-day window as history grows.
  // Keep the persisted counters cleared for compatibility with older stores.
  awarded = s.eligible;
  enter.fill(0);
  leave.fill(0);
  lastEvaluated = today;
}
}  // namespace habits

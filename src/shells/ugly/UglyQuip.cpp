#include "UglyQuip.h"

#include <I18n.h>
#include <InflateStream.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "UglyLogic.h"
#include "UglyQuips.h"
#include "activities/settings/SettingsActivity.h"

namespace ugly {
namespace {
uint8_t turns[16];  // how many times each event has spoken since boot: the lines take turns
uint16_t noted = 0;
bool hasNoted = false;

// Line `index` of the block of the language in use (the Chinese interface reads the English one). The block
// is inflated only up to the end of the group of lines holding it, into a buffer freed on return.
std::string lineAt(const int index) {
  const bool vi = I18N.getLanguage() == Language::VI;
  const uint8_t* block = vi ? quips::VI : quips::EN;
  const size_t size = vi ? sizeof(quips::VI) : sizeof(quips::EN);
  const uint16_t* at = vi ? quips::VI_AT : quips::EN_AT;
  const int mark = index / quips::LINES_PER_MARK;
  const size_t need = at[mark + 1];
  auto raw = makeUniqueNoThrow<uint8_t[]>(need);
  InflateStream in;
  if (!raw || !in.init(false)) return {};
  in.setZlibWrapped();
  in.setSource(block, size);
  size_t produced = 0;
  if (in.readAtMost(raw.get(), need, &produced) == InflateStream::Status::Error || produced < need) return {};
  const char* p = reinterpret_cast<const char*>(raw.get()) + at[mark];
  for (int k = mark * quips::LINES_PER_MARK; k < index; ++k) p += strlen(p) + 1;
  return p;
}
}  // namespace

std::string quip(const Quip event, const uint16_t key, const uint8_t when, const int number, const char* name) {
  const int e = static_cast<int>(event);
  const int slot = logic::quipSlot(quips::SLOTS, sizeof(quips::SLOTS) / sizeof(quips::SLOTS[0]), e, key, when);
  if (slot < 0) return {};
  int first = 0;  // the slots follow the lines: this one's first line comes after the lines of those before it
  for (int i = 0; i < slot; ++i) first += quips::SLOTS[i].count;
  const uint32_t today = ReadingStatsStore::currentDay();
  const std::string line = lineAt(first + logic::quipTurn(today ? logic::civilDays(today) : 0, turns[e]++, quips::SLOTS[slot].count));
  if (line.find('%') == std::string::npos) return line;
  char out[192];
  if (line.find("%s") != std::string::npos)
    snprintf(out, sizeof(out), line.c_str(), name ? name : "");
  else
    snprintf(out, sizeof(out), line.c_str(), number);
  std::string said = out;
  if (said.size() == sizeof(out) - 1) {  // a long name was cut: no half letter at the end
    size_t end = said.size();
    while (end > 0 && (static_cast<unsigned char>(said[end - 1]) & 0xC0) == 0x80) --end;
    if (end > 0 && static_cast<unsigned char>(said[end - 1]) >= 0xC0) said.resize(end - 1);
  }
  return said;
}

uint16_t valueKey(const SettingInfo& s) {
  const int value = s.valuePtr ? SETTINGS.*(s.valuePtr) : s.valueGetter ? s.valueGetter() : 0;
  return valueKey(s, value);
}

uint16_t valueKey(const SettingInfo& s, const int candidate) {
  if (candidate < 0) return 0;
  const char* label = I18N.get(s.nameId, Language::VI);
  if (s.nameId == StrId::STR_TIME_TO_SLEEP) {
    const char* bucket = candidate >= CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES ? "@TIMEOUT_NEVER"
                         : candidate <= 2 ? "@TIMEOUT_FAST" : candidate <= 10 ? "@TIMEOUT_MID" : "@TIMEOUT_LONG";
    return logic::quipKey(label, bucket);
  }
  if (s.nameId == StrId::STR_SCREEN_MARGIN)
    return logic::quipKey(label, candidate <= 10 ? "@MARGIN_THIN" : candidate <= 25 ? "@MARGIN_MID" : "@MARGIN_WIDE");
  if (s.nameId == StrId::STR_FONT_SIZE) {
    int points = candidate;
    if (!s.enumStringValues.empty()) {
      if (candidate >= static_cast<int>(s.enumStringValues.size())) return 0;
      points = atoi(s.enumStringValues[candidate].c_str());
    }
    return logic::quipKey(label, points <= 10 ? "@FSIZE_TINY" : points <= 13 ? "@FSIZE_SMALL"
                         : points <= 16 ? "@FSIZE_MID" : points <= 19 ? "@FSIZE_BIG" : "@FSIZE_HUGE");
  }
  if (s.type == SettingType::TOGGLE)
    return logic::quipKey(label, I18N.get(candidate ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF, Language::VI));
  if (!s.enumStringValues.empty()) {
    if (candidate >= static_cast<int>(s.enumStringValues.size())) return 0;
    const char* value = s.enumStringValues[candidate].c_str();
    if (s.nameId == StrId::STR_FONT_FAMILY && strcmp(value, "Noto Serif") && strcmp(value, "Noto Sans"))
      return logic::quipKey(label, "@FONT_SD");
    return logic::quipKey(label, value);
  }
  const auto labels = s.enumLabels();
  return candidate < static_cast<int>(labels.size()) ? logic::quipKey(label, I18N.get(labels[candidate], Language::VI)) : 0;
}

void noteValue(const SettingInfo& s) {
  noted = valueKey(s);
  hasNoted = true;
}

std::string takeNoted() {
  if (!hasNoted) return {};
  hasNoted = false;
  return quip(Quip::SetValue, noted);
}

}  // namespace ugly

#include "ReadingStatsFormat.h"

#include <I18n.h>

#include <cstdio>
namespace readingstatsview {
void duration(uint64_t ms, char* text, size_t size) {
  if (ms && ms < 60000) {
    snprintf(text, size, "%s", tr(STR_STATS_UNDER_MINUTE));
    return;
  }
  const auto minutes = ms / 60000;
  if (minutes >= 60)
    snprintf(text, size, "%llu %s %llu %s", static_cast<unsigned long long>(minutes / 60), tr(STR_STATS_HOURS),
             static_cast<unsigned long long>(minutes % 60), tr(STR_STATS_MINUTES));
  else
    snprintf(text, size, "%llu %s", static_cast<unsigned long long>(minutes), tr(STR_STATS_MINUTES));
}
}  // namespace readingstatsview

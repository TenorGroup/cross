#include "UglyWords.h"

#include <I18n.h>

namespace ugly::words {

const char* crashTitle() { return tr(STR_UGLY_CRASH_TITLE); }
const char* crashBody() { return tr(STR_UGLY_CRASH_BODY); }
const char* crashReason() { return tr(STR_UGLY_CRASH_REASON); }
const char* cacheDone() { return tr(STR_UGLY_CACHE_DONE); }
const char* cacheFail() { return tr(STR_UGLY_CACHE_FAIL); }
const char* flashing() { return tr(STR_UGLY_FLASHING); }
const char* keyboardTips() { return tr(STR_UGLY_KB_TIPS); }
const char* habitNoData() { return tr(STR_UGLY_HABIT_NO_DATA); }

}  // namespace ugly::words

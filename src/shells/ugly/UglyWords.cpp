#include "UglyWords.h"

#include <I18n.h>

namespace ugly::words {

const char* crashTitle() { return tr(STR_UGLY_CRASH_TITLE); }
const char* crashBody() { return tr(STR_UGLY_CRASH_BODY); }
const char* crashReason() { return tr(STR_UGLY_CRASH_REASON); }

}  // namespace ugly::words

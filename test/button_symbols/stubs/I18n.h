#pragma once
// ButtonSymbols.cpp only needs a StrId to compare labels against and a string
// to hand back; it never renders these strings. The real catalogue's ids come
// through unmodified (see the include below), so StrId::STR_BACK etc. stay
// the same values production code uses. Only the translation lookup itself,
// which pulls in the full string tables, is replaced.
#include "I18nKeys.h"
class I18n {
 public:
  static I18n& getInstance() {
    static I18n instance;
    return instance;
  }
  const char* get(StrId) const { return "stub"; }
};
#define tr(id) I18n::getInstance().get(StrId::id)
#define I18N I18n::getInstance()

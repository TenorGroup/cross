#include "I18n.h"

#include <cstddef>
#include <cstring>

#include "I18nStrings.h"

using namespace i18n_strings;

namespace {
constexpr uint16_t ENGLISH_FALLBACK = 0xFFFF;  // same value as scripts/gen_i18n.py
}  // namespace

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const { return get(id, _language); }

const char* I18n::get(StrId id, Language language) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  // Use generated helper function - no hardcoded switch needed!
  const LangStrings lang = getLanguageStrings(language);

  // A string a language leaves identical to English holds ENGLISH_FALLBACK (scripts/gen_i18n.py writes it);
  // it is read from the English blob through the English table. Any other value is an offset into the
  // language's own blob.
  const uint16_t off = lang.offsets[index];
  if (off == ENGLISH_FALLBACK) return STRINGS_EN_DATA + OFFSETS_EN[index];
  return lang.data + off;
}

void I18n::setLanguage(Language lang) {
  if (lang >= Language::_COUNT) {
    return;
  }
  _language = lang;
}

const char* I18n::getLanguageName(Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

Language I18n::languageFromCode(const char* code) {
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcmp(code, LANGUAGE_CODES[i]) == 0) return static_cast<Language>(i);
  }
  return Language::EN;
}

// Generate character set for a specific language
const char* I18n::getCharacterSet(Language lang) {
  const auto langIndex = static_cast<size_t>(lang);
  if (langIndex >= static_cast<size_t>(Language::_COUNT)) {
    lang = Language::EN;  // Fallback to first language
  }

  return CHARACTER_SETS[static_cast<size_t>(lang)];
}

#pragma once
enum class Language { EN, VI, ZH };
struct I18nStub {
  Language language = Language::EN;
  Language getLanguage() const { return language; }
};
inline I18nStub I18N;

#pragma once
#include <I18n.h>

#include <memory>
#include <string>
#include <vector>
class UiListActivity;
class GfxRenderer;
class MappedInputManager;
struct SettingInfo;
namespace menufavorites {
struct Descriptor {
  const char* key;
  StrId label;
  const char* screen;
  int tab;
  int row;
};
const Descriptor* find(const std::string& key);
const char* keyFor(const char* screen, int tab, int row);
StrId label(const std::string& key, const std::vector<SettingInfo>& settings);
std::string value(const std::string& key, const std::vector<SettingInfo>& settings);
std::unique_ptr<UiListActivity> open(const std::string& key, GfxRenderer& renderer, MappedInputManager& input);
}  // namespace menufavorites

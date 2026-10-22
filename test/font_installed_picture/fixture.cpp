// The installed list: which rows get the store picture. Compiles the production loadInstalledList() and
// rebuildStoreRowItems() (see run.py) against a card whose folders are scripted below.
#include <FontPackInstaller.h>
#include <SdCardFontRegistry.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "FontStoreCatalog.h"
#include "FontStoreNames.h"
#include "components/UIScale.h"

#define tr(id) #id
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
namespace fui = freeink::ui;
namespace shell { inline bool ugly = false; inline bool uglyParts() { return ugly; } }
struct { uint8_t uiTextSize = 0; } SETTINGS_STATE;
#define SETTINGS SETTINGS_STATE
struct RenderLock { template <class T> explicit RenderLock(T&&) {} };

// The card: each folder says whether it is a pack (pack.json) and whether its receipt matches the store's.
struct Folder { bool pack; bool matches; bool vector; };
std::map<std::string, Folder> card;
bool FontPackInstaller::isPackFamily(const char* root, const char* family) {
  return root && card.at(family).pack;
}
bool FontPackInstaller::matchesInstalled(const char*, const char* family, uint32_t, uint32_t) { return card.at(family).matches; }
const char* SdCardFontRegistry::findFamilyRoot(const char* family) { return card.count(family) ? "/.fonts" : nullptr; }
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  for (const auto& family : families_) if (family.name == name) return &family;
  return nullptr;
}
// The registry's own search, as the production one answers it: the file nearest in size (ties smaller), none for no file.
const SdCardFontFileInfo* SdCardFontFamilyInfo::findNearestSize(uint8_t pointSize, uint8_t) const {
  const SdCardFontFileInfo* best = nullptr;
  for (const auto& file : files) {
    const int gap = std::abs(file.pointSize - pointSize);
    if (!best || gap < std::abs(best->pointSize - pointSize) || (gap == std::abs(best->pointSize - pointSize) && file.pointSize < best->pointSize))
      best = &file;
  }
  return best;
}
struct FontSystem {
  SdCardFontRegistry reg;
  void refreshIfDirty() {}
  SdCardFontRegistry& registry() { return reg; }
} sdFontSystem;
bool fontIsLoaded(const std::string&) { return false; }
struct Renderer {
  int getTextWidth(int, const char* text, int = 0) const { return static_cast<int>(std::strlen(text)) * 10; }
  int getFontAscenderSize(int) const { return 25; }
};

#include "helpers.inc"

class FontDownloadActivity {
 public:
#include "struct.inc"
  std::vector<InstalledFont> installed_;
  std::vector<std::string> rowLabels_, rowNames_;
  std::vector<fui::ListItem> rowItems_;
  bool rowsDirty_ = false;
  Renderer renderer;
  void loadInstalledList();
  void rebuildStoreRowItems();
  const SdCardFontFamilyInfo* ownNameFamily(const InstalledFont& font) const;
  int storeRowCount() const { return static_cast<int>(installed_.size()) + 2; }
  const fui::ListItem& rowFor(const char* name) const {
    for (size_t i = 0; i < installed_.size(); ++i)
      if (installed_[i].name == name) return rowItems_[i + 1];
    assert(false);
    return rowItems_[0];
  }
};

#include "methods.inc"

static size_t packIndex(const char* name) {
  for (size_t i = 0; i < fontstore::PACK_COUNT; ++i) if (!std::strcmp(fontstore::PACKS[i].name, name)) return i;
  assert(false);
  return 0;
}

static SdCardFontFamilyInfo family(const char* name, bool vector, bool files) {
  SdCardFontFamilyInfo info;
  info.name = name;
  info.vector = vector;
  info.stems = {name};
  if (files) for (uint8_t size : {12, 14, 16, 18}) info.files.push_back({size, 0, 0});
  return info;
}

int main() {
  // Alegreya: the store's pack, up to date. Geist: the store's pack, an older receipt. Inter: a folder of the
  // reader's own with a store name (no pack.json). Bookerly: the reader's own font. Vec: a vector family. Empty: a
  // folder with no file of a size.
  card = {{"Alegreya", {true, true, false}}, {"Geist", {true, false, false}}, {"Inter", {false, false, false}},
          {"Bookerly", {false, false, false}}, {"Vec", {false, false, true}}, {"Empty", {false, false, false}}};
  std::vector<SdCardFontFamilyInfo> families = {family("Alegreya", false, true), family("Geist", false, true),
                                                family("Inter", false, true), family("Bookerly", false, true),
                                                family("Vec", true, true), family("Empty", false, false)};
  for (int size = 0; size < 3; ++size) {
    SETTINGS.uiTextSize = static_cast<uint8_t>(size);
    FontDownloadActivity activity;
    sdFontSystem.reg.adopt(families);
    activity.loadInstalledList();
    activity.rebuildStoreRowItems();
    const auto picture = [&](const char* name) { return activity.rowFor(name).labelImage; };
    const auto isStore = [&](const char* name) {
      for (size_t i = 0; i < fontstore::PACK_COUNT; ++i) if (picture(name) == &fontstore::NAME_IMAGES[size][i]) return true;
      return false;
    };
    const auto isBlank = [&](const char* name) { return picture(name) && picture(name)->bitmap.data == kBlank; };
    const auto words = [&](const char* name) { return !picture(name) && !std::strcmp(activity.rowFor(name).label, name); };

    // The store's pack, up to date: the store picture at this UI size, not words, not a blank for own letters.
    assert(picture("Alegreya") == &fontstore::NAME_IMAGES[size][packIndex("Alegreya")]);
    assert(!activity.rowFor("Alegreya").label);
    // An older pack of the store: its picture while the switch is on, else its own face (the same family on the card).
    assert(activity.rowFor("Geist").value && !std::strcmp(activity.rowFor("Geist").value, "STR_UPDATE_AVAILABLE"));
    if (kPictureForOutdatedPack) assert(picture("Geist") == &fontstore::NAME_IMAGES[size][packIndex("Geist")]);
    else assert(!isStore("Geist") && (kOwnFontNames ? isBlank("Geist") : words("Geist")));
    // A folder with a store name but no pack.json, and a font that is not the store's: never a store picture;
    // their own face when it can draw, else words.
    for (const char* mine : {"Inter", "Bookerly"}) {
      assert(!isStore(mine));
      assert(!std::strcmp(activity.rowFor(mine).value, "STR_INSTALLED"));
      if (kOwnFontNames) {
        assert(isBlank(mine) && !activity.rowFor(mine).label);
        const auto& blank = picture(mine)->bitmap;
        assert(blank.width > 0 && blank.width <= kBlankWidth && blank.height == uiTextSizeSpec(size).bodyLineHeight);
        assert(blank.height <= 44 && picture(mine)->baseline == 25);  // stands on the words' baseline, inside the blank
      } else {
        assert(words(mine));
      }
    }
    // A vector family or a folder without a file near the UI size: words.
    assert(words("Vec") && words("Empty"));
    // A face that failed once is not asked again: words.
    for (auto& font : activity.installed_) if (font.name == "Bookerly") font.ownFailed = true;
    activity.rebuildStoreRowItems();
    assert(words("Bookerly"));
  }
  std::puts("font_installed_picture: ok");
  return 0;
}

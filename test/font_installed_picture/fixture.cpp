// The installed list: which rows get the store picture, and how an own-face name gets its picture. Compiles the
// production loadInstalledList(), rebuildStoreRowItems() and renderOwnNames() (see run.py) against a card whose
// folders, and a name renderer whose answers, are scripted below.
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
#include "activities/settings/FontOwnName.h"
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
std::string SdCardFontFamilyInfo::filePath(const SdCardFontFileInfo& file, uint8_t) const {
  return "/.fonts/" + name + "/" + std::to_string(file.pointSize);
}
// The name renderer, scripted: what it makes of a family (by the folder in the path) and how often it was asked.
// The x-height it reports is `xPerMille` of the point size (the UI face stands at 14, 16, 18 px for 12, 14, 16 pt).
struct Script { bool fail = false; int xPerMille = 1150; size_t bytes = 100; };
std::map<std::string, Script> script;
std::vector<std::string> asked;  // the path of every render
namespace fontownname {
bool render(const char* path, const char* name, int maxWidth, int maxHeight, Picture& out, Accept accept, void* ctx) {
  asked.push_back(path);
  const std::string folder = std::string(path).substr(8, std::string(path).rfind('/') - 8);
  const int points = std::atoi(std::strrchr(path, '/') + 1);
  const Script& how = script[folder];
  assert(name && *name && maxWidth > 0 && maxHeight > 0);
  if (how.fail) return false;
  const int xHeight = (points * how.xPerMille + 500) / 1000;
  if (accept && !accept(ctx, xHeight)) {  // the size is not the one to draw with: nothing is drawn
    out.xHeight = static_cast<uint8_t>(xHeight);
    return false;
  }
  out.bits.reset(new uint8_t[how.bytes]);
  std::memset(out.bits.get(), 0xFF, how.bytes);
  out.size = how.bytes;
  out.width = static_cast<uint16_t>(points * 10);  // tells which size drew it
  out.height = static_cast<uint16_t>(maxHeight - 4);
  out.baseline = 15;
  out.xHeight = static_cast<uint8_t>(xHeight);
  return true;
}
}  // namespace fontownname
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
#include "own.inc"
  std::vector<std::unique_ptr<OwnPicture>> ownPictures_;
  size_t ownPictureBytes_ = 0;
  bool ownCacheFull_ = false;
  std::vector<InstalledFont> installed_;
  std::vector<std::string> rowLabels_, rowNames_;
  std::vector<fui::ListItem> rowItems_;
  bool rowsDirty_ = false;
  Renderer renderer;
  void loadInstalledList();
  void rebuildStoreRowItems();
  const SdCardFontFamilyInfo* ownNameFamily(const InstalledFont& font) const;
  const OwnPicture* ownPicture(const std::string& family) const;
  bool renderOwnPicture(const SdCardFontFamilyInfo& family, const std::string& name, OwnPicture& out);
  void renderOwnNames(int first, int count);
  void releaseOwnPictures();
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

// The rows of a list that cannot be told from words or a store picture: the picture is the row's own.
static bool ownPictured(const fui::ListItem& item) {
  return item.labelImage && !item.label && item.labelImage->bitmap.data && item.labelImage->bitmap.width > 0 &&
         !item.labelImage->bitmap.progmem && item.labelImage->bitmap.data[0] != 0;
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
    const int points = fontstore::NAME_POINTS[size];
    script.clear();
    script["Inter"].xPerMille = 1000;  // x-height 12/14/16 px: a size two points up stands on the UI face's 14/16/18
    script["Bookerly"].xPerMille = 1150;  // already on the UI face's x-height: its nearest size is the one
    asked.clear();
    FontDownloadActivity activity;
    sdFontSystem.reg.adopt(families);
    activity.loadInstalledList();
    activity.rebuildStoreRowItems();
    const auto picture = [&](const char* name) { return activity.rowFor(name).labelImage; };
    const auto isStore = [&](const char* name) {
      for (size_t i = 0; i < fontstore::PACK_COUNT; ++i) if (picture(name) == &fontstore::NAME_IMAGES[size][i]) return true;
      return false;
    };
    const auto words = [&](const char* name) { return !picture(name) && !std::strcmp(activity.rowFor(name).label, name); };
    const int rows = activity.storeRowCount();

    // The store's pack, up to date: the store picture at this UI size, not words, not a picture of its own.
    assert(picture("Alegreya") == &fontstore::NAME_IMAGES[size][packIndex("Alegreya")]);
    assert(!activity.rowFor("Alegreya").label);
    // An older pack of the store: its picture while the switch is on, else its own face (the same family on the card).
    assert(activity.rowFor("Geist").value && !std::strcmp(activity.rowFor("Geist").value, "STR_UPDATE_AVAILABLE"));
    if (kPictureForOutdatedPack) assert(picture("Geist") == &fontstore::NAME_IMAGES[size][packIndex("Geist")]);
    else assert(!isStore("Geist"));
    // Nothing is rendered by building the rows: a row that can be drawn in its own face writes its words until its
    // page is shown. A vector family or a folder without a file near the UI size: words for good.
    assert(asked.empty());
    for (const char* mine : {"Geist", "Inter", "Bookerly"}) {
      assert(!isStore(mine) || kPictureForOutdatedPack);
      if (!isStore(mine)) assert(words(mine));
    }
    assert(words("Vec") && words("Empty"));
    assert(!std::strcmp(activity.rowFor("Inter").value, "STR_INSTALLED"));

    // A page that shows no font renders none; a page with the three own faces renders each once, lazily.
    activity.renderOwnNames(0, 1);
    assert(asked.empty() && activity.ownPictures_.empty());
    activity.renderOwnNames(0, rows);
    if (kOwnFontNames) {
      for (const char* mine : {"Inter", "Bookerly"}) {
        assert(!isStore(mine) && ownPictured(activity.rowFor(mine)));
        const auto* label = picture(mine);
        assert(label->baseline == 15 && label->bitmap.height == uiTextSizeSpec(size).bodyLineHeight - 4);
        assert(label->bitmap.height <= uiTextSizeSpec(size).bodyLineHeight);
        assert(!activity.rowFor(mine).label);
      }
      if (!kPictureForOutdatedPack) assert(ownPictured(activity.rowFor("Geist")));
      // Where the face's x-height is short of the UI face's, the folder's size that lands nearest it draws the name:
      // two points up for Inter (one more render), the size nearest the UI size for Bookerly (no more).
      assert(picture("Inter")->bitmap.width == (points + 2) * 10);
      assert(picture("Bookerly")->bitmap.width == points * 10);
      int inter = 0, bookerly = 0;
      for (const auto& path : asked) {
        inter += path.find("/Inter/") != std::string::npos;
        bookerly += path.find("/Bookerly/") != std::string::npos;
      }
      assert(inter == 2 && bookerly == 1);
      assert(words("Vec") && words("Empty"));

      // The rows' pictures are the cache's: the same pictures after another build of the rows, and nothing drawn again.
      const auto before = asked.size();
      const fui::LabelImage* inter1 = picture("Inter");
      const fui::LabelImage* bookerly1 = picture("Bookerly");
      activity.rebuildStoreRowItems();
      assert(picture("Inter") == inter1 && picture("Bookerly") == bookerly1 && ownPictured(activity.rowFor("Inter")));
      activity.renderOwnNames(0, rows);
      assert(picture("Inter") == inter1 && asked.size() == before);
      assert(activity.ownPictures_.size() == (kPictureForOutdatedPack ? 2u : 3u));
      // The picture stands in the label's place with its ink kept in the cache's memory: a ListItem copy keeps it.
      fui::ListItem copy = activity.rowFor("Inter");
      assert(copy.labelImage->bitmap.data == inter1->bitmap.data);

      // Freed with the screen's list: no picture, no byte left.
      activity.releaseOwnPictures();
      assert(activity.ownPictures_.empty() && activity.ownPictureBytes_ == 0 && !activity.ownCacheFull_);
    }
  }

  // A face that cannot draw the name keeps the words and is not asked again.
  if (kOwnFontNames) {
    SETTINGS.uiTextSize = 0;
    script.clear();
    script["Bookerly"].fail = true;
    asked.clear();
    FontDownloadActivity activity;
    sdFontSystem.reg.adopt(families);
    activity.loadInstalledList();
    activity.rebuildStoreRowItems();
    activity.renderOwnNames(0, activity.storeRowCount());
    assert(ownPictured(activity.rowFor("Inter")));
    const auto after = asked.size();
    assert(!activity.rowFor("Bookerly").labelImage && !std::strcmp(activity.rowFor("Bookerly").label, "Bookerly"));
    for (auto& font : activity.installed_) assert((font.name == "Bookerly") == font.ownFailed);
    activity.rebuildStoreRowItems();
    activity.renderOwnNames(0, activity.storeRowCount());
    assert(!activity.rowFor("Bookerly").labelImage);
    assert(asked.size() == after);
  }

  // The cache stops at its cap: a picture that would pass it is dropped, the row and every row after keep their
  // words, and nothing is rendered again once it is full.
  if (kOwnFontNames) {
    SETTINGS.uiTextSize = 1;
    script.clear();
    card.clear();
    std::vector<SdCardFontFamilyInfo> many;
    for (const char* name : {"Ap", "Bp", "Cp", "Dp", "Ep"}) {
      card[name] = {false, false, false};
      many.push_back(family(name, false, true));
      script[name].xPerMille = 1170;  // on the UI face's x-height: one render each
      script[name].bytes = 6000;
    }
    asked.clear();
    FontDownloadActivity activity;
    sdFontSystem.reg.adopt(many);
    activity.loadInstalledList();
    activity.rebuildStoreRowItems();
    activity.renderOwnNames(0, activity.storeRowCount());
    int pictured = 0;
    for (size_t i = 0; i < activity.installed_.size(); ++i) pictured += ownPictured(activity.rowItems_[i + 1]);
    assert(kOwnPictureCap / 6000 == 2 && pictured == 2);
    assert(activity.ownPictureBytes_ == 12000 && activity.ownPictureBytes_ <= kOwnPictureCap && activity.ownCacheFull_);
    assert(asked.size() == 3);  // the third was drawn, did not fit, and was dropped
    for (size_t i = 2; i < activity.installed_.size(); ++i)
      assert(!activity.rowItems_[i + 1].labelImage && activity.rowItems_[i + 1].label);
    activity.rebuildStoreRowItems();
    activity.renderOwnNames(0, activity.storeRowCount());
    assert(asked.size() == 3 && activity.ownPictures_.size() == 2);
  }
  std::puts("font_installed_picture: ok");
  return 0;
}

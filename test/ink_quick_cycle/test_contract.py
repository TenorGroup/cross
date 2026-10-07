from pathlib import Path
import unittest
import os
import re
import subprocess
import tempfile


REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "src/activities/settings/TextSettingsActivity.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


class InkQuickCycleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()
        cls.confirm = function_body(cls.source, "void TextSettingsActivity::confirmStyleRow")
        cls.confirm_label = function_body(cls.source, "const char* TextSettingsActivity::confirmLabelText")
        cls.value = function_body(cls.source, "std::string TextSettingsActivity::styleValueText")
        cls.render = function_body(cls.source, "void TextSettingsActivity::render")

    def test_short_press_cycles_requested_level_and_persists(self):
        # Compile the shared row, apply and save flow with the production ink descriptor.
        signatures = [
            "int TextSettingsActivity::formIndex",
            "const SettingInfo* TextSettingsActivity::formSetting",
            "ugly::QuestionSheet::Row TextSettingsActivity::formRow",
            "bool TextSettingsActivity::saveSettings",
            "bool TextSettingsActivity::applyChosenValue",
            "void TextSettingsActivity::confirmStyleRow",
        ]
        methods = "\n".join(
            self.source[self.source.index(signature):
                        self.source.index("{", self.source.index(signature)) + 1]
            + function_body(self.source, signature) + "}"
            for signature in signatures
        )
        header = (SOURCE.with_suffix(".h")).read_text()
        enums = "\n".join(re.search(r"enum class " + name + r"(?:\s*:\s*\w+)?\s*\{[^}]+\};", header).group()
                          for name in ("Tab", "LayoutRow", "StyleRow"))
        arrays = "\n".join(re.search(r"constexpr StrId " + name + r"\[\] = \{[^}]+\};", self.source).group()
                           for name in ("LAYOUT_ROW_NAME_IDS", "STYLE_ROW_NAME_IDS"))
        catalog = (REPO / "src/SettingsList.h").read_text()
        ink_labels = ""
        if "getBaseTextSetting(" in catalog:
            descriptor = function_body(catalog, "inline std::optional<SettingInfo> getBaseTextSetting")
            ink_labels = re.search(r"static constexpr StrId ink\[\] = \{[^}]+\};", descriptor).group()
            ink = re.search(r"SettingInfo::StaticEnum\(StrId::STR_READER_INK_WEIGHT,[\s\S]+?\);", descriptor).group()[:-1]
        else:
            ink = re.search(r"SettingInfo::Enum\(StrId::STR_READER_INK_WEIGHT,[\s\S]+?\.withTextSettings\(\)", catalog).group()
        ids = sorted(set(re.findall(r"StrId::(STR_[A-Z0-9_]+)", arrays + methods + ink + ink_labels)))
        harness = r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include "ReaderInkWeight.h"
#define LOG_ERR(...) ((void)0)
struct CrossPointSettings {
  uint8_t readerInkWeight = 0;
  int writes = 0;
  bool writable = true;
  bool saveToFile() { ++writes; return writable; }
} SETTINGS;
enum class StrId { @IDS@ };
enum class SettingType { TOGGLE, VALUE, ENUM };
struct SettingInfo {
  StrId nameId;
  uint8_t CrossPointSettings::*valuePtr;
  std::vector<StrId> labels;
  SettingType type = SettingType::ENUM;
  struct { int min=0, max=0, step=0; } valueRange;
  const std::vector<StrId>& enumLabels() const { return labels; }
  static SettingInfo Enum(StrId id, uint8_t CrossPointSettings::*ptr,
                          std::vector<StrId> labels, const char*, StrId) { return {id, ptr, labels}; }
  template<size_t N> static SettingInfo StaticEnum(StrId id, uint8_t CrossPointSettings::*ptr,
                          const StrId (&labels)[N], const char*, StrId) { return {id, ptr, {labels, labels+N}}; }
  SettingInfo withTextSettings() { return *this; }
};
@INK_LABELS@
const std::vector<SettingInfo>& getBaseSettingsList() {
  static const std::vector<SettingInfo> rows = { @INK@ };
  return rows;
}
@ARRAYS@
namespace readerSpacing { int clampDropCapMode(int value) { return value; } }
struct { const char* get(StrId) { return "label"; } } I18N;
namespace ugly { struct QuestionSheet {
  enum class Kind { Paper, Choice, Toggle, Ruler };
  struct Row { uint32_t id=0; const char* question=""; int count=0, selected=0; Kind kind=Kind::Choice; };
}; }
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
struct FontBoundary {
  int loads=0;
  uint8_t mask=1;
  void ensureLoaded(int) { ++loads; }
  uint8_t availableWeightMask() const { return mask; }
} sdFontSystem;
struct TextSettingsActivity {
  @ENUMS@
  std::vector<int> fonts_, sizes_;
  int currentFamilyIndex_=0, currentSizeIndex_=0, renderer=0, paints=0;
  std::atomic<bool> saveFailed_{false};
  void requestUpdate() { ++paints; }
  bool applyFamily(int) { return false; }
  bool applySize(int) { return false; }
  static int formIndex(Tab, int);
  static const SettingInfo* formSetting(int);
  static ugly::QuestionSheet::Row formRow(void*, int);
  bool saveSettings(bool repaint=true);
  bool applyChosenValue(Tab, int, int, bool repaint=true);
  void confirmStyleRow(int);
};
@METHODS@
int main() {
  TextSettingsActivity a;
  const int row = static_cast<int>(TextSettingsActivity::StyleRow::InkWeight);
  for (uint8_t mask : {uint8_t(1), uint8_t(15)}) {
    sdFontSystem.mask = mask;
    for (int current : {0, 1, 2, 3, 255}) {
      SETTINGS.readerInkWeight = current;
      SETTINGS.writes=0; a.paints=0; sdFontSystem.loads=0;
      a.confirmStyleRow(row);
      assert(SETTINGS.readerInkWeight == readerInk::next(current));
      assert(SETTINGS.writes == 1 && a.paints == 1 && sdFontSystem.loads == 1);
      assert(!a.saveFailed_.load());
    }
  }
  SETTINGS.readerInkWeight=2; SETTINGS.writable=false; SETTINGS.writes=0; a.paints=0;
  a.confirmStyleRow(row);
  assert(SETTINGS.readerInkWeight==3 && SETTINGS.writes==1 && a.paints==1 && a.saveFailed_.load());
  SETTINGS.writable=true;
  assert(a.saveSettings(false) && SETTINGS.writes==2 && a.paints==1 && !a.saveFailed_.load());
  for (int invalid : {-1, static_cast<int>(TextSettingsActivity::StyleRow::Count)}) {
    a.confirmStyleRow(invalid);
    assert(SETTINGS.readerInkWeight==3 && SETTINGS.writes==2 && a.paints==1);
  }
}
"""
        for key, value in {"IDS": ",".join(ids), "INK": ink, "INK_LABELS": ink_labels, "ARRAYS": arrays,
                           "ENUMS": enums, "METHODS": methods}.items():
            harness = harness.replace("@" + key + "@", value)
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "quick-cycle.cpp"
            binary = Path(tmp) / "quick-cycle"
            source.write_text(harness)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I" + str(REPO / "src"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        style_label = self.confirm_label.split("case Tab::Style:", 1)[1].split("default:", 1)[0]
        self.assertIn("return tr(STR_TOGGLE);", style_label)

    def test_row_keeps_requested_label_and_missing_variant_is_explicit(self):
        self.assertIn(
            "INK_WEIGHT_IDS[readerInk::clamp(SETTINGS.readerInkWeight)]", self.value
        )
        self.assertIn(
            "!readerInk::available(SETTINGS.readerInkWeight, sdFontSystem.availableWeightMask())",
            self.render,
        )


if __name__ == "__main__":
    unittest.main()

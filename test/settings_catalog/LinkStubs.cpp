// Link stubs for the settings JSON round-trip test.
//
// The code under test is CrossPointSettings::toJson/fromJson, which walks the
// whole settings catalogue. Parts of that catalogue sit beside owners that keep
// their own storage (KOReader credentials, the tilt sensor, credential
// obfuscation, file serialization). This suite never touches those, so this
// translation unit satisfies the linker for them and keeps the test pointed at
// one subject. Nothing here changes what the settings code does with a
// wakeIntoBook value.

#include "KOReaderCredentialStore.h"
#include "ObfuscationUtils.h"
#include "PersistableStore.h"

#include <HalTiltSensor.h>
#include <SdCardFontRegistry.h>

#include "components/UITheme.h"

namespace settings_test_io {
JsonDocument nextRead;
int writes = 0;

void setNextRead(const JsonDocument& doc) {
  nextRead.clear();
  nextRead.set(doc.as<JsonVariantConst>());
  writes = 0;
}
}  // namespace settings_test_io

// The catalogue asks the tilt sensor whether the hardware is present.
HalTiltSensor halTiltSensor;

// The catalogue asks the SD-font registry which point sizes a family offers.
// A host has no registry: no family is found, so the built-in size list is used.
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
 for(const auto& f:getFamilies()) if(f.name==name)return &f; return nullptr;
}

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
 std::vector<uint8_t> sizes; for(const auto& f:files)sizes.push_back(f.pointSize);return sizes;
}

void KOReaderCredentialStore::setCredentials(const std::string&, const std::string&) {}
void KOReaderCredentialStore::setServerUrl(const std::string&) {}
void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod) {}
void KOReaderCredentialStore::setSendMetadata(bool) {}
void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior) {}
void KOReaderCredentialStore::toJson(JsonDocument&) const {}

bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument& doc) {
  ++settings_test_io::writes;
  settings_test_io::nextRead.clear();
  settings_test_io::nextRead.set(doc.as<JsonVariantConst>());
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char*, JsonDocument& doc) {
  doc.set(settings_test_io::nextRead.as<JsonVariantConst>());
  return true;
}

namespace obfuscation {
// Credential obfuscation is device-keyed; the settings round trip only needs it
// to be reversible, so the test double is the identity.
String obfuscateToBase64(const std::string& plaintext) { return String(plaintext.c_str()); }

std::string deobfuscateFromBase64(const char* encoded, const size_t, bool* ok, bool* tooLong) {
  if (ok) *ok = true;
  if (tooLong) *tooLong = false;
  return encoded ? std::string(encoded) : std::string();
}
}  // namespace obfuscation

#include <HalClock.h>
#include <HalGPIO.h>
HalClock halClock;
HalGPIO gpio;
InputManager::InputManager() {}

#include "activities/settings/TextSettingsActivity.h"
#include "activities/settings/ClockSettingsActivity.h"
std::string TextSettingsActivity::layoutValueText(int){return "layout-boundary";}
std::string TextSettingsActivity::styleValueText(int){return "style-boundary";}
std::string ClockSettingsActivity::giaTriDong(int){return "clock-boundary";}

namespace tenorchrome {
int tipLineCount(const GfxRenderer&, const char*, int) { return 0; }
int tipHeight(const GfxRenderer&, const char*, int) { return 0; }
}  // namespace tenorchrome

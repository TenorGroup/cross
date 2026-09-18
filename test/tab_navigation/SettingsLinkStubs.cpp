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

// The catalogue asks the tilt sensor whether the hardware is present.
HalTiltSensor halTiltSensor;

// The catalogue asks the SD-font registry which point sizes a family offers.
// A host has no registry: no family is found, so the built-in size list is used.
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string&) const { return nullptr; }

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const { return {}; }

void KOReaderCredentialStore::setCredentials(const std::string&, const std::string&) {}
void KOReaderCredentialStore::setServerUrl(const std::string&) {}
void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod) {}
void KOReaderCredentialStore::setSendMetadata(bool) {}
void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior) {}
void KOReaderCredentialStore::toJson(JsonDocument&) const {}

// Only used by saveToFile(), which this suite does not call.
bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument&) { return true; }

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

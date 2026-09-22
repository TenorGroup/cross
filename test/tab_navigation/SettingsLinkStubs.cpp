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

// UiTabListActivity owns tab navigation. This host target does not link the
// hardware IMU driver. The seam injects one physical forward tilt and maps it
// through the real mode contract, so routing tests can observe the mode that
// each activity supplied to the sensor.
namespace tiltfixture {
uint8_t lastMode = CrossPointTiltPageTurn::TILT_OFF;
bool lastTargetActive = false;
bool physicalForward = false;
bool forwardEvent = false;
bool backwardEvent = false;
// Vertical channel: the menu row axis, armed by its own mode and target gate.
uint8_t lastVerticalMode = CrossPointTiltPageTurn::TILT_OFF;
bool lastVerticalTargetActive = false;
bool physicalVertical = false;
bool upEvent = false;
bool downEvent = false;

void reset() {
  lastMode = CrossPointTiltPageTurn::TILT_OFF;
  lastTargetActive = false;
  physicalForward = false;
  forwardEvent = false;
  backwardEvent = false;
  lastVerticalMode = CrossPointTiltPageTurn::TILT_OFF;
  lastVerticalTargetActive = false;
  physicalVertical = false;
  upEvent = false;
  downEvent = false;
}

void injectPhysicalForward() { physicalForward = true; }

// One physical flick on the vertical axis: the top edge leaning toward the
// user, which Normal reports as a step up.
void injectPhysicalVertical() { physicalVertical = true; }
}  // namespace tiltfixture

void HalTiltSensor::update(const uint8_t mode, const uint8_t, const bool gestureTargetActive) {
  tiltfixture::lastMode = mode;
  tiltfixture::lastTargetActive = gestureTargetActive;
  tiltfixture::forwardEvent = false;
  tiltfixture::backwardEvent = false;
  if (!tiltfixture::physicalForward) return;

  tiltfixture::physicalForward = false;
  if (!gestureTargetActive || mode == CrossPointTiltPageTurn::TILT_OFF) return;
  if (mode == CrossPointTiltPageTurn::TILT_NORMAL) {
    tiltfixture::forwardEvent = true;
  } else if (mode == CrossPointTiltPageTurn::TILT_INVERTED) {
    tiltfixture::backwardEvent = true;
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool event = tiltfixture::forwardEvent;
  tiltfixture::forwardEvent = false;
  return event;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool event = tiltfixture::backwardEvent;
  tiltfixture::backwardEvent = false;
  return event;
}

void HalTiltSensor::configureVerticalGesture(const uint8_t mode, const bool gestureTargetActive) {
  tiltfixture::lastVerticalMode = mode;
  tiltfixture::lastVerticalTargetActive = gestureTargetActive;
  tiltfixture::upEvent = false;
  tiltfixture::downEvent = false;
  if (!tiltfixture::physicalVertical) return;

  tiltfixture::physicalVertical = false;
  if (!gestureTargetActive || mode == CrossPointTiltPageTurn::TILT_OFF) return;
  if (mode == CrossPointTiltPageTurn::TILT_NORMAL) {
    tiltfixture::upEvent = true;
  } else if (mode == CrossPointTiltPageTurn::TILT_INVERTED) {
    tiltfixture::downEvent = true;
  }
}

bool HalTiltSensor::wasTiltedUp() {
  const bool event = tiltfixture::upEvent;
  tiltfixture::upEvent = false;
  return event;
}

bool HalTiltSensor::wasTiltedDown() {
  const bool event = tiltfixture::downEvent;
  tiltfixture::downEvent = false;
  return event;
}

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

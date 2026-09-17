#pragma once

// Test-only control surface for the SdCardFontRegistry host stub
// (see ../RegistryStubs.cpp). FontInstaller asks the registry only two
// questions - "which root already holds this family?" and "which root should a
// new family go to?" - so the stub lets a test answer both and count the asks,
// which is what proves rejected input never reaches the registry at all.

#include <SdCardFontRegistry.h>

namespace font_installer_test {

/// Root SdCardFontRegistry::findFamilyRoot() reports as holding the family.
/// nullptr = the family is not installed anywhere.
extern const char* existingRoot;

/// Root SdCardFontRegistry::defaultWriteRoot() reports for a new install.
extern const char* writeRoot;

/// findFamilyRoot()/defaultWriteRoot() calls since the last reset.
extern int registryRootLookups;

/// Answer "not installed, install under the hidden root" and zero the counter.
inline void resetRegistryStub() {
  existingRoot = nullptr;
  writeRoot = SdCardFontRegistry::FONTS_DIR_HIDDEN;
  registryRootLookups = 0;
}

}  // namespace font_installer_test

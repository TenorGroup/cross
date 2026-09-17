// Host definitions for the real SdCardFontRegistry interface: the production
// header (lib/EpdFont/SdCardFontRegistry.h) is the one that is compiled, so a
// signature or root-constant change there breaks this target at compile time.
//
// FontInstaller uses exactly two root queries, plus discover()/findFamily() via
// refreshRegistry()/isFamilyInstalled(). The real bodies scan the SD card, which
// is out of scope for a validation test, so the root answers come from the test
// hooks instead - letting a test place a family in either root and count the
// lookups.

#include <SdCardFontRegistry.h>

#include "FontInstallerTestHooks.h"

namespace font_installer_test {
const char* existingRoot = nullptr;
const char* writeRoot = SdCardFontRegistry::FONTS_DIR_HIDDEN;
int registryRootLookups = 0;
}  // namespace font_installer_test

const char* SdCardFontRegistry::findFamilyRoot(const char*) {
  ++font_installer_test::registryRootLookups;
  return font_installer_test::existingRoot;
}

const char* SdCardFontRegistry::defaultWriteRoot() {
  ++font_installer_test::registryRootLookups;
  return font_installer_test::writeRoot;
}

// No SD scan on the host: FontInstaller only forwards to discovery.
bool SdCardFontRegistry::discover() { return false; }

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string&) const { return nullptr; }

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "activities/settings/FontDownloadReleaseGuard.h"

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::string readFile(const std::string& path) {
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void testProductionTransitionOwnsItsRelease() {
  const std::string source =
      readFile(std::string(REPO_ROOT_PATH) + "/src/activities/settings/FontDownloadActivity.cpp");
  expect(source.find("familyListReleaseGuard_") != std::string::npos,
         "FontDownload must retain the release that cancelled its download");
  expect(source.find("bool FontDownloadActivity::handleButtons()") != std::string::npos,
         "FontDownload must intercept the retained release before UiListActivity");
  expect(source.find("returnToFamilyList(fontdownload::ReleaseButton::Back)") != std::string::npos,
         "Back cancel and terminal Back must arm their release guard");
  expect(source.find("returnToFamilyList(fontdownload::ReleaseButton::Confirm)") != std::string::npos,
         "terminal Confirm must arm its release guard");
  expect(source.find("familyListReleaseGuard_.consumeIfReleased") != std::string::npos,
         "the family list must consume the retained release before the base list");

  const size_t home = source.find("if (goHomeRequested_)");
  const size_t abortedFamilyList = source.find("returnToFamilyList(fontdownload::ReleaseButton::Back)", home);
  expect(home != std::string::npos && abortedFamilyList != std::string::npos && home < abortedFamilyList,
         "long Home must exit before the normal Back release guard");
}

void testBackCancelDoesNotEscapeTheFamilyList() {
  fontdownload::ReleaseConsumptionGuard guard;
  guard.arm(fontdownload::ReleaseButton::Back);

  expect(!guard.consumeIfReleased(false, true), "a held Back is not a release");
  expect(guard.active(), "the pending Back release must remain armed");
  expect(guard.consumeIfReleased(true, false), "the cancelling Back release must be consumed");
  expect(!guard.active(), "the consumed Back release must clear its guard");
  expect(!guard.consumeIfReleased(true, false), "the next independent Back release must reach the list");
}

void testTerminalConfirmDoesNotReactivateTheRow() {
  fontdownload::ReleaseConsumptionGuard guard;
  guard.arm(fontdownload::ReleaseButton::Confirm);

  expect(!guard.consumeIfReleased(false, true), "a held Confirm is not a release");
  expect(guard.consumeIfReleased(true, false), "the terminal Confirm release must be consumed");
  expect(!guard.active(), "the terminal Confirm guard must clear after one release");
}

void testMissedReleaseCannotSwallowLaterInput() {
  fontdownload::ReleaseConsumptionGuard guard;
  guard.arm(fontdownload::ReleaseButton::Back);

  expect(!guard.consumeIfReleased(false, false), "an already-up Back clears a stale guard");
  expect(!guard.active(), "a stale guard must clear before another input");
  expect(!guard.consumeIfReleased(true, false), "a later Back release remains available to the list");
}

}  // namespace

int main() {
  testProductionTransitionOwnsItsRelease();
  testBackCancelDoesNotEscapeTheFamilyList();
  testTerminalConfirmDoesNotReactivateTheRow();
  testMissedReleaseCannotSwallowLaterInput();
  return EXIT_SUCCESS;
}

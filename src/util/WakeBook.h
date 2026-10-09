#pragma once

#include <algorithm>
#include <string>
#include <vector>

// Where a wake from sleep lands when "Wake into the book" is on: the book last opened, if it
// is still in the Recent list and still on the card. A book taken off Recent, by hand or when
// finished, is left alone by the next wake. Empty means Home.
namespace wakebook {

template <typename Book, typename Exists>
std::string bookToOpen(const bool sleepWake, const bool settingOn, const std::string& lastBook,
                       const std::vector<Book>& recents, Exists&& onCard, const bool normalColdBoot = false) {
  if ((!sleepWake && !normalColdBoot) || !settingOn || lastBook.empty()) return {};
  const bool inRecents =
      std::any_of(recents.begin(), recents.end(), [&](const Book& b) { return b.path == lastBook; });
  if (!inRecents || !onCard(lastBook)) return {};
  return lastBook;
}

}  // namespace wakebook

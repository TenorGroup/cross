#pragma once
// Delete and rename of an entry on the card: one flow for the file browser and Home's File card.
#include <functional>
#include <string>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace fileactions {
// Asks before deleting `path` (a file, or a folder with all it holds); `deleted` runs once it is gone.
void confirmDelete(Activity& host, GfxRenderer& renderer, MappedInputManager& input, const std::string& path,
                   std::function<void()> deleted);
// Asks for a new name for the file at `path`, its extension kept; `renamed(newPath)` runs once it has moved.
void rename(Activity& host, GfxRenderer& renderer, MappedInputManager& input, const std::string& path,
            std::function<void(const std::string&)> renamed);
// Deletes `path` now, a folder with everything in it, and the reading cache of each book. False on the first failure.
bool remove(const std::string& path);
}  // namespace fileactions

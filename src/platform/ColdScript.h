#pragma once

#include <RecoverableFile.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

// The pure part of the X4 Pro probe's card script (ColdLog.h): commands read one line at a time
// from a file on the card, kept apart from the hardware so the host tests run it.
namespace cold_script {

// The first command in text and everything after its line. Lines are trimmed (CR included); blank
// lines, '#' comments and a UTF-8 byte order mark are skipped. False when text holds no command.
inline bool popLine(const std::string& text, std::string& line, std::string& rest) {
  size_t pos = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    size_t first = pos, last = end;
    pos = end + 1;
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    if (first == last || text[first] == '#') continue;
    line.assign(text, first, last - first);
    rest = pos < text.size() ? text.substr(pos) : std::string();
    return true;
  }
  return false;
}

// "WAIT <ms>": the script holds its next line that long while the loop keeps running.
inline bool parseWait(const std::string& line, uint32_t& ms) {
  if (line.compare(0, 5, "WAIT ") != 0) return false;
  size_t at = 5;
  while (at < line.size() && line[at] == ' ') ++at;
  if (at == line.size() || line.size() - at > 9) return false;
  for (size_t i = at; i < line.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(line[i]))) return false;
  ms = static_cast<uint32_t>(std::strtoul(line.c_str() + at, nullptr, 10));
  return true;
}

// Whether a wait ending at `until` is over at `now` (millis, which wraps after 49 days).
inline bool waitOver(const uint32_t now, const uint32_t until) { return static_cast<int32_t>(now - until) >= 0; }

enum class Take : uint8_t { Line, Done, Failed };

// Takes the next command out of the script and commits the rest to the card before handing the
// command over. The commit is the last card operation before the return: renaming the rest
// (written whole to path + ".tmp") over the script, whose old text is kept as path + ".davbak"
// meanwhile (RecoverableFile), or deleting the script for its last command. A boot that dies after
// the commit (the command restarts or crashes the unit) goes on with the next line; one that dies
// before it takes the same line again, which has not run. Failed: the card could not be read or
// the commit did not land, and the line must not run (it would run again on every boot).
// Store: exists, rename, remove, read(path, std::string&), write(path, const std::string&).
template <typename Store>
Take take(Store& store, const std::string& path, std::string& line) {
  const std::string backup = path + ".davbak";
  const std::string staging = path + ".tmp";
  // A cut between the two renames below: the old script comes back and its line was not taken.
  if (!freeink::recoverFile(store, path.c_str())) return Take::Failed;
  if (!store.exists(path.c_str())) return Take::Done;
  // The old script beside the new one: that commit landed and its line was handed over.
  if (store.exists(backup.c_str()) && !store.remove(backup.c_str())) return Take::Failed;
  std::string text, rest, next, after;
  if (!store.read(path.c_str(), text)) return Take::Failed;
  if (!popLine(text, line, rest)) return store.remove(path.c_str()) ? Take::Done : Take::Failed;
  if (!popLine(rest, next, after)) return store.remove(path.c_str()) ? Take::Line : Take::Failed;
  if (!store.write(staging.c_str(), rest) || !store.rename(path.c_str(), backup.c_str())) return Take::Failed;
  if (!store.rename(staging.c_str(), path.c_str())) {
    store.rename(backup.c_str(), path.c_str());
    return Take::Failed;
  }
  return Take::Line;
}

}  // namespace cold_script

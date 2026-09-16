#pragma once
#include <string>
namespace filefavorites {
bool isFileKey(const std::string& key);
std::string keyFor(const std::string& path, bool folder);
std::string pathFor(const std::string& key);
bool unpin(const std::string& key);
bool toggle(const std::string& path, bool folder);
}  // namespace filefavorites

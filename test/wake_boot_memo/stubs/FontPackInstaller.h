#pragma once

// Host double: the boot test has no card, so no family is a store pack and none carries a line factor.
class FontPackInstaller {
 public:
  static bool isPackFamily(const char*, const char*) { return false; }
  static float lineFactor(const char*, const char*) { return 0.0f; }
};

#pragma once
#include <cstddef>
// A manifest offering a stable release for this board, newer unless a case sets fixtureTag.
extern const char* fixtureTag;
class ReleaseJsonParser {
 public:
  void setFirmwareAssetName(const char*) {}
  void feed(const char*, size_t) {}
  bool complete() const { return true; }
  bool foundTag() const { return true; }
  bool foundFirmware() const { return true; }
  const char* getTagName() const { return fixtureTag; }
  const char* getFirmwareUrl() const { return "https://cross.tenor.vn/firmware/tenor-cross-x3-x4.bin"; }
  const char* getFirmwareDigest() const {
    return "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  }
  size_t getFirmwareSize() const;
};

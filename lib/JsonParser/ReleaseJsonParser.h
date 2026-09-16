#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"
#include "StrictJsonValidator.h"

class ReleaseJsonParser {
 public:
  ReleaseJsonParser();

  ReleaseJsonParser(const ReleaseJsonParser&) = delete;
  ReleaseJsonParser& operator=(const ReleaseJsonParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // Release-asset filename to match (default "firmware.bin").
  void setFirmwareAssetName(const char* name);

  bool foundTag() const;
  bool foundFirmware() const;
  const char* getTagName() const;
  const char* getFirmwareUrl() const;
  size_t getFirmwareSize() const;
  const char* getFirmwareDigest() const { return firmwareDigest; }
  bool complete() const { return rootClosed && !invalid && !parser.hasError() && syntax.complete(); }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ASSETS_ARRAY,
    IN_ASSET_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    TAG_NAME,
    ASSETS,
    ASSET_NAME,
    ASSET_URL,
    ASSET_SIZE,
    ASSET_DIGEST,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitAsset();

  StreamingJsonParser parser;
  StrictJsonValidator syntax;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t assetDepth;

  uint8_t rootKeys = 0;
  uint8_t assetKeys = 0;
  bool rootClosed = false;
  bool invalid = false;
  char firmwareDigest[72] = {};
  char currentAssetDigest[72] = {};
  char tagName[32];
  char firmwareUrl[512];
  size_t firmwareSize;
  bool tagFound;
  bool firmwareFound;

  char currentAssetName[48];
  char currentAssetUrl[512];
  size_t currentAssetSize;

  char firmwareAssetName[48];
};

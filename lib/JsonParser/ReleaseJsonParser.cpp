#include "ReleaseJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

ReleaseJsonParser::ReleaseJsonParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), "firmware.bin", sizeof("firmware.bin") - 1);
  reset();
}

void ReleaseJsonParser::setFirmwareAssetName(const char* name) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), name, strlen(name));
}

void ReleaseJsonParser::reset() {
  parser.reset();
  syntax.reset();
  rootKeys = assetKeys = 0;
  rootClosed = false;
  invalid = false;
  firmwareDigest[0] = currentAssetDigest[0] = 0;
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  tagName[0] = '\0';
  firmwareUrl[0] = '\0';
  firmwareSize = 0;
  tagFound = false;
  firmwareFound = false;
  currentAssetDigest[0] = '\0';
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void ReleaseJsonParser::feed(const char* data, size_t len) {
  syntax.feed(data, len);
  parser.feed(data, len);
}

bool ReleaseJsonParser::foundTag() const { return tagFound; }
bool ReleaseJsonParser::foundFirmware() const { return firmwareFound; }
const char* ReleaseJsonParser::getTagName() const { return tagName; }
const char* ReleaseJsonParser::getFirmwareUrl() const { return firmwareUrl; }
size_t ReleaseJsonParser::getFirmwareSize() const { return firmwareSize; }

void ReleaseJsonParser::commitAsset() {
  if (strcmp(currentAssetName, firmwareAssetName) == 0) {
    if (firmwareFound) invalid = true;
    memcpy(firmwareUrl, currentAssetUrl, sizeof(firmwareUrl));
    firmwareSize = currentAssetSize;
    memcpy(firmwareDigest, currentAssetDigest, sizeof(firmwareDigest));
    firmwareFound = true;
  }
  currentAssetDigest[0] = '\0';
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void ReleaseJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 6 && memcmp(key, "digest", 6) == 0)
          self->lastKey = LastKey::ASSET_DIGEST;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
  if ((self->position == Position::TOP_LEVEL && self->depth == 1) ||
      (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)) {
    if (self->lastKey != LastKey::NONE) {
      auto& seen = self->position == Position::TOP_LEVEL ? self->rootKeys : self->assetKeys;
      const uint8_t mask = 1u << static_cast<uint8_t>(self->lastKey);
      if (seen & mask) self->invalid = true;
      seen |= mask;
    }
  }
}

void ReleaseJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->lastKey) {
    case LastKey::TAG_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1) {
        safeCopy(self->tagName, sizeof(self->tagName), value, len);
        self->tagFound = len > 0 && len < sizeof(self->tagName);
        if (!self->tagFound) self->invalid = true;
      }
      break;
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
        if (len >= sizeof(self->currentAssetName))
          self->invalid = true;
        else
          safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      }
      break;
    case LastKey::ASSET_DIGEST:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
        if (len >= sizeof(self->currentAssetDigest))
          self->invalid = true;
        else
          safeCopy(self->currentAssetDigest, sizeof(self->currentAssetDigest), value, len);
      }
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
        if (len >= sizeof(self->currentAssetUrl))
          self->invalid = true;
        else
          safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      }
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    size_t size = 0;
    bool valid = len > 0;
    for (size_t i = 0; i < len; ++i) {
      if (value[i] < '0' || value[i] > '9' || size > (SIZE_MAX - (value[i] - '0')) / 10) {
        valid = false;
        break;
      }
      size = size * 10 + value[i] - '0';
    }
    if (!valid) self->invalid = true;
    self->currentAssetSize = valid ? size : 0;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNull(void* ctx) { static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE; }

void ReleaseJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->rootClosed) self->invalid = true;
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->assetKeys = 0;
      self->currentAssetDigest[0] = '\0';
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetSize = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void ReleaseJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      if (self->depth == 0) self->rootClosed = true;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ASSETS && self->depth == 1) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}

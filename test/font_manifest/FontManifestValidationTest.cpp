#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <string>

#include "FontManifestValidation.h"

namespace {

bool validatesShape(const char* json) {
  JsonDocument document;
  if (deserializeJson(document, json)) return false;
  return font_manifest::validateRequiredShape(document);
}

bool validatesManifest(const char* json) {
  JsonDocument document;
  if (deserializeJson(document, json)) return false;
  return font_manifest::validateRequiredShape(document) &&
         font_manifest::isSupportedBaseUrl(document["baseUrl"].as<const char*>());
}

constexpr const char* kValidGeneratorManifest = R"json({
  "version": 1,
  "baseUrl": "http://127.0.0.1:8000/",
  "scriptGroups": [],
  "families": [{
    "name": "NotoSansSC",
    "description": "NotoSansSC",
    "styles": ["regular"],
    "scripts": [],
    "files": [
      {"name": "NotoSansSC_12.cpfont", "size": 3403611, "crc32": 1690722923},
      {"name": "NotoSansSC_18.cpfont", "size": 6974052, "crc32": 268158750}
    ]
  }]
})json";

constexpr const char* kBaseManifestPrefix = R"json({
  "version": 1,
  "baseUrl": "BASE_URL",
  "families": [{
    "name": "NotoSansSC",
    "files": [{"name": "NotoSansSC_12.cpfont", "size": 1, "crc32": 1}]
  }]
})json";

}  // namespace

TEST(FontManifestValidation, AcceptsGeneratorManifestAndBothHttpSchemes) {
  EXPECT_TRUE(validatesManifest(kValidGeneratorManifest));
  EXPECT_TRUE(font_manifest::isSupportedBaseUrl("http://127.0.0.1:8000/"));
  EXPECT_TRUE(font_manifest::isSupportedBaseUrl("https://fonts.example/releases/"));
}

TEST(FontManifestValidation, RejectsMissingOrEmptyFamiliesAndFiles) {
  EXPECT_FALSE(validatesShape(R"json({"version":1})json"));
  EXPECT_FALSE(validatesShape(R"json({"version":1,"families":[]})json"));
  EXPECT_FALSE(validatesShape(
      R"json({"version":1,"families":[{"name":"NotoSansSC"}]})json"));
  EXPECT_FALSE(validatesShape(
      R"json({"version":1,"families":[{"name":"NotoSansSC","files":[]}]})json"));
  EXPECT_FALSE(validatesShape(
      R"json({"version":1,"families":[{"name":"NotoSansSC","files":[null]}]})json"));
}

TEST(FontManifestValidation, RejectsNonPositiveOrNonUint32FileSizes) {
  EXPECT_FALSE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","size":0,"crc32":1}]}]})json"));
  EXPECT_FALSE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","crc32":1}]}]})json"));
  EXPECT_FALSE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","size":-1,"crc32":1}]}]})json"));
  EXPECT_FALSE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","size":1.5,"crc32":1}]}]})json"));
  EXPECT_FALSE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","size":4294967296,"crc32":1}]}]})json"));
  EXPECT_TRUE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","size":4294967295,"crc32":1}]}]})json"));
}

TEST(FontManifestValidation, RejectsFamilyTotalOverflowBeforeDownload) {
  EXPECT_FALSE(validatesShape(R"json({"families":[{"name":"NotoSansSC","files":[{"name":"a.cpfont","size":4294967295,"crc32":1},{"name":"b.cpfont","size":1,"crc32":2}]}]})json"));
}

TEST(FontManifestValidation, RejectsMissingOrUnsupportedBaseUrl) {
  const char* const invalidUrls[] = {
      "", "BASE_URL", "ftp://fonts.example/", "https://fonts.example", "https://fonts.example/?v=1",
      "https://fonts.example/#latest", "https:///missing-host/", "https://fonts.example:0/"};
  for (const char* url : invalidUrls) {
    std::string manifest = kBaseManifestPrefix;
    const size_t marker = manifest.find("BASE_URL");
    ASSERT_NE(marker, std::string::npos);
    manifest.replace(marker, sizeof("BASE_URL") - 1, url);
    EXPECT_FALSE(validatesManifest(manifest.c_str())) << url;
  }
}

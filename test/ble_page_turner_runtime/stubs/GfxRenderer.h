#pragma once

class FontCacheManager;

class GfxRenderer {
 public:
  FontCacheManager* getFontCacheManager() const { return fontCacheManager_; }
  void setFontCacheManager(FontCacheManager* manager) { fontCacheManager_ = manager; }

 private:
  FontCacheManager* fontCacheManager_ = nullptr;
};

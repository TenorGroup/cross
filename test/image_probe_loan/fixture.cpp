#include <functional>
#include <iostream>
#include <string>

#define LOG_ERR(...) ((void)0)
void delay(int) {}
struct ImageDimensions { int width; int height; };
struct ImageDimsProbe {
  bool found = false;
  bool getDimensions(ImageDimensions& dims) const {
    if (found) dims = {640, 480};
    return found;
  }
};
struct GfxRenderer {
  bool lent = false;
  unsigned loans = 0;
  struct FrameBufferLoan {
    GfxRenderer& renderer;
    bool active;
    explicit FrameBufferLoan(GfxRenderer& r) : renderer(r), active(!r.lent) {
      if (active) { r.lent = true; ++r.loans; }
    }
    ~FrameBufferLoan() { if (active) renderer.lent = false; }
  };
};
struct HalFile { void flush() {} void close() {} };
struct StorageBoundary {
  bool openFileForWrite(const char*, const std::string&, HalFile&) { return true; }
} Storage;
struct ImageToFramebufferDecoder {
  bool getDimensions(const std::string&, ImageDimensions& dims) {
    dims = {640, 480}; return true;
  }
};
namespace ImageDecoderFactory {
ImageToFramebufferDecoder* getDecoder(const std::string&) {
  static ImageToFramebufferDecoder decoder;
  return &decoder;
}
}
struct EpubBoundary {
  GfxRenderer& renderer;
  bool heapEnough = true, headerValid = true, extractionWorks = true;
  int probes = 0, extractions = 0;
  bool readItemContentsToStream(const std::string&, ImageDimsProbe& probe, int, bool) {
    ++probes;
    probe.found = headerValid && (heapEnough || renderer.lent);
    return probe.found;
  }
  bool readItemContentsToStream(const std::string&, HalFile&, int) {
    ++extractions;
    return extractionWorks && (heapEnough || renderer.lent);
  }
};
struct ParserBoundary {
  GfxRenderer renderer;
  EpubBoundary stream{renderer};
  EpubBoundary* epub = &stream;
  bool imagePopupFired = false;
  int popups = 0;
  std::function<void()> popupFn = [this] { ++popups; };
  bool probe() {
    auto* self = this;
    const std::string resolvedPath = "image.png", cachedImagePath = "cached.png";
@@PROBE@@
    return gotDimensions && dims.width == 640 && dims.height == 480;
  }
};
int main() {
  unsigned passed = 0;
  auto check = [&](bool condition, const char* name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    passed += condition;
  };
  ParserBoundary healthy;
  check(healthy.probe() && healthy.stream.probes == 1 && healthy.stream.extractions == 0 &&
        healthy.renderer.loans == 0 && !healthy.renderer.lent, "healthy header avoids loans");
  ParserBoundary constrained;
  constrained.stream.heapEnough = false;
  check(constrained.probe() && constrained.stream.probes == 2 && constrained.stream.extractions == 0 &&
        constrained.popups == 0 && constrained.renderer.loans == 1 && !constrained.renderer.lent,
        "header retry uses scratch before extraction");
  ParserBoundary fallback;
  fallback.stream.heapEnough = false;
  fallback.stream.headerValid = false;
  check(fallback.probe() && fallback.stream.extractions == 1 && fallback.popups == 1 &&
        fallback.renderer.loans == 2 && !fallback.renderer.lent, "fallback extraction uses scratch");
  ParserBoundary failure;
  failure.stream.heapEnough = false;
  failure.stream.headerValid = false;
  failure.stream.extractionWorks = false;
  check(!failure.probe() && failure.renderer.loans == 2 && !failure.renderer.lent,
        "failed extraction returns scratch");
  std::cout << passed << "/4 image-probe paths pass\n";
  return passed == 4 ? 0 : 1;
}

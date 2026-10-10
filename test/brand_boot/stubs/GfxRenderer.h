#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
inline uint32_t millis() { return 0; }
struct HalDisplay {
 enum class GrayscaleMode { Absolute };
 enum class GrayscaleBase { Combined, Separate };
 static constexpr int FULL_REFRESH = 1;
 struct Caps { bool available; GrayscaleBase base; bool supported() const { return available; } };
};
struct GfxRenderer {
 enum Orientation { Portrait, LandscapeClockwise };
 enum Mode { BW };
 int width, height;
 bool available = true;
 bool combined = false;
 Orientation orientation = LandscapeClockwise;
 int bases = 0, gray = 0, full = 0, versions = 0;
 std::vector<uint8_t> framebuffer, lsb, msb;
 explicit GfxRenderer(int w, int h) : width(w), height(h), framebuffer(w*h/8), lsb(w*h/8), msb(w*h/8) {}
 auto grayscaleCapabilities(HalDisplay::GrayscaleMode) const { return HalDisplay::Caps{available, combined ? HalDisplay::GrayscaleBase::Combined : HalDisplay::GrayscaleBase::Separate}; }
 bool hasFrameBuffer() const { return !framebuffer.empty(); }
 size_t getBufferSize() const { return framebuffer.size(); }
 uint8_t* getFrameBuffer() { return framebuffer.data(); }
 Orientation getOrientation() const { return orientation; }
 void setOrientation(Orientation value) { orientation = value; }
 int getScreenWidth() const { return width; }
 int getScreenHeight() const { return height; }
 void setRenderMode(Mode) {}
 void drawCenteredText(int, int y, const char* version) { if (y != height-30 || std::string(version) != "v1.0.57") __builtin_trap(); ++versions; }
 void displayBuffer(int) { ++full; }
 bool displayGrayscaleBase(HalDisplay::GrayscaleMode) { ++bases; return true; }
 void copyGrayscaleLsbBuffers() { std::memcpy(lsb.data(), framebuffer.data(), lsb.size()); }
 void copyGrayscaleMsbBuffers() { std::memcpy(msb.data(), framebuffer.data(), msb.size()); }
 void displayGrayBuffer() { ++gray; }
};

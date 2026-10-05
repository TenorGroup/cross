#include <Epub/Page.h>
#include <Epub/TokenBoundary.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <Serialization.h>

namespace parserImageFixture {
bool enabled = false;
}

const char* lookupHtmlEntity(const char*, size_t) { return nullptr; }

#include <BidiUtils.h>

bool isExplicitHyphen(uint32_t) { return false; }
bool isSoftHyphen(uint32_t) { return false; }

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}

bool ImageDecoderFactory::isFormatSupported(const std::string& path) {
  return parserImageFixture::enabled && path.ends_with(".png");
}
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t width, int64_t height, ImageDimensions& dims,
                                                          const char*) {
  if (!parserImageFixture::enabled || width <= 0 || height <= 0 || width > INT16_MAX || height > INT16_MAX) return false;
  dims = {static_cast<int16_t>(width), static_cast<int16_t>(height)};
  return true;
}

void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::needsDecode() const { return false; }
bool ImageBlock::serialize(HalFile& file) {
  return parserImageFixture::enabled && serialization::writeString(file, imagePath) &&
         serialization::writeString(file, srcPath) && serialization::writePod(file, width) &&
         serialization::writePod(file, height);
}
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile& file) {
  if (!parserImageFixture::enabled) return nullptr;
  auto block = std::unique_ptr<ImageBlock>(new (std::nothrow) ImageBlock({}, {}, 0, 0));
  serialization::CheckedReader reader(file);
  if (!block || !reader.string(block->imagePath, 4096) || !reader.string(block->srcPath, 4096) ||
      !reader.pod(block->width) || !reader.pod(block->height) || block->imagePath.empty() ||
      block->width <= 0 || block->height <= 0) return nullptr;
  return block;
}
void Hyphenator::setPreferredLanguage(const std::string&) {}

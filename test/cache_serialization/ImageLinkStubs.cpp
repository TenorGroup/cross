#include <Epub/Page.h>
#include <Epub/blocks/ImageBlock.h>
ImageBlock::ImageBlock(const std::string& path, const std::string& src, int16_t w, int16_t h)
    : imagePath(path), srcPath(src), width(w), height(h) {}
void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::needsDecode() const { return false; }

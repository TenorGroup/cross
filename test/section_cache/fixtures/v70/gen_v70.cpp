// Writes the section cache of chapter 1 as the firmware at 8c26ea97 (section version 70) leaves it:
//   gen <dir> partial        a build suspended partway, as the reader leaves when it closes
//   gen <dir> crash <copy>   the files a build leaves when the power goes: .part, .lut.part, .checkpoint.part, .dd.part
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <filesystem>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  const std::string root = argv[1];
  const std::string mode = argv[2];
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  epub->cachePath = root;
  std::string c =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head>"
      "<body><h1>Chapter title</h1>";
  for (int i = 0; i < 160; ++i)
    c += "<p>Paragraph " + std::to_string(i) + " alpha beta gamma delta epsilon zeta eta theta iota kappa lambda.</p>";
  c += "</body></html>";
  epub->contents = c;
  ReaderRenderSpec spec;
  spec.viewportWidth = 160; spec.viewportHeight = 34; spec.embeddedStyle = false; spec.dropCapMode = 0;
  Section s(epub, 1, renderer);
  std::cout << "start=" << s.startBuild(spec) << "\n";
  std::cout << "more=" << s.buildSomeMore(6) << " pages=" << s.pageCount << "\n";
  if (mode == "crash") {
    std::cout << "park=" << s.parkBuild() << "\n";
    std::filesystem::copy(root, argv[3], std::filesystem::copy_options::recursive);
    // the build is torn down here only to leave the process; the copy is what a power cut leaves
  }
}

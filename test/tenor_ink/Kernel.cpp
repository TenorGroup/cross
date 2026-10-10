#include <Print.h>
#include <Arduino.h>
EspClass ESP;
#include "TenorInk.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <string>

class Output final : public Print {
 public:
  std::ofstream file;
  explicit Output(const char* path) : file(path, std::ios::binary) {}
  size_t write(uint8_t byte) override { file.put(byte); return file ? 1 : 0; }
  size_t write(const uint8_t* data, size_t n) override {
    file.write(reinterpret_cast<const char*>(data), n);
    return file ? n : 0;
  }
};
int main(int argc, char** argv) {
  if (argc != 6) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::string magic; int sw, sh, max;
  input >> magic >> sw >> sh >> max;
  if (magic != "P5" || max != 255 || sw <= 0 || sh <= 0) return 2;
  input.get();
  std::vector<uint8_t> gray{std::istreambuf_iterator<char>(input), {}};
  if (gray.size() != size_t(sw) * sh) return 2;
  Output out(argv[2]);
  return tenorink::writeCover(gray.data(), sw, sh, out, std::stoi(argv[3]), std::stoi(argv[4]),
                              std::stoi(argv[5]) == 4) ? 0 : 1;
}

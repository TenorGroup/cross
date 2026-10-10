#include "SimulatorImageDecode.h"
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  simulator_image::DecodedImage image;
  if (!simulator_image::decodeImageBytes(bytes.data(), bytes.size(), 1, image)) return 1;
  std::ofstream out(argv[2], std::ios::binary);
  out << "P5\n" << image.width << " " << image.height << "\n255\n";
  out.write(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
  return out ? 0 : 1;
}

#include "usp_map_cache_renderer.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream colors_file(argv[1], std::ios::binary);
  std::vector<uint16_t> pixels(usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT);
  usp_map_cache::Renderer renderer(pixels.data());
  uint16_t colors[27];
  bool first = true;
  while (colors_file.read(reinterpret_cast<char *>(colors), sizeof(colors))) {
    assert(renderer.update(colors) || !first);
    assert(!renderer.update(colors));  // Unchanged data must not request a redraw.
    std::cout.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * 2);
    first = false;
  }
  assert(colors_file.eof());
}

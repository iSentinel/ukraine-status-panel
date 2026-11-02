#include "usp_dashboard_map_renderer.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream colors_file(argv[1], std::ios::binary);
  std::vector<uint16_t> pixels(
      usp_dashboard_map_cache_data::WIDTH *
      usp_dashboard_map_cache_data::HEIGHT);
  usp_dashboard_map_renderer::Renderer renderer(pixels.data());
  uint16_t colors[27];
  bool first = true;
  while (colors_file.read(reinterpret_cast<char *>(colors), sizeof(colors))) {
    assert(renderer.update(colors) || !first);
    assert(!renderer.update(colors));
    std::cout.write(
        reinterpret_cast<const char *>(pixels.data()),
        static_cast<std::streamsize>(pixels.size() * 2));
    first = false;
  }
  assert(colors_file.eof());
}

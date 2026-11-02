#include "usp_dashboard_map_renderer.h"

#include <cassert>
#include <cstdio>
#include <vector>

using usp_dashboard_map_renderer::DirtyArea;
using usp_dashboard_map_renderer::Renderer;
using usp_dashboard_map_renderer::rgb565;

int main() {
  constexpr size_t pixels =
      usp_dashboard_map_cache_data::WIDTH *
      usp_dashboard_map_cache_data::HEIGHT;
  std::vector<uint16_t> frame(pixels);
  Renderer renderer(frame.data());
  uint16_t colors[27]{};
  colors[0] = rgb565(0x0C1828);
  colors[26] = rgb565(0x91A8B2);
  for (int label = 1; label <= 25; ++label) colors[label] = rgb565(0x1F8A4C);

  DirtyArea dirty{};
  assert(renderer.update(colors, &dirty));
  assert(dirty.changed && dirty.full);
  assert(dirty.x1 == 0 && dirty.y1 == 0);
  assert(dirty.x2 == 475 && dirty.y2 == 270);

  assert(!renderer.update(colors, &dirty));
  assert(!dirty.changed && !dirty.full);

  colors[1] = rgb565(0xDC3545);
  assert(renderer.update(colors, &dirty));
  assert(dirty.changed && !dirty.full);
  assert(dirty.x1 == 158 && dirty.y1 == 88);
  assert(dirty.x2 == 214 && dirty.y2 == 146);
  const size_t single_area =
      size_t(dirty.x2 - dirty.x1 + 1) * (dirty.y2 - dirty.y1 + 1);
  assert(single_area == 3363U);

  colors[2] = rgb565(0xDC3545);
  assert(renderer.update(colors, &dirty));
  assert(dirty.changed && !dirty.full);
  assert(dirty.x1 == 77 && dirty.y1 == 21);
  assert(dirty.x2 == 129 && dirty.y2 == 72);

  colors[0] = rgb565(0x010203);
  assert(renderer.update(colors, &dirty));
  assert(dirty.changed && dirty.full);

  std::printf(
      "DASHBOARD_MAP_DIRTY_AREA=PASS initial_full=1 noop=1 "
      "single_bbox_pixels=%zu background_full=1\n",
      single_area);
  return 0;
}

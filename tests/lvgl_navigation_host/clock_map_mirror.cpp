#include "usp_dashboard_map_cache.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

uint32_t navigation_test_tick = 1000;

lv_display_t *prepare_display(uint16_t *pixels, size_t bytes) {
  lv_init();
  auto *display = lv_display_create(1024, 600);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, pixels, nullptr, bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(
      display, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
        lv_display_flush_ready(d);
      });
  return display;
}

struct Invalidations {
  std::vector<lv_area_t> areas;
};

void record_invalidation(lv_event_t *event) {
  auto *state = static_cast<Invalidations *>(lv_event_get_user_data(event));
  const auto *area = static_cast<const lv_area_t *>(lv_event_get_param(event));
  assert(state != nullptr && area != nullptr);
  state->areas.push_back(*area);
}

void require_region_areas(const Invalidations &state) {
  assert(state.areas.size() == 2U);
  for (const auto &area : state.areas) {
    assert(lv_area_get_width(&area) == 57);
    assert(lv_area_get_height(&area) == 59);
    assert(lv_area_get_size(&area) == 3363);
  }
}

int main() {
  uint16_t pixels[1024 * 20]{};
  auto *display = prepare_display(pixels, sizeof(pixels));
  auto *screen = lv_display_get_screen_active(display);
  auto *home = lv_obj_create(screen);
  lv_obj_set_size(home, 520, 400);
  auto *mirror = lv_image_create(screen);
  auto &cache = usp_dashboard_map_cache::Cache::instance();
  int states[25]{};
  bool changed = true;

  assert(!cache.mirror_to(mirror, states, 0, &changed));
  assert(!changed && map_allocation_count == 0);
  assert(cache.refresh(home, nullptr, 0, states, 0));
  assert(map_allocation_count == 1 && map_allocation_bytes == 257992);
  assert(cache.mirror_to(mirror, states, 0, &changed) && !changed);
  auto *image = lv_obj_get_child(home, 0);
  const auto *descriptor =
      static_cast<const lv_image_dsc_t *>(lv_image_get_src(image));
  assert(lv_image_get_src(mirror) == descriptor);
  lv_obj_update_layout(screen);
  lv_refr_now(display);

  Invalidations invalidations;
  lv_display_add_event_cb(
      display, record_invalidation, LV_EVENT_INVALIDATE_AREA, &invalidations);
  std::vector<uint8_t> before(
      descriptor->data, descriptor->data + descriptor->data_size);
  states[0] = 4;
  assert(cache.mirror_to(mirror, states, 0, &changed) && changed);
  const int changed_pixels = std::memcmp(
      before.data(), descriptor->data, descriptor->data_size);
  assert(changed_pixels != 0);
  assert(map_allocation_count == 1);
  assert(lv_image_get_src(mirror) == lv_image_get_src(image));
  require_region_areas(invalidations);

  invalidations.areas.clear();
  assert(cache.mirror_to(mirror, states, 0, &changed) && !changed);
  assert(invalidations.areas.empty());
  lv_display_delete(display);
  lv_deinit();
  std::puts(
      "CLOCK_MAP_MIRROR=PASS shared_pixels one_allocation "
      "single_region_partial=2x3363 unchanged_noop");
}

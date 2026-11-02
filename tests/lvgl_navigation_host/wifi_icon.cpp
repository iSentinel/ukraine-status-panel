#include "usp_wifi_icon.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static uint16_t pixels[80 * 64];
static uint16_t previous[80 * 64];
static uint16_t visible[80 * 64];
static unsigned flushes = 0;

static void flush_cb(lv_display_t *display, const lv_area_t *, uint8_t *) {
  ++flushes;
  lv_display_flush_ready(display);
}

static void check_boundaries() {
  using usp_wifi_icon::strength;
  assert(strength(false, -40) == 0);
  for (int rssi : {-127, -101, 0, 50}) assert(strength(true, rssi) == 1);
  for (int rssi : {-100, -71}) assert(strength(true, rssi) == 2);
  for (int rssi : {-70, -61}) assert(strength(true, rssi) == 3);
  for (int rssi : {-60, -1}) assert(strength(true, rssi) == 4);
}

static lv_obj_t *make_badge(lv_obj_t *screen) {
  auto *badge = lv_label_create(screen);
  lv_obj_set_pos(badge, 7, 8);
  lv_obj_set_size(badge, 66, 44);
  lv_obj_set_style_pad_all(badge, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(badge, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(badge, lv_color_hex(0x122B46), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(badge, lv_color_hex(0x263D57), LV_PART_MAIN);
  lv_obj_set_style_text_color(badge, lv_color_hex(0x38BDF8), LV_PART_MAIN);
  lv_label_set_text(badge, "WiFi");
  return badge;
}

static lv_obj_t *make_bar(lv_obj_t *screen, int x, int y, int height) {
  auto *bar = lv_obj_create(screen);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, 4, height);
  lv_obj_set_style_radius(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x64748B), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  return bar;
}

static unsigned changed_pixels() {
  unsigned changed = 0;
  for (size_t i = 0; i < 80U * 64U; ++i) {
    if (pixels[i] != previous[i]) ++changed;
  }
  std::memcpy(previous, pixels, sizeof(pixels));
  return changed;
}

static void check_colors(lv_obj_t **bars, uint8_t level) {
  for (uint8_t index = 0; index < 4; ++index) {
    const auto actual = lv_obj_get_style_bg_color(bars[index], LV_PART_MAIN);
    assert(lv_color_eq(actual, lv_color_hex(usp_wifi_icon::bar_color(level, index))));
    assert(lv_obj_get_style_bg_opa(bars[index], LV_PART_MAIN) == LV_OPA_COVER);
  }
}

static void repair_foreground(lv_obj_t *badge, lv_obj_t **bars) {
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(badge);
  for (unsigned index = 0; index < 4; ++index) {
    lv_obj_clear_flag(bars[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(bars[index]);
  }
}

static lv_obj_t *make_cover(lv_obj_t *screen) {
  auto *cover = lv_obj_create(screen);
  lv_obj_set_pos(cover, 7, 8);
  lv_obj_set_size(cover, 66, 44);
  lv_obj_set_style_bg_color(cover, lv_color_hex(0x7F1D1D), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
  return cover;
}

int main() {
  check_boundaries();
  lv_init();
  auto *display = lv_display_create(80, 64);
  lv_display_set_default(display);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush_cb);
  auto *screen = lv_display_get_screen_active(display);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x08111F), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  auto *badge = make_badge(screen);
  lv_obj_t *bars[] = {
      make_bar(screen, 46, 36, 8), make_bar(screen, 53, 32, 12),
      make_bar(screen, 60, 28, 16), make_bar(screen, 67, 24, 20)};
  for (auto *bar : bars) assert(lv_obj_get_parent(bar) == screen);
  assert(lv_obj_get_parent(badge) == screen);

  usp_wifi_icon::Icon icon;
  assert(icon.update(bars[0], bars[1], bars[2], bars[3], true, -55));
  check_colors(bars, 4);
  lv_obj_update_layout(screen);
  lv_refr_now(display);
  std::memcpy(previous, pixels, sizeof(pixels));
  std::memcpy(visible, pixels, sizeof(pixels));

  make_cover(screen);
  lv_refr_now(display);
  const unsigned covered_pixels = changed_pixels();
  assert(covered_pixels > 1000);

  repair_foreground(badge, bars);
  lv_refr_now(display);
  const unsigned restored_pixels = changed_pixels();
  assert(restored_pixels > 1000);
  unsigned interior_mismatches = 0;
  for (unsigned y = 14; y < 47; ++y) {
    for (unsigned x = 13; x < 68; ++x) {
      const size_t index = y * 80U + x;
      if (visible[index] != pixels[index]) ++interior_mismatches;
    }
  }
  assert(interior_mismatches == 0);

  unsigned minimum_transition = 80U * 64U;
  for (const auto &sample : {-127, -85, -65, -55}) {
    assert(icon.update(bars[0], bars[1], bars[2], bars[3], true, sample));
    lv_refr_now(display);
    const unsigned changed = changed_pixels();
    if (changed < minimum_transition) minimum_transition = changed;
    assert(changed > 10);
  }
  std::printf("WIFI_ICON_LVGL_NATIVE=PASS root_siblings foreground_restore "
              "covered=%u restored=%u interior_mismatches=%u "
              "minimum_transition=%u flushes=%u\n",
              covered_pixels, restored_pixels, interior_mismatches,
              minimum_transition, flushes);
  lv_display_delete(display);
  lv_deinit();
  return 0;
}

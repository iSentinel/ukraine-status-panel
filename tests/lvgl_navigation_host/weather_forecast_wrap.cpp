#include "lvgl.h"
#include "weather-wrap-fixture.h"
#include <cstdio>
#include <cstring>

/** Use the exact ESPHome advance and ink bounds from generated font data. */
bool glyph(const lv_font_t *, lv_font_glyph_dsc_t *dsc,
           uint32_t code, uint32_t) {
  for (const auto &g : GLYPHS) {
    if (g.code != code) continue;
    dsc->adv_w = g.advance;
    dsc->ofs_x = g.x;
    dsc->ofs_y = FONT_HEIGHT - g.h - g.y - FONT_BASELINE;
    dsc->box_w = g.w;
    dsc->box_h = g.h;
    dsc->is_placeholder = false;
    dsc->format = LV_FONT_GLYPH_FORMAT_A4;
    return true;
  }
  return false;
}

/** Measure all real descriptions through LVGL's own wrapping algorithm. */
int main() {
  lv_init();
  lv_font_t font{};
  font.line_height = FONT_HEIGHT;
  font.base_line = FONT_BASELINE;
  font.get_glyph_dsc = glyph;
  unsigned failures = 0, count = 0;
  int maximum = 0;
  for (const auto &r : RECTS) {
    for (const auto &s : SAMPLES) {
      lv_point_t size{};
      lv_text_get_size(&size, s.text, &font, r.letter_space, r.line_space,
                       r.width, LV_TEXT_FLAG_NONE);
      const bool fits = size.y <= r.height;
      failures += !fits;
      ++count;
      if (size.y > maximum) maximum = size.y;
      std::printf("%d\t%u\t%u\t%d\t%d\t%d\t%s\n",
          r.card, s.code, s.language, size.x, size.y, r.height, s.text);
    }
  }
  std::fprintf(stderr, "WEATHER_FORECAST_WRAP=%s measurements=%u overflow=%u max_height=%d line_height=%d\n",
      failures ? "FAIL" : "PASS", count, failures, maximum, FONT_HEIGHT);
  return failures ? 1 : 0;
}

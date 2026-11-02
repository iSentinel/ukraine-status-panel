#include "usp_alarm_days_ui.h"
#include <cassert>
#include <cstdio>
#include <cstring>

/** Check the visible selected state and seven/five/two-day summaries. */
static void check_language(uint8_t language, lv_obj_t *const buttons[3],
                           lv_obj_t *summary) {
  for (unsigned preset = 0; preset < 3; ++preset) {
    usp_alarm_days_ui::render(buttons, summary, language, preset);
    unsigned checked = 0;
    for (unsigned index = 0; index < 3; ++index) {
      if (lv_obj_has_state(buttons[index], LV_STATE_CHECKED))
        checked |= 1U << index;
      const auto color = lv_color_to_u32(
          lv_obj_get_style_bg_color(buttons[index], LV_PART_MAIN));
      assert((color & 0xFFFFFFU) == (index == preset ? 0x1D4ED8U : 0x14253AU));
      assert(std::strcmp(lv_label_get_text(lv_obj_get_child(buttons[index], 0)),
                         usp_alarm_ui::preset(language, index)) == 0);
    }
    assert(checked == (1U << preset));
    const char *visible = lv_label_get_text(summary);
    assert(std::strcmp(visible, usp_alarm_ui::summary(language, preset)) == 0);
    unsigned separators = 0;
    for (const char *at = visible; (at = std::strstr(at, " · ")); at += 4)
      ++separators;
    assert(separators + 1 == (preset == 0 ? 7U : preset == 1 ? 5U : 2U));
    assert(usp_alarm_ui::mask(preset) ==
           (preset == 0 ? 127U : preset == 1 ? 62U : 65U));
  }
}

int main() {
  lv_init();
  auto *display = lv_display_create(1024, 600);
  lv_obj_t *buttons[3];
  auto *summary = lv_label_create(lv_screen_active());
  for (auto &button : buttons) {
    button = lv_button_create(lv_screen_active());
    lv_obj_set_size(button, 74, 54);
    lv_label_create(button);
  }
  for (uint8_t language = 0; language < 3; ++language)
    check_language(language, buttons, summary);
  usp_alarm_days_ui::render(buttons, summary, 0, 99);
  assert(lv_obj_has_state(buttons[0], LV_STATE_CHECKED));
  lv_display_delete(display);
  lv_deinit();
  std::puts("ALARM_DAYS_LVGL=PASS three_languages seven_five_two distinct_state");
}

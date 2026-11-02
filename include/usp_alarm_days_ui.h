#pragma once
#include "lvgl.h"
#include "usp_alarm_ui.h"

namespace usp_alarm_days_ui {

/** Keep LVGL part/state flags explicit for C++20 and newer compilers. */
inline lv_style_selector_t selector(lv_state_t state) {
  return static_cast<lv_style_selector_t>(LV_PART_MAIN) |
         static_cast<lv_style_selector_t>(state);
}

/** Make the selected preset distinct in color, border and checked state. */
inline void style(lv_obj_t *button, const char *caption, bool selected) {
  if (selected) lv_obj_add_state(button, LV_STATE_CHECKED);
  else lv_obj_remove_state(button, LV_STATE_CHECKED);
  const auto background = lv_color_hex(selected ? 0x1D4ED8 : 0x14253A);
  const auto border = lv_color_hex(selected ? 0xBFDBFE : 0x52677D);
  const lv_style_selector_t states[] = {
      selector(LV_STATE_DEFAULT), selector(LV_STATE_CHECKED)};
  for (auto state : states) {
    lv_obj_set_style_bg_color(button, background, state);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, state);
    lv_obj_set_style_border_color(button, border, state);
    lv_obj_set_style_border_width(button, selected ? 2 : 1, state);
  }
  lv_obj_set_style_bg_color(button, lv_color_hex(0x263D57),
                           selector(LV_STATE_PRESSED));
  auto *label = lv_obj_get_child(button, 0);
  lv_label_set_text(label, caption);
  lv_obj_set_style_text_color(label,
      lv_color_hex(selected ? 0xFFFFFF : 0xA9B8C8), LV_PART_MAIN);
}

/** Show actual day names while leaving the saved schedule untouched. */
inline void render(lv_obj_t *const buttons[3], lv_obj_t *summary,
                   uint8_t language, unsigned selected) {
  if (selected >= 3) selected = 0;
  for (unsigned index = 0; index < 3; ++index)
    style(buttons[index], usp_alarm_ui::preset(language, index),
          index == selected);
  lv_label_set_text(summary, usp_alarm_ui::summary(language, selected));
}

}  // namespace usp_alarm_days_ui

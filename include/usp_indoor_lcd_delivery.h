#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"

namespace usp_indoor_lcd_delivery {

template <typename PrimaryTarget, typename FullscreenTarget>
inline void render(const std::string &text, uint32_t color,
                   PrimaryTarget &&primary, FullscreenTarget &&fullscreen) {
  lv_label_set_text(primary, text.c_str());
  lv_obj_set_style_text_color(primary, lv_color_hex(color), LV_PART_MAIN);
  lv_label_set_text(fullscreen, text.c_str());
  lv_obj_set_style_text_color(fullscreen, lv_color_hex(color), LV_PART_MAIN);
}

}  // namespace usp_indoor_lcd_delivery

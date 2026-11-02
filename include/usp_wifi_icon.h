#pragma once

#include "lvgl.h"
#include <cstdint>

namespace usp_wifi_icon {

/** Zero means disconnected; one means connected before a valid signal sample. */
inline uint8_t strength(bool connected, int rssi) {
  if (!connected) return 0;
  if (rssi < -100 || rssi >= 0) return 1;
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  return 2;
}

/** Four ordinary LVGL rectangles are deliberately used for physical reliability. */
inline uint32_t bar_color(uint8_t level, uint8_t index) {
  if (level == 0) return 0xEF4444;
  return index < level ? 0x38BDF8 : 0x334155;
}

/** Main-task-only controller for the persistent four-bar Wi-Fi indicator. */
class Icon {
 public:
  /** Recolor ordinary scene objects only when connection quality changes. */
  bool update(lv_obj_t *bar1, lv_obj_t *bar2, lv_obj_t *bar3,
              lv_obj_t *bar4, bool connected, int rssi) {
    lv_obj_t *bars[] = {bar1, bar2, bar3, bar4};
    for (auto *bar : bars) {
      if (bar == nullptr) return false;
    }
    const auto next = strength(connected, rssi);
    if (next == level_) return false;
    level_ = next;
    for (uint8_t index = 0; index < 4; ++index) {
      lv_obj_set_style_bg_color(
          bars[index], lv_color_hex(bar_color(level_, index)), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(bars[index], LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_invalidate(bars[index]);
    }
    return true;
  }

  uint8_t level() const { return level_; }

 private:
  uint8_t level_{255};
};

/** One stable controller for the persistent header indicator. */
inline Icon &controller() {
  static Icon instance;
  return instance;
}

}  // namespace usp_wifi_icon

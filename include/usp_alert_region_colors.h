#pragma once

#include <cstdint>

#include "usp_alert_palette_0_115_114.h"

namespace usp_alert_region_colors {

inline uint32_t color_hex(int state, uint8_t palette) {
  state = static_cast<uint8_t>(state);
  uint32_t color = 0x34424E;
  if (state == 1) color = 0x1F8A4C;
  else if (state == 2 || state == 8) color = 0xF5BE26;
  else if (state == 16) color = 0xF97316;
  else if (state == 4) color = 0xDC3545;
  else if (state == 32) color = 0x9C27B0;
  else if (state == 64) color = 0xA855F7;
  else if (state == 128) color = 0xF97316;
  return usp_alert_palette::color_hex(color, palette);
}

}  // namespace usp_alert_region_colors

#pragma once

#include <cstdint>

namespace usp_alert_palette {

// Rendering-only color modes. Alert state/source semantics never change.
enum : uint8_t {
  STANDARD = 0,
  ACCESSIBLE = 1,
  MONO = 2,
};

inline uint8_t sanitize_mode(uint8_t mode) {
  return mode <= static_cast<uint8_t>(MONO)
      ? mode : static_cast<uint8_t>(STANDARD);
}

inline uint32_t color_hex(uint32_t standard, uint8_t mode) {
  mode = sanitize_mode(mode);
  if (mode == STANDARD) return standard;

  if (mode == ACCESSIBLE) {
    switch (standard) {
      case 0x34424E: return 0x56616D;  // no data
      case 0x1F8A4C: return 0x0072B2;  // clear: blue
      case 0xF5BE26: return 0xF0E442;  // UAV: yellow
      case 0xF97316: return 0xE69F00;  // partial: orange
      case 0xDC3545: return 0xD55E00;  // alert: vermillion
      case 0xA855F7: return 0xCC79A7;  // ballistic: purple-pink
      case 0x9C27B0: return 0xCC79A7;  // fallback/event: purple-pink
      default: return standard;
    }
  }

  switch (standard) {
    case 0x34424E: return 0x4D4D4D;
    case 0x1F8A4C: return 0xA6A6A6;
    case 0xF5BE26: return 0xD9D9D9;
    case 0xF97316: return 0xBFBFBF;
    case 0xDC3545: return 0xF2F2F2;
    case 0xA855F7: return 0xE0E0E0;  // ballistic: bright gray
    case 0x9C27B0: return 0x737373;
    default: return standard;
  }
}

}  // namespace usp_alert_palette

#pragma once

#include "esphome/components/usp_neptun/neptun_threat_point.h"
#include "usp_alert_palette_0_115_114.h"

#include <cmath>
#include <cstdint>

namespace usp_threat_icons_ui {

using esphome::usp_neptun::ThreatPointKind;
using esphome::usp_neptun::ThreatPointQuality;

inline const char *label(ThreatPointKind kind, ThreatPointQuality quality) {
  const bool approximate = quality == ThreatPointQuality::APPROXIMATE;
  switch (kind) {
    case ThreatPointKind::UAV: return approximate ? "~U" : "U";
    case ThreatPointKind::FPV: return approximate ? "~F" : "F";
    case ThreatPointKind::RECON: return approximate ? "~R" : "R";
    case ThreatPointKind::MISSILE: return approximate ? "~M" : "M";
    case ThreatPointKind::BALLISTIC: return approximate ? "~B" : "B";
    case ThreatPointKind::KAB: return approximate ? "~K" : "K";
    case ThreatPointKind::MIG31K: return approximate ? "~J" : "J";
    default: return "?";
  }
}

inline uint32_t base_color(ThreatPointKind kind) {
  switch (kind) {
    case ThreatPointKind::UAV:
    case ThreatPointKind::FPV:
    case ThreatPointKind::RECON:
      return 0xF5BE26;
    case ThreatPointKind::MISSILE:
      return 0xDC3545;
    case ThreatPointKind::BALLISTIC:
      return 0xA855F7;
    case ThreatPointKind::KAB:
    case ThreatPointKind::MIG31K:
      return 0xF97316;
    default:
      return 0x34424E;
  }
}

inline uint32_t color(ThreatPointKind kind, uint8_t palette) {
  return usp_alert_palette::color_hex(base_color(kind), palette);
}

inline void heading_delta(int16_t heading_deg, int16_t length,
                          int16_t *dx, int16_t *dy) {
  if (dx == nullptr || dy == nullptr || heading_deg < 0 || heading_deg >= 360) {
    if (dx != nullptr) *dx = 0;
    if (dy != nullptr) *dy = 0;
    return;
  }
  const float radians = static_cast<float>(heading_deg) * 0.01745329252f;
  *dx = static_cast<int16_t>(std::lround(std::sin(radians) * length));
  *dy = static_cast<int16_t>(std::lround(-std::cos(radians) * length));
}

}  // namespace usp_threat_icons_ui

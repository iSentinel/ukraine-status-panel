#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::usp_neptun {

static constexpr size_t THREAT_POINT_CAPACITY = 12;
static constexpr uint32_t THREAT_POINT_TTL_SECONDS = 20U * 60U;

enum class ThreatPointKind : uint8_t {
  UAV,
  FPV,
  RECON,
  MISSILE,
  BALLISTIC,
  KAB,
  MIG31K,
  UNSUPPORTED,
};

enum class ThreatPointQuality : uint8_t {
  CONFIRMED,
  APPROXIMATE,
  UNKNOWN,
};

struct ThreatPoint {
  uint32_t id_hash{0};
  uint32_t updated_epoch{0};
  float latitude{0.0f};
  float longitude{0.0f};
  int16_t heading_deg{-1};
  ThreatPointKind kind{ThreatPointKind::UNSUPPORTED};
  ThreatPointQuality quality{ThreatPointQuality::UNKNOWN};
};

inline uint32_t threat_id_hash(const char *value) {
  uint32_t hash = 2166136261U;
  if (value == nullptr) return 0;
  for (const auto *cursor = reinterpret_cast<const uint8_t *>(value);
       *cursor != 0; ++cursor) {
    hash ^= *cursor;
    hash *= 16777619U;
  }
  return hash;
}

inline ThreatPointKind threat_point_kind(const char *value) {
  if (value == nullptr) return ThreatPointKind::UNSUPPORTED;
  if (std::strcmp(value, "uav") == 0) return ThreatPointKind::UAV;
  if (std::strcmp(value, "fpv") == 0) return ThreatPointKind::FPV;
  if (std::strcmp(value, "recon") == 0) return ThreatPointKind::RECON;
  if (std::strcmp(value, "missile") == 0) return ThreatPointKind::MISSILE;
  if (std::strcmp(value, "ballistic") == 0) return ThreatPointKind::BALLISTIC;
  if (std::strcmp(value, "kab") == 0) return ThreatPointKind::KAB;
  if (std::strcmp(value, "mig31k") == 0) return ThreatPointKind::MIG31K;
  return ThreatPointKind::UNSUPPORTED;
}

inline ThreatPointQuality threat_point_quality(const char *value) {
  if (value == nullptr) return ThreatPointQuality::UNKNOWN;
  if (std::strcmp(value, "confirmed") == 0)
    return ThreatPointQuality::CONFIRMED;
  if (std::strcmp(value, "approx") == 0)
    return ThreatPointQuality::APPROXIMATE;
  return ThreatPointQuality::UNKNOWN;
}

inline bool parse_digits(const char *value, size_t offset, size_t count,
                         int *result) {
  int parsed = 0;
  for (size_t index = 0; index < count; ++index) {
    const char c = value[offset + index];
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10 + (c - '0');
  }
  *result = parsed;
  return true;
}

inline int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153U * (month + (month > 2 ? -3 : 9)) + 2U) / 5U + day - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline bool valid_calendar(int year, int month, int day, int hour,
                           int minute, int second) {
  if (year < 1970 || year > 2105 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 60) return false;
  static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
  int limit = days[month - 1];
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (month == 2 && leap) ++limit;
  return day <= limit;
}

inline bool parse_iso8601_utc(const char *value, uint32_t *epoch) {
  if (value == nullptr || epoch == nullptr || std::strlen(value) < 20) return false;
  if (value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
      value[13] != ':' || value[16] != ':') return false;
  int year, month, day, hour, minute, second;
  if (!parse_digits(value, 0, 4, &year) || !parse_digits(value, 5, 2, &month) ||
      !parse_digits(value, 8, 2, &day) || !parse_digits(value, 11, 2, &hour) ||
      !parse_digits(value, 14, 2, &minute) || !parse_digits(value, 17, 2, &second))
    return false;
  if (!valid_calendar(year, month, day, hour, minute, second)) return false;
  const char *zone = value + 19;
  if (*zone == '.') {
    do { ++zone; } while (*zone >= '0' && *zone <= '9');
  }
  if (*zone != 'Z' || zone[1] != '\0') return false;
  const int64_t days = days_from_civil(year, month, day);
  const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + std::min(second, 59);
  if (seconds < 0 || seconds > UINT32_MAX) return false;
  *epoch = static_cast<uint32_t>(seconds);
  return true;
}

inline bool threat_point_fresh(uint32_t server_epoch, uint32_t updated_epoch) {
  return server_epoch != 0 && updated_epoch != 0 && updated_epoch <= server_epoch &&
         server_epoch - updated_epoch <= THREAT_POINT_TTL_SECONDS;
}

inline bool threat_source_point(float latitude, float longitude,
                                float *x, float *y) {
  static constexpr float X_SCALE = 26.140814659f;
  static constexpr float Y_SCALE = 39.444409533f;
  static constexpr float X_OFFSET = -523.666171090f;
  static constexpr float Y_OFFSET = 2073.689917376f;
  if (!std::isfinite(latitude) || !std::isfinite(longitude)) return false;
  const float px = longitude * X_SCALE + X_OFFSET;
  const float py = Y_OFFSET - latitude * Y_SCALE;
  if (px < 0.0f || py < 0.0f || px >= 580.0f || py >= 330.0f) return false;
  *x = px;
  *y = py;
  return true;
}

inline bool threat_fullscreen_point(float latitude, float longitude,
                                    int16_t *x, int16_t *y) {
  float source_x = 0.0f, source_y = 0.0f;
  if (x == nullptr || y == nullptr ||
      !threat_source_point(latitude, longitude, &source_x, &source_y))
    return false;
  *x = static_cast<int16_t>(std::lround(63.0f + source_x * 1.48f));
  *y = static_cast<int16_t>(std::lround(12.0f + source_y * 1.48f));
  return true;
}

}  // namespace esphome::usp_neptun

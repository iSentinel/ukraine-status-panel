#pragma once

#include <cstddef>
#include <cstdint>

namespace usp_room_chart {

// 372 points cover a leap-year daily aggregate window. The legacy 24h source
// still contains at most 288 five-minute samples, so its rendered data is
// unchanged while sharing the same PSRAM workspace.
static constexpr size_t MAX_POINTS = 372;
static constexpr size_t MAX_SEGMENTS = 12;

struct Point {
  int16_t x{0};
  int16_t y{0};
};

struct Segment {
  uint16_t start{0};
  uint16_t count{0};
};

struct Model {
  Point temperature[MAX_POINTS]{};
  Point humidity[MAX_POINTS]{};
  Segment segments[MAX_SEGMENTS]{};
  uint16_t point_count{0};
  uint8_t segment_count{0};
  uint8_t dropped_runs{0};
  uint32_t valid_count{0};
  uint32_t gap_count{0};
  uint32_t first_epoch{0};
  uint32_t last_epoch{0};
  float temperature_min{0.0f};
  float temperature_max{0.0f};
};

}  // namespace usp_room_chart

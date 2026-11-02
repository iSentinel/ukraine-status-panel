#pragma once

#include "usp_room_chart_model.h"
#include "usp_room_period.h"
#include "esphome/components/usp_room_aggregate_history/usp_room_aggregate_history.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace usp_room_period_chart {

using esphome::usp_room_aggregate_history::Aggregate;
using esphome::usp_room_aggregate_history::RoomAggregateHistory;
using usp_room_period::Period;

struct Spec {
  bool hourly{false};
  uint16_t buckets{0};
};

inline Spec spec(Period period) {
  switch (period) {
    case Period::DAYS_7:
      return {true, 7U * 24U};
    case Period::DAYS_30:
      return {false, 30U};
    case Period::YEAR_1:
      return {false, 366U};
    case Period::HOURS_24:
      break;
  }
  return {};
}

inline const Aggregate *sample(const RoomAggregateHistory &history,
                               bool hourly, size_t index) {
  return hourly ? history.hourly_sample(index) : history.daily_sample(index);
}

inline size_t count(const RoomAggregateHistory &history, bool hourly) {
  return hourly ? history.hourly_count() : history.daily_count();
}

inline uint32_t resolve_end(const RoomAggregateHistory &history,
                            const Spec &range, uint32_t requested) {
  if (requested != 0) return requested;
  const size_t total = count(history, range.hourly);
  for (size_t index = total; index > 0; --index) {
    const Aggregate *point = sample(history, range.hourly, index - 1U);
    if (point != nullptr && (point->valid_count != 0 || point->gap_count != 0))
      return point->bucket;
  }
  return 0;
}

inline uint32_t window_start(uint32_t end, uint16_t buckets) {
  if (end == 0 || buckets == 0) return 0;
  const uint32_t width = static_cast<uint32_t>(buckets - 1U);
  return end > width ? end - width : 0U;
}

inline bool in_window(const Aggregate *point, uint32_t start, uint32_t end) {
  return point != nullptr && point->bucket >= start && point->bucket <= end;
}

inline bool usable(const Aggregate *point) {
  return point != nullptr && point->valid_count != 0;
}

inline float average_temperature(const Aggregate &point) {
  return static_cast<float>(point.temperature_sum_centi) /
      static_cast<float>(point.valid_count) / 100.0f;
}

inline float average_humidity(const Aggregate &point) {
  return static_cast<float>(point.humidity_sum_centi) /
      static_cast<float>(point.valid_count) / 100.0f;
}

inline int16_t scale_x(uint32_t bucket, uint32_t start, uint32_t end,
                       int16_t width) {
  if (end <= start || width <= 0) return 0;
  const uint64_t elapsed = bucket > start ? bucket - start : 0U;
  const uint64_t span = end - start;
  return static_cast<int16_t>(std::min<uint64_t>(
      static_cast<uint64_t>(width),
      elapsed * static_cast<uint64_t>(width) / span));
}

inline int16_t scale_y(float value, float minimum, float maximum,
                       int16_t height) {
  if (height <= 0 || maximum <= minimum) return height / 2;
  const float ratio = std::clamp(
      (maximum - value) / (maximum - minimum), 0.0f, 1.0f);
  return static_cast<int16_t>(std::lround(ratio * height));
}

inline void summarize(const RoomAggregateHistory &history, const Spec &range,
                      uint32_t start, uint32_t end,
                      usp_room_chart::Model *model) {
  float low = std::numeric_limits<float>::infinity();
  float high = -std::numeric_limits<float>::infinity();
  const size_t total = count(history, range.hourly);
  for (size_t index = 0; index < total; ++index) {
    const Aggregate *point = sample(history, range.hourly, index);
    if (!in_window(point, start, end)) continue;
    model->valid_count += point->valid_count;
    model->gap_count += point->gap_count;
    if (!usable(point)) continue;
    low = std::min(low, point->temperature_min_centi / 100.0f);
    high = std::max(high, point->temperature_max_centi / 100.0f);
  }
  if (!std::isfinite(low) || !std::isfinite(high)) return;
  const float padding = std::max(0.5f, (high - low) * 0.10f);
  model->temperature_min = low - padding;
  model->temperature_max = high + padding;
  if (model->temperature_max - model->temperature_min >= 2.0f) return;
  const float middle = (low + high) * 0.5f;
  model->temperature_min = middle - 1.0f;
  model->temperature_max = middle + 1.0f;
}

inline size_t count_runs(const RoomAggregateHistory &history, const Spec &range,
                         uint32_t start, uint32_t end) {
  bool open = false;
  uint32_t previous = 0;
  size_t runs = 0;
  const size_t total = count(history, range.hourly);
  for (size_t index = 0; index < total; ++index) {
    const Aggregate *point = sample(history, range.hourly, index);
    if (!in_window(point, start, end)) continue;
    if (!usable(point)) {
      open = false;
      previous = 0;
      continue;
    }
    if (!open || previous == 0 || point->bucket != previous + 1U) {
      ++runs;
      open = true;
    }
    previous = point->bucket;
  }
  return runs;
}

inline bool start_period_segment(usp_room_chart::Model *model, size_t run,
                          size_t skipped) {
  if (run <= skipped || model->segment_count >= usp_room_chart::MAX_SEGMENTS)
    return false;
  auto &segment = model->segments[model->segment_count++];
  segment.start = model->point_count;
  return true;
}

inline void append_point(usp_room_chart::Model *model,
                         const Aggregate &aggregate, uint32_t start,
                         uint32_t end, int16_t width, int16_t temp_height,
                         int16_t humidity_height) {
  if (model->point_count >= usp_room_chart::MAX_POINTS) return;
  const size_t index = model->point_count++;
  const int16_t x = scale_x(aggregate.bucket, start, end, width);
  model->temperature[index] = {
      x, scale_y(average_temperature(aggregate), model->temperature_min,
                 model->temperature_max, temp_height)};
  model->humidity[index] = {
      x, scale_y(average_humidity(aggregate), 0.0f, 100.0f,
                 humidity_height)};
  ++model->segments[model->segment_count - 1U].count;
}

inline void collect(const RoomAggregateHistory &history, const Spec &range,
                    uint32_t start, uint32_t end, size_t skipped,
                    int16_t width, int16_t temp_height, int16_t humidity_height,
                    usp_room_chart::Model *model) {
  bool open = false;
  bool keep = false;
  uint32_t previous = 0;
  size_t run = 0;
  const size_t total = count(history, range.hourly);
  for (size_t index = 0; index < total; ++index) {
    const Aggregate *point = sample(history, range.hourly, index);
    if (!in_window(point, start, end)) continue;
    if (!usable(point)) {
      open = false;
      keep = false;
      previous = 0;
      continue;
    }
    if (!open || previous == 0 || point->bucket != previous + 1U) {
      ++run;
      open = true;
      keep = start_period_segment(model, run, skipped);
    }
    previous = point->bucket;
    if (keep) append_point(model, *point, start, end, width,
                           temp_height, humidity_height);
  }
}

inline void build(const RoomAggregateHistory &history, Period period,
                  uint32_t requested_end, int16_t width, int16_t temp_height,
                  int16_t humidity_height, usp_room_chart::Model *model) {
  *model = usp_room_chart::Model{};
  const Spec range = spec(period);
  if (!history.active() || range.buckets == 0) return;
  const uint32_t end = resolve_end(history, range, requested_end);
  if (end == 0) return;
  const uint32_t start = window_start(end, range.buckets);
  summarize(history, range, start, end, model);
  const size_t runs = count_runs(history, range, start, end);
  const size_t skipped = runs > usp_room_chart::MAX_SEGMENTS
      ? runs - usp_room_chart::MAX_SEGMENTS : 0U;
  model->dropped_runs = static_cast<uint8_t>(std::min<size_t>(skipped, 255U));
  collect(history, range, start, end, skipped, width, temp_height,
          humidity_height, model);
}

}  // namespace usp_room_period_chart

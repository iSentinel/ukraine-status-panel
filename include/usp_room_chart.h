#pragma once

#include "usp_room_chart_model.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/usp_room_history/room_history.h"
#include "esp_heap_caps.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace usp_room_chart {

using esphome::usp_room_history::ROOM_HISTORY_BUCKET_SECONDS;
using esphome::usp_room_history::ROOM_SAMPLE_VALID;
using esphome::usp_room_history::RoomHistory;
using esphome::usp_room_history::RoomSample;

static constexpr uint32_t WINDOW_SECONDS = 24U * 60U * 60U;
static constexpr uint32_t BREAK_SECONDS = ROOM_HISTORY_BUCKET_SECONDS * 2U;

struct LvglBuffers {
  lv_point_precise_t temperature[MAX_POINTS]{};
  lv_point_precise_t humidity[MAX_POINTS]{};
  lv_point_precise_t temperature_single[MAX_SEGMENTS][2]{};
  lv_point_precise_t humidity_single[MAX_SEGMENTS][2]{};
};

struct Workspace {
  Model model{};
  LvglBuffers buffers{};
};

struct Window {
  uint32_t start{0};
  uint32_t end{0};
};

inline bool valid(const RoomSample *sample) {
  return sample != nullptr && (sample->flags & ROOM_SAMPLE_VALID) != 0;
}

inline uint32_t resolve_end(const RoomHistory &history, uint32_t requested) {
  if (requested != 0) return requested;
  for (size_t index = history.count(); index > 0; --index) {
    const auto *sample = history.sample(index - 1);
    if (sample != nullptr && sample->epoch != 0) return sample->epoch;
  }
  return 0;
}

inline Window window(const RoomHistory &history, uint32_t requested_end) {
  Window result{};
  result.end = resolve_end(history, requested_end);
  if (result.end == 0) return result;
  result.start = result.end > WINDOW_SECONDS
      ? result.end - WINDOW_SECONDS
      : 0;
  return result;
}

inline bool in_window(const RoomSample *sample, const Window &range) {
  return sample != nullptr && sample->epoch >= range.start &&
         sample->epoch <= range.end;
}

inline bool begins_run(bool open, uint32_t previous, uint32_t current) {
  return !open || previous == 0 || current - previous > BREAK_SECONDS;
}

inline size_t count_runs(const RoomHistory &history, const Window &range) {
  bool open = false;
  uint32_t previous = 0;
  size_t runs = 0;
  for (size_t index = 0; index < history.count(); ++index) {
    const auto *sample = history.sample(index);
    if (!in_window(sample, range)) continue;
    if (!valid(sample)) {
      open = false;
      previous = 0;
      continue;
    }
    if (begins_run(open, previous, sample->epoch)) {
      ++runs;
      open = true;
    }
    previous = sample->epoch;
  }
  return runs;
}

inline void temperature_range(const RoomHistory &history, const Window &range,
                              float *minimum, float *maximum) {
  float low = std::numeric_limits<float>::infinity();
  float high = -std::numeric_limits<float>::infinity();
  for (size_t index = 0; index < history.count(); ++index) {
    const auto *sample = history.sample(index);
    if (!in_window(sample, range) || !valid(sample)) continue;
    const float value = sample->temperature_centi / 100.0f;
    low = std::min(low, value);
    high = std::max(high, value);
  }
  if (!std::isfinite(low) || !std::isfinite(high)) {
    *minimum = 0.0f;
    *maximum = 0.0f;
    return;
  }
  const float padding = std::max(0.5f, (high - low) * 0.10f);
  *minimum = low - padding;
  *maximum = high + padding;
  if (*maximum - *minimum >= 2.0f) return;
  const float middle = (low + high) * 0.5f;
  *minimum = middle - 1.0f;
  *maximum = middle + 1.0f;
}

inline int16_t scale_x(uint32_t epoch, const Window &range, int16_t width) {
  if (range.end <= range.start || width <= 0) return 0;
  const uint64_t elapsed = epoch > range.start ? epoch - range.start : 0;
  const uint64_t span = range.end - range.start;
  return static_cast<int16_t>(std::min<uint64_t>(
      width, elapsed * static_cast<uint64_t>(width) / span));
}

inline int16_t scale_y(float value, float minimum, float maximum,
                       int16_t height) {
  if (height <= 0 || maximum <= minimum) return height / 2;
  const float ratio = std::clamp(
      (maximum - value) / (maximum - minimum), 0.0f, 1.0f);
  return static_cast<int16_t>(std::lround(ratio * height));
}

inline void append_point(Model *model, const RoomSample &sample,
                         const Window &range, int16_t width,
                         int16_t temp_height, int16_t humidity_height) {
  if (model->point_count >= MAX_POINTS) return;
  const size_t index = model->point_count++;
  const int16_t x = scale_x(sample.epoch, range, width);
  model->temperature[index] = {
      x, scale_y(sample.temperature_centi / 100.0f, model->temperature_min,
                 model->temperature_max, temp_height)};
  model->humidity[index] = {
      x, scale_y(sample.humidity_centi / 100.0f, 0.0f, 100.0f,
                 humidity_height)};
  ++model->segments[model->segment_count - 1].count;
  if (model->first_epoch == 0) model->first_epoch = sample.epoch;
  model->last_epoch = sample.epoch;
}

inline bool start_segment(Model *model, size_t run, size_t skipped) {
  if (run <= skipped || model->segment_count >= MAX_SEGMENTS) return false;
  auto &segment = model->segments[model->segment_count++];
  segment.start = model->point_count;
  return true;
}

inline void collect_points(const RoomHistory &history, const Window &range,
                           size_t skipped, int16_t width, int16_t temp_height,
                           int16_t humidity_height, Model *model) {
  bool open = false;
  bool keep_run = false;
  uint32_t previous = 0;
  size_t run = 0;
  for (size_t index = 0; index < history.count(); ++index) {
    const auto *sample = history.sample(index);
    if (!in_window(sample, range)) continue;
    if (!valid(sample)) {
      open = false;
      keep_run = false;
      previous = 0;
      continue;
    }
    if (begins_run(open, previous, sample->epoch)) {
      ++run;
      open = true;
      keep_run = start_segment(model, run, skipped);
    }
    previous = sample->epoch;
    if (!keep_run) continue;
    append_point(model, *sample, range, width, temp_height, humidity_height);
  }
}

inline void build(const RoomHistory &history, uint32_t requested_end,
                  int16_t width, int16_t temp_height, int16_t humidity_height,
                  Model *model) {
  *model = Model{};
  model->valid_count = static_cast<uint32_t>(history.valid_count());
  model->gap_count = static_cast<uint32_t>(history.gap_count());
  const Window range = window(history, requested_end);
  if (range.end == 0 || history.count() == 0) return;
  const size_t runs = count_runs(history, range);
  const size_t skipped = runs > MAX_SEGMENTS ? runs - MAX_SEGMENTS : 0;
  model->dropped_runs =
      static_cast<uint8_t>(std::min<size_t>(skipped, 255));
  temperature_range(history, range, &model->temperature_min,
                    &model->temperature_max);
  collect_points(history, range, skipped, width, temp_height,
                  humidity_height, model);
}

inline Workspace *allocate_workspace(bool *using_psram) {
  void *memory = heap_caps_calloc(
      1, sizeof(Workspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  *using_psram = memory != nullptr;
  if (memory == nullptr) {
    memory = heap_caps_calloc(1, sizeof(Workspace), MALLOC_CAP_8BIT);
  }
  return static_cast<Workspace *>(memory);
}

using Line = esphome::lvgl::LvLineType;

inline void hide_lines(Line *const *lines) {
  for (size_t index = 0; index < MAX_SEGMENTS; ++index) {
    if (lines[index] != nullptr) {
      lv_obj_add_flag(lines[index]->obj, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

inline void copy_points(const Point *source, lv_point_precise_t *target,
                        uint16_t count) {
  for (uint16_t index = 0; index < count; ++index) {
    target[index].x = source[index].x;
    target[index].y = source[index].y;
  }
}

inline void show_segment(Line *line, lv_point_precise_t *points,
                         lv_point_precise_t single[2], uint16_t count) {
  if (line == nullptr || line->obj == nullptr || count == 0) return;
  if (count == 1) {
    single[0] = points[0];
    single[1] = points[0];
    lv_line_set_points(line->obj, single, 2);
  } else {
    lv_line_set_points(line->obj, points, count);
  }
  lv_obj_clear_flag(line->obj, LV_OBJ_FLAG_HIDDEN);
}

inline void render(const Model &model, Line *const *temperature_lines,
                   Line *const *humidity_lines, LvglBuffers *buffers) {
  hide_lines(temperature_lines);
  hide_lines(humidity_lines);
  copy_points(model.temperature, buffers->temperature, model.point_count);
  copy_points(model.humidity, buffers->humidity, model.point_count);
  for (uint8_t index = 0; index < model.segment_count; ++index) {
    const auto &segment = model.segments[index];
    show_segment(temperature_lines[index],
                 buffers->temperature + segment.start,
                 buffers->temperature_single[index], segment.count);
    show_segment(humidity_lines[index],
                 buffers->humidity + segment.start,
                 buffers->humidity_single[index], segment.count);
  }
}

}  // namespace usp_room_chart

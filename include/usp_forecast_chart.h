#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "usp_forecast_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace usp_forecast_chart {

static constexpr size_t MAX_POINTS = 24;
static constexpr size_t MAX_SEGMENTS = 12;

using Line = esphome::lvgl::LvLineType;

struct Segment {
  uint8_t start{0};
  uint8_t count{0};
};

struct Model {
  std::array<lv_point_precise_t, MAX_POINTS> points{};
  std::array<Segment, MAX_SEGMENTS> segments{};
  uint8_t point_count{0};
  uint8_t segment_count{0};
};

struct Buffers {
  std::array<lv_point_precise_t, MAX_POINTS> points{};
  std::array<std::array<lv_point_precise_t, 2>, MAX_SEGMENTS> single{};
};

inline int16_t scale_y(float value, float minimum, float maximum, int16_t height) {
  if (height <= 0 || maximum <= minimum) return height / 2;
  const float ratio = std::clamp((maximum - value) / (maximum - minimum), 0.0F, 1.0F);
  return static_cast<int16_t>(std::lround(ratio * height));
}

inline Model build(const usp_forecast_view::View &view, int16_t width, int16_t height) {
  Model model;
  bool open = false;
  for (size_t index = 0; index < view.points.size(); ++index) {
    const auto &source = view.points[index];
    if (!source.available) {
      open = false;
      continue;
    }
    if (!open) {
      if (model.segment_count >= MAX_SEGMENTS) break;
      auto &segment = model.segments[model.segment_count++];
      segment.start = model.point_count;
      open = true;
    }
    if (model.point_count >= MAX_POINTS) break;
    const int16_t x = static_cast<int16_t>(
        (static_cast<int32_t>(index) * width) / (MAX_POINTS - 1));
    model.points[model.point_count] = {
        x, scale_y(source.temperature, view.minimum, view.maximum, height)};
    ++model.point_count;
    ++model.segments[model.segment_count - 1].count;
  }
  return model;
}

inline void hide(Line *const *lines) {
  for (size_t index = 0; index < MAX_SEGMENTS; ++index) {
    if (lines[index] != nullptr && lines[index]->obj != nullptr)
      lv_obj_add_flag(lines[index]->obj, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void render(const usp_forecast_view::View &view, Line *const *lines,
                   Buffers *buffers, int16_t width, int16_t height) {
  hide(lines);
  const auto model = build(view, width, height);
  for (uint8_t index = 0; index < model.point_count; ++index)
    buffers->points[index] = model.points[index];
  for (uint8_t index = 0; index < model.segment_count; ++index) {
    auto *line = lines[index];
    const auto &segment = model.segments[index];
    if (line == nullptr || line->obj == nullptr || segment.count == 0) continue;
    auto *points = buffers->points.data() + segment.start;
    if (segment.count == 1) {
      buffers->single[index][0] = points[0];
      buffers->single[index][1] = points[0];
      lv_line_set_points(line->obj, buffers->single[index].data(), 2);
    } else {
      lv_line_set_points(line->obj, points, segment.count);
    }
    lv_obj_clear_flag(line->obj, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace usp_forecast_chart

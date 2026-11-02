#pragma once

#include <array>

#include "lvgl.h"
#include "usp_weather_view_types.h"

namespace usp_weather_lcd_delivery {

struct DayTargets {
  lv_obj_t *date{};
  lv_obj_t *condition{};
  lv_obj_t *temperature{};
  lv_obj_t *rain{};
};

struct Targets {
  lv_obj_t *city{};
  lv_obj_t *status{};
  lv_obj_t *temperature{};
  lv_obj_t *condition{};
  lv_obj_t *feels{};
  lv_obj_t *humidity{};
  lv_obj_t *wind{};
  lv_obj_t *updated{};
  std::array<DayTargets, 4> days{};
};

inline void render_day(const usp_weather_view::DayView &view,
                       const DayTargets &targets) {
  lv_label_set_text(targets.date, view.date.c_str());
  lv_label_set_text(targets.condition, view.condition.c_str());
  lv_label_set_text(targets.temperature, view.temperature.c_str());
  lv_label_set_text(targets.rain, view.rain.c_str());
}

inline void render_current(const usp_weather_view::WeatherView &view,
                           const Targets &targets) {
  lv_label_set_text(targets.city, view.city.c_str());
  lv_label_set_text(targets.status, view.status.c_str());
  lv_obj_set_style_text_color(targets.status, lv_color_hex(view.status_color),
                              LV_PART_MAIN);
  lv_label_set_text(targets.temperature, view.temperature.c_str());
  lv_label_set_text(targets.condition, view.condition.c_str());
  lv_label_set_text(targets.feels, view.feels.c_str());
  lv_label_set_text(targets.humidity, view.humidity.c_str());
  lv_label_set_text(targets.wind, view.wind.c_str());
  lv_label_set_text(targets.updated, view.updated.c_str());
}

inline void render(const usp_weather_view::WeatherView &view,
                   const Targets &targets) {
  render_current(view, targets);
  for (size_t index = 0; index < view.days.size(); ++index) {
    render_day(view.days[index], targets.days[index]);
  }
}

}  // namespace usp_weather_lcd_delivery

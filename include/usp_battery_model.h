#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace usp_battery_model {

enum class State : uint8_t {
  NOT_INSTALLED = 0,
  UNAVAILABLE = 1,
  CRITICAL = 2,
  LOW = 3,
  NORMAL = 4,
};

struct CurvePoint {
  float voltage;
  float percent;
};

inline float estimate_percent(float voltage) {
  if (!std::isfinite(voltage)) return NAN;

  constexpr CurvePoint curve[] = {
      {3.40f, 0.0f},  {3.50f, 2.0f},  {3.60f, 5.0f},
      {3.70f, 12.0f}, {3.75f, 20.0f}, {3.80f, 30.0f},
      {3.85f, 40.0f}, {3.90f, 50.0f}, {3.95f, 60.0f},
      {4.00f, 70.0f}, {4.05f, 80.0f}, {4.10f, 90.0f},
      {4.15f, 95.0f}, {4.20f, 100.0f},
  };

  if (voltage <= curve[0].voltage) return 0.0f;
  constexpr size_t count = sizeof(curve) / sizeof(curve[0]);
  if (voltage >= curve[count - 1].voltage) return 100.0f;

  for (size_t i = 1; i < count; ++i) {
    if (voltage > curve[i].voltage) continue;
    const auto &low = curve[i - 1];
    const auto &high = curve[i];
    const float ratio = (voltage - low.voltage) /
                        (high.voltage - low.voltage);
    return low.percent + ratio * (high.percent - low.percent);
  }
  return 100.0f;
}

inline State state(bool installed, float voltage) {
  if (!installed) return State::NOT_INSTALLED;
  const float percent = estimate_percent(voltage);
  if (!std::isfinite(percent)) return State::UNAVAILABLE;
  if (percent <= 5.0f) return State::CRITICAL;
  if (percent <= 15.0f) return State::LOW;
  return State::NORMAL;
}

inline const char *state_code(State value) {
  switch (value) {
    case State::NOT_INSTALLED: return "NOT_INSTALLED";
    case State::UNAVAILABLE: return "UNAVAILABLE";
    case State::CRITICAL: return "CRITICAL";
    case State::LOW: return "LOW";
    case State::NORMAL: return "NORMAL";
  }
  return "UNAVAILABLE";
}

inline const char *state_label(uint8_t language, State value) {
  if (language == 0U) {
    switch (value) {
      case State::NOT_INSTALLED: return "Батарея не встановлена";
      case State::UNAVAILABLE: return "Батарея: немає даних";
      case State::CRITICAL: return "Критичний заряд";
      case State::LOW: return "Низький заряд";
      case State::NORMAL: return "Батарея: норма";
    }
  }
  if (language == 1U) {
    switch (value) {
      case State::NOT_INSTALLED: return "Батарея не установлена";
      case State::UNAVAILABLE: return "Батарея: нет данных";
      case State::CRITICAL: return "Критический заряд";
      case State::LOW: return "Низкий заряд";
      case State::NORMAL: return "Батарея: норма";
    }
  }
  switch (value) {
    case State::NOT_INSTALLED: return "Battery not installed";
    case State::UNAVAILABLE: return "Battery unavailable";
    case State::CRITICAL: return "Battery critical";
    case State::LOW: return "Battery low";
    case State::NORMAL: return "Battery normal";
  }
  return "Battery unavailable";
}

inline const char *button_label(uint8_t language, bool installed) {
  if (language == 0U) return installed ? "БАТАРЕЯ: Є" : "БАТАРЕЯ: НЕМАЄ";
  if (language == 1U) return installed ? "БАТАРЕЯ: ЕСТЬ" : "БАТАРЕЯ: НЕТ";
  return installed ? "BATTERY: INSTALLED" : "BATTERY: NOT INSTALLED";
}

inline uint32_t text_rgb(State value) {
  switch (value) {
    case State::CRITICAL: return 0xF87171U;
    case State::LOW: return 0xFACC15U;
    case State::NORMAL: return 0x4ADE80U;
    default: return 0x90A4B8U;
  }
}

inline uint32_t background_rgb(State value) {
  switch (value) {
    case State::CRITICAL: return 0x3B1820U;
    case State::LOW: return 0x3A3012U;
    case State::NORMAL: return 0x123B2AU;
    default: return 0x202B3AU;
  }
}

}  // namespace usp_battery_model

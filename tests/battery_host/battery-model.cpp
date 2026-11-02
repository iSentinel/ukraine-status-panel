#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "usp_battery_model.h"

int main() {
  using usp_battery_model::State;
  using usp_battery_model::estimate_percent;
  using usp_battery_model::state;

  assert(std::isnan(estimate_percent(NAN)));
  assert(estimate_percent(3.30f) == 0.0f);
  assert(estimate_percent(4.25f) == 100.0f);
  assert(std::fabs(estimate_percent(3.90f) - 50.0f) < 0.01f);
  assert(std::fabs(estimate_percent(4.05f) - 80.0f) < 0.01f);
  assert(estimate_percent(3.775f) > 20.0f);
  assert(estimate_percent(3.775f) < 30.0f);

  float previous = -1.0f;
  for (int mv = 3300; mv <= 4250; ++mv) {
    const float value = estimate_percent(static_cast<float>(mv) / 1000.0f);
    assert(value >= previous);
    assert(value >= 0.0f && value <= 100.0f);
    previous = value;
  }

  assert(state(false, 4.12f) == State::NOT_INSTALLED);
  assert(state(true, NAN) == State::UNAVAILABLE);
  assert(state(true, 3.60f) == State::CRITICAL);
  assert(state(true, 3.70f) == State::LOW);
  assert(state(true, 3.80f) == State::NORMAL);
  assert(std::strcmp(usp_battery_model::state_code(State::LOW), "LOW") == 0);
  assert(std::strstr(usp_battery_model::button_label(2, true), "INSTALLED") != nullptr);
  assert(usp_battery_model::text_rgb(State::CRITICAL) == 0xF87171U);

  std::cout << "BATTERY_MODEL=PASS monotonic=951 states=5 manual_presence=1\n";
  return 0;
}

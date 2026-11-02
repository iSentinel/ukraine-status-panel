#pragma once
#include <cstdint>

namespace usp_weather_refresh {

constexpr uint32_t MIN_ATTEMPT_INTERVAL_MS = 60'000U;

struct AttemptState {
  uint32_t last_attempt_ms{0};
  bool attempted{false};
};

inline uint32_t elapsed(uint32_t now, uint32_t then) {
  return now - then;
}

inline bool eligible(const AttemptState &state, uint32_t now) {
  return !state.attempted || elapsed(now, state.last_attempt_ms) >= MIN_ATTEMPT_INTERVAL_MS;
}

inline bool should_start(bool manual_pending, bool scheduled_due,
                         const AttemptState &state, uint32_t now) {
  return (manual_pending || scheduled_due) && eligible(state, now);
}

inline void mark_started(AttemptState *state, uint32_t now) {
  state->last_attempt_ms = now;
  state->attempted = true;
}

}  // namespace usp_weather_refresh

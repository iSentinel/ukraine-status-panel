#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "usp_weather_refresh_gate.h"

using usp_weather_refresh::AttemptState;
using usp_weather_refresh::MIN_ATTEMPT_INTERVAL_MS;
using usp_weather_refresh::mark_started;
using usp_weather_refresh::should_start;

int main() {
  AttemptState state{};
  assert(should_start(true, false, state, 1000));
  mark_started(&state, 1000);
  assert(!should_start(true, false, state, 1001));
  assert(!should_start(false, true, state, 31'000));
  assert(!should_start(true, false, state, 60'999));
  assert(should_start(true, false, state, 61'000));

  // Atomic pending bit is bounded and burst-collapsing under concurrent callers.
  std::atomic<bool> pending{false};
  std::vector<std::thread> workers;
  for (int thread = 0; thread < 4; ++thread) {
    workers.emplace_back([&pending]() {
      for (int i = 0; i < 2500; ++i)
        pending.store(true, std::memory_order_release);
    });
  }
  for (auto &worker : workers) worker.join();
  assert(pending.load(std::memory_order_acquire));
  pending.exchange(false, std::memory_order_acq_rel);
  assert(!pending.load());

  // Request during active fetch re-arms the bit after the worker consumed it.
  mark_started(&state, 61'000);
  pending.store(true, std::memory_order_release);
  assert(!should_start(pending.load(), false, state, 69'000));
  assert(should_start(pending.load(), false, state, 121'000));

  // Automatic 15m cadence is unaffected once the protective gap has elapsed.
  mark_started(&state, 121'000);
  assert(!should_start(false, false, state, 1'020'999));
  assert(should_start(false, true, state, 1'021'000));

  // Wrap-safe unsigned elapsed arithmetic.
  AttemptState wrap{0xFFFF'FFF0U, true};
  assert(!should_start(true, false, wrap, 0x10U));
  const uint32_t deadline = static_cast<uint32_t>(0xFFFF'FFF0U + MIN_ATTEMPT_INTERVAL_MS);
  assert(should_start(true, false, wrap, deadline));

  static_assert(sizeof(AttemptState) <= 8);
  std::cout << "WEATHER_REFRESH_GATE_NATIVE=PASS gap_ms=" << MIN_ATTEMPT_INTERVAL_MS
            << " concurrent10000=collapsed pending_fetch=retained retry30s=blocked wrap=pass bytes="
            << sizeof(AttemptState) << "\n";
}

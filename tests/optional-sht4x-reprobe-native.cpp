#include "reprobe_policy.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using esphome::usp_optional_sht4x::ReprobePolicy;

int main() {
  static_assert(sizeof(ReprobePolicy) <= 8, "reprobe policy must stay tiny");
  static_assert(ReprobePolicy::NEVER_SEEN_INTERVAL_MS == 300000U);
  static_assert(ReprobePolicy::RECONNECT_INTERVAL_MS == 60000U);

  ReprobePolicy gate;
  assert(gate.due(100U));
  gate.schedule(100U, false);
  assert(gate.armed());
  assert(!gate.due(300099U));
  assert(gate.due(300100U));
  gate.clear();
  assert(gate.due(123U));

  gate.schedule(1000U, true);
  assert(!gate.due(60999U));
  assert(gate.due(61000U));

  const uint32_t near_wrap = UINT32_MAX - 1000U;
  gate.schedule(near_wrap, false);
  const uint32_t wrapped_due = near_wrap + ReprobePolicy::NEVER_SEEN_INTERVAL_MS;
  assert(!gate.due(wrapped_due - 1U));
  assert(gate.due(wrapped_due));
  assert(gate.due(wrapped_due + 1U));

  std::cout << "OPTIONAL_SHT4X_REPROBE_NATIVE=PASS never_seen=300s reconnect=60s wrap=1 bytes="
            << sizeof(ReprobePolicy) << "\n";
}

#include "usp_stripe_bandwidth_probe.h"
#include <cassert>
#include <cstdio>
#include <vector>

void normal(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
  const auto before_a = a, before_b = b;
  for (unsigned count = 0; count < 10; ++count) {
    const auto result = usp_stripe_bandwidth::run(a.data(), b.data(), 511, count % 2, 69300);
    assert(result.accepted && result.complete && result.rows_done == 600);
    assert(result.scratch_rows == 4 && result.scratch_bytes == 8192);
    assert(result.heap_before == result.heap_after && host_live_allocations == 0);
  }
  assert(a == before_a && b == before_b);
  host_available = 46 * 1024;
  const auto smaller = usp_stripe_bandwidth::run(a.data(), b.data(), 1, true, 69300);
  assert(smaller.complete && smaller.scratch_rows == 2 && smaller.scratch_bytes == 4096);
}

void refusals(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
  const auto calls = host_allocations;
  host_available = 43 * 1024;
  assert(!usp_stripe_bandwidth::run(a.data(), b.data(), 1, false, 69300).accepted);
  host_available = 52000;
  host_largest = 2048;
  assert(!usp_stripe_bandwidth::run(a.data(), b.data(), 1, false, 69300).accepted);
  host_largest = 52000;
  assert(!usp_stripe_bandwidth::run(a.data(), b.data(), 1025, false, 69300).accepted);
  assert(!usp_stripe_bandwidth::run(a.data(), a.data(), 1, false, 69300).accepted);
  assert(!usp_stripe_bandwidth::run(nullptr, b.data(), 1, false, 69300).accepted);
  assert(host_allocations == calls);
  host_fail_allocation = true;
  assert(!usp_stripe_bandwidth::run(a.data(), b.data(), 1, false, 69300).accepted);
  host_fail_allocation = false;
  assert(host_live_allocations == 0);
}

void bounds(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
  host_tick = UINT32_MAX - 50;
  auto result = usp_stripe_bandwidth::run(a.data(), b.data(), 32, false, 69300);
  assert(result.complete && result.total_us < 100000);
  host_step = 1000;
  result = usp_stripe_bandwidth::run(a.data(), b.data(), 1023, true, 69300);
  assert(result.accepted && !result.complete && result.rows_done < 600);
  assert(result.total_us <= 105000 && host_live_allocations == 0);
  host_step = 5;
}

int main() {
  std::vector<uint16_t> a(1024 * 600, 0x1234), b(1024 * 600, 0xABCD);
  normal(a, b);
  refusals(a, b);
  bounds(a, b);
  std::puts("STRIPE_PROBE_NATIVE=PASS exact_header read_only bounded_heap allocation_failure timeout rollover free_once");
}

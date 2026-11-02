#!/usr/bin/env python3
"""Exercise production HTTPS gate priority and bounded admission."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/usp_weather/network_gate.h"

HAL = r'''
#pragma once
#include <chrono>
#include <cstdint>
#include <thread>
inline uint32_t millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now() - start).count());
}
inline void delay(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
'''
LOG = r'''
#pragma once
template<typename... Args> inline void usp_test_log(Args&&...) {}
#define ESP_LOGI(...) usp_test_log(__VA_ARGS__)
#define ESP_LOGW(...) usp_test_log(__VA_ARGS__)
'''
CPP = r'''
#include "components/usp_weather/network_gate.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  using namespace esphome::usp_network;
  using namespace std::chrono;
  std::atomic<bool> holder_ready{false};
  std::thread holder([&]() {
    HttpsTransaction slow("outage", HttpsPriority::NORMAL, 1000);
    assert(slow.acquired());
    holder_ready.store(true, std::memory_order_release);
    std::this_thread::sleep_for(milliseconds(180));
  });
  while (!holder_ready.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  const auto started = steady_clock::now();
  HttpsTransaction critical("neptun.threats", HttpsPriority::CRITICAL, 50);
  const auto waited = duration_cast<milliseconds>(
      steady_clock::now() - started).count();
  assert(!critical.acquired());
  assert(waited >= 40 && waited < 120);
  holder.join();

  // A waiting CRITICAL request owns admission before a later NORMAL waiter.
  std::atomic<bool> first_ready{false};
  std::atomic<int> order{0};
  int critical_order = 0;
  int normal_order = 0;
  std::thread first([&]() {
    HttpsTransaction tx("weather", HttpsPriority::NORMAL, 1000);
    assert(tx.acquired());
    first_ready.store(true, std::memory_order_release);
    std::this_thread::sleep_for(milliseconds(80));
  });
  while (!first_ready.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::thread alert([&]() {
    HttpsTransaction tx("neptun.alerts", HttpsPriority::CRITICAL, 500);
    assert(tx.acquired());
    critical_order = ++order;
    std::this_thread::sleep_for(milliseconds(20));
  });
  std::this_thread::sleep_for(milliseconds(10));
  std::thread normal([&]() {
    HttpsTransaction tx("outage", HttpsPriority::NORMAL, 500);
    assert(tx.acquired());
    normal_order = ++order;
  });
  first.join(); alert.join(); normal.join();
  assert(critical_order == 1);
  assert(normal_order == 2);
  assert(https_waiters().load() == 0);
  assert(https_critical_waiters().load() == 0);
  assert(https_active_owner().load() == nullptr);
  std::cout
      << "NETWORK_GATE_PRIORITY_NATIVE=PASS bounded50 critical_first "
      << "cleanup\n";
}
'''


def main() -> None:
    """Compile actual production header with host shims and sanitizers."""
    with tempfile.TemporaryDirectory(prefix="usp-net-gate-") as temp:
        root = Path(temp)
        shim = root / "shim/esphome/core"
        shim.mkdir(parents=True)
        (shim / "hal.h").write_text(HAL)
        (shim / "log.h").write_text(LOG)
        cpp = root / "test.cpp"
        binary = root / "test"
        cpp.write_text(CPP)
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-g", "-pthread", f"-I{root / 'shim'}", f"-I{ROOT}",
            str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()

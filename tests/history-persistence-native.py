#!/usr/bin/env python3
"""Exercise HistoryStore persistence retry policy using production methods."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/usp_history/history_store.cpp"
METHODS = (
    "loop", "save_", "mark_dirty_", "retry_due_",
    "note_save_failure_", "reset_save_retry_", "flush",
)


def method(source: str, name: str) -> str:
    """Extract one complete production method by balanced braces."""
    marker = f"HistoryStore::{name}("
    pos = source.index(marker)
    start = source.rfind("\n", 0, pos) + 1
    opened = source.index("{", pos)
    depth = 1
    end = opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <iostream>

namespace esphome {
static uint32_t test_millis = 0;
uint32_t millis() { return test_millis; }

template<typename... Args> void history_log(Args&&...) {}
#define ESP_LOGW(...) history_log(__VA_ARGS__)
#define ESP_LOGI(...) history_log(__VA_ARGS__)

struct HistoryBlob {
  uint16_t count{0};
  uint16_t head{0};
  uint32_t total_events{0};
  uint8_t bytes[392]{};
  uint32_t checksum{0};
};
struct TestPreference {
  unsigned saves{0};
  bool save_ok{true};
  bool save(const HistoryBlob *) { ++saves; return save_ok; }
};
struct TestPreferences {
  unsigned syncs{0};
  bool sync_ok{false};
  bool sync() { ++syncs; return sync_ok; }
};
static TestPreferences prefs;
static TestPreferences *global_preferences = &prefs;
struct TestTrigger { unsigned calls{0}; void trigger() { ++calls; } };

namespace usp_history {
static const char *const TAG = "usp.history";
class HistoryStore {
 public:
  void loop();
  bool flush();
 protected:
  bool save_(bool notify = true);
  void mark_dirty_();
  bool retry_due_(uint32_t now) const;
  void note_save_failure_(uint32_t now);
  void reset_save_retry_();
  static uint32_t checksum_(const HistoryBlob &) { return 0; }
  static constexpr uint32_t MIN_RETRY_MS = 30000;
  static constexpr uint32_t MAX_RETRY_MS = 300000;
  TestPreference preference_{};
  HistoryBlob blob_{};
  TestTrigger update_trigger_{};
  uint32_t commit_delay_ms_{5000};
  uint32_t dirty_since_ms_{0};
  uint32_t save_count_{0};
  uint32_t last_save_attempt_ms_{0};
  uint32_t retry_delay_ms_{MIN_RETRY_MS};
  bool dirty_{false};
  bool storage_ok_{true};
  bool loaded_from_nvs_{false};
  bool save_failed_{false};
 public:
  void make_dirty() { mark_dirty_(); }
  unsigned syncs() const { return prefs.syncs; }
  unsigned triggers() const { return update_trigger_.calls; }
  uint32_t retry_ms() const { return retry_delay_ms_; }
  bool dirty() const { return dirty_; }
};
'''

SUFFIX = r'''
}  // namespace usp_history
}  // namespace esphome

int main() {
  using namespace esphome;
  using namespace esphome::usp_history;
  HistoryStore store;
  prefs = TestPreferences{};
  test_millis = 1000;
  store.make_dirty();
  test_millis = 5999;
  store.loop();
  assert(store.syncs() == 0);
  test_millis = 6000;
  store.loop();
  assert(store.syncs() == 1);
  assert(store.retry_ms() == 30000);
  for (uint32_t delta = 1; delta < 30000; delta += 997) {
    test_millis = 6000 + delta;
    store.loop();
  }
  assert(store.syncs() == 1);
  test_millis = 36000;
  store.loop();
  assert(store.syncs() == 2);
  assert(store.retry_ms() == 60000);
  test_millis = 95999;
  store.loop();
  assert(store.syncs() == 2);
  test_millis = 96000;
  store.loop();
  assert(store.syncs() == 3);
  assert(store.retry_ms() == 120000);

  // New dirty events must not bypass an active storage-failure backoff.
  test_millis = 100000;
  store.make_dirty();
  store.loop();
  assert(store.syncs() == 3);

  // Explicit flush remains immediate; successful persistence resets policy.
  prefs.sync_ok = true;
  test_millis = 100001;
  assert(store.flush());
  assert(store.syncs() == 4);
  assert(!store.dirty());
  assert(store.retry_ms() == 30000);
  assert(store.triggers() == 1);
  store.loop();
  assert(store.syncs() == 4);

  // Rollover-safe elapsed arithmetic for a failed automatic retry.
  HistoryStore wrap;
  prefs = TestPreferences{};
  test_millis = 0xFFFFF000U;
  wrap.make_dirty();
  test_millis = 0x00000388U;  // +5000 ms across wrap
  wrap.loop();
  assert(wrap.syncs() == 1);
  test_millis = 0x000078B7U;  // +29999 ms from failed attempt
  wrap.loop();
  assert(wrap.syncs() == 1);
  test_millis = 0x000078B8U;  // +30000 ms
  wrap.loop();
  assert(wrap.syncs() == 2);

  // Retry growth is bounded at five minutes.
  HistoryStore cap;
  prefs = TestPreferences{};
  test_millis = 1000;
  cap.make_dirty();
  test_millis = 6000;
  cap.loop();
  const uint32_t waits[] = {30000, 60000, 120000, 240000, 300000, 300000};
  uint32_t now = 6000;
  for (uint32_t wait : waits) {
    now += wait;
    test_millis = now;
    cap.loop();
  }
  assert(cap.retry_ms() == 300000);

  std::cout
      << "HISTORY_PERSISTENCE_NATIVE=PASS commit_delay bounded_backoff "
      << "event_no_bypass immediate_flush success_reset rollover cap300s\n";
}
'''


def main() -> None:
    """Compile production persistence methods with ASAN/UBSAN and run them."""
    source = SOURCE.read_text()
    generated = PREFIX + "".join(method(source, name) for name in METHODS)
    generated += SUFFIX
    with tempfile.TemporaryDirectory(prefix="usp-history-persist-") as tmp:
        cpp = Path(tmp) / "test.cpp"
        binary = Path(tmp) / "test"
        cpp.write_text(generated)
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-g", str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()

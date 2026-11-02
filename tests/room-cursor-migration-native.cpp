#include "usp_room_aggregate_history.h"
#include "esphome/core/helpers.h"
#include "esp_heap_caps.h"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using esphome::usp_room_aggregate_history::Aggregate;
using esphome::usp_room_aggregate_history::RoomAggregateHistory;
uint32_t esphome::host_millis = 1000;

static void reset_host(bool clear_nvs = true) {
  assert(host_live_bytes == 0);
  if (clear_nvs) esphome::host_preferences.persisted.clear();
  esphome::host_preferences.pending.clear();
  esphome::host_preferences.save_calls = 0;
  esphome::host_preferences.sync_calls = 0;
  esphome::host_preferences.fail_save = false;
  esphome::host_preferences.fail_sync = false;
  esphome::host_preferences.fail_save_after = -1;
  host_alloc_fail_after = -1;
  host_alloc_calls = 0;
  esphome::host_millis = 1000;
}

static std::vector<uint8_t> decode_hex(const std::string &text) {
  assert(text.size() % 2 == 0);
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size() / 2);
  for (size_t index = 0; index < text.size(); index += 2)
    bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(index, 2), nullptr, 16)));
  return bytes;
}

static void load_fixture(const std::string &path) {
  reset_host();
  std::ifstream input(path);
  assert(input);
  std::string key_text;
  std::string data_text;
  while (input >> key_text >> data_text) {
    const auto key = static_cast<uint32_t>(std::stoul(key_text, nullptr, 16));
    esphome::host_preferences.persisted[key] = decode_hex(data_text);
  }
  assert(!esphome::host_preferences.persisted.empty());
}

static void add_day(RoomAggregateHistory &history, uint32_t &epoch,
                    uint16_t day, bool repeated_hour) {
  const unsigned hours = repeated_hour ? 25 : 24;
  for (unsigned index = 0; index < hours; ++index) {
    const uint8_t hour = repeated_hour && index >= 4 ? index - 1 : index;
    for (unsigned sample = 0; sample < 12; ++sample) {
      assert(history.record(epoch, 2026, day, hour, 20.0f, 50.0f, true));
      epoch += 300;
    }
  }
}

static const Aggregate *last_day(const RoomAggregateHistory &history) {
  assert(history.daily_count() != 0);
  return history.daily_sample(history.daily_count() - 1);
}

static void test_normal_day_cursor() {
  reset_host();
  uint32_t epoch = 1792702800;
  {
    RoomAggregateHistory history;
    history.setup();
    add_day(history, epoch, 297, false);
    history.on_shutdown();
    assert(last_day(history)->valid_count == 288);
  }
  RoomAggregateHistory restored;
  restored.setup();
  assert(restored.loaded_from_nvs() && last_day(restored)->valid_count == 288);
  assert(!restored.record(epoch - 240, 2026, 297, 23, 21, 51, true));
  assert(!restored.record(epoch - 540, 2026, 297, 23, 22, 52, true));
  assert(restored.record(epoch, 2026, 298, 0, 21, 51, true));
  assert(restored.flush());
  assert(restored.daily_count() == 2 && last_day(restored)->valid_count == 1);
}

static void test_dst_reboots() {
  reset_host();
  uint32_t epoch = 1792789200;
  {
    RoomAggregateHistory history;
    history.setup();
    add_day(history, epoch, 297, false);
    add_day(history, epoch, 298, true);
    history.on_shutdown();
    assert(last_day(history)->valid_count == 300);
  }
  for (unsigned reboot = 0; reboot < 3; ++reboot) {
    RoomAggregateHistory restored;
    restored.setup();
    assert(restored.corrupt_pages() == 0 && last_day(restored)->valid_count == 300);
    assert(!restored.record(epoch - 240, 2026, 298, 23, 22, 52, true));
    restored.on_shutdown();
  }
}

static void test_partial_save_retry() {
  reset_host();
  RoomAggregateHistory history;
  history.setup();
  uint32_t epoch = 1800000000;
  assert(history.record(epoch, 2026, 300, 23, 20, 50, true));
  assert(history.record(epoch + 300, 2026, 301, 0, 21, 51, true));
  esphome::host_preferences.fail_save_after = 1;
  assert(!history.flush() && !history.storage_ok());
  assert(esphome::host_preferences.sync_calls == 0);
  esphome::host_preferences.fail_save_after = -1;
  esphome::host_millis += 30000;
  history.loop();
  assert(history.storage_ok() && esphome::host_preferences.sync_calls == 1);
  RoomAggregateHistory restored;
  restored.setup();
  assert(restored.hourly_count() == 2 && restored.daily_count() == 2);
  assert(!restored.record(epoch + 540, 2026, 301, 0, 99, 99, true));
}

static void test_sync_retry() {
  reset_host();
  RoomAggregateHistory history;
  history.setup();
  constexpr uint32_t epoch = 1810000000;
  assert(history.record(epoch, 2026, 302, 10, 20, 50, true));
  esphome::host_preferences.fail_sync = true;
  assert(!history.flush() && !history.storage_ok());
  esphome::host_preferences.fail_sync = false;
  esphome::host_millis += 30000;
  history.loop();
  assert(history.storage_ok());
  RoomAggregateHistory restored;
  restored.setup();
  assert(last_day(restored)->valid_count == 1);
  assert(!restored.record(epoch + 60, 2026, 302, 10, 22, 52, true));
}

static void test_legacy_midday(const std::string &fixture) {
  load_fixture(fixture);
  RoomAggregateHistory history;
  history.setup();
  assert(history.loaded_from_nvs() && history.daily_count() == 1);
  assert(last_day(history)->valid_count == 3);
  constexpr uint32_t base = 1793095200;
  assert(!history.record(base + 900, 2026, 300, 12, 21, 51, true));
  assert(history.record(base + 3600, 2026, 300, 13, 21, 51, true));
  assert(history.flush());
  assert(history.hourly_count() == 2 && last_day(history)->valid_count == 3);
  assert(history.record(base + 86400, 2026, 301, 0, 22, 52, true));
  assert(history.flush());
  assert(history.daily_count() == 2 && history.daily_sample(0)->valid_count == 3);
  assert(last_day(history)->valid_count == 1);
}

static void test_legacy_dst(const std::string &fixture) {
  load_fixture(fixture);
  RoomAggregateHistory history;
  history.setup();
  assert(history.loaded_from_nvs() && history.daily_count() == 2);
  assert(last_day(history)->valid_count == 300 && history.corrupt_pages() == 0);
  constexpr uint32_t next_epoch = 1792965600;
  assert(!history.record(next_epoch - 240, 2026, 298, 23, 21, 51, true));
  assert(history.record(next_epoch, 2026, 299, 0, 21, 51, true));
  assert(history.flush());
  assert(history.daily_count() == 3 && last_day(history)->valid_count == 1);
}


/** Reject damaged legacy bytes without discarding the other valid series. */
static void test_legacy_checksum(const std::string &fixture, bool daily) {
  load_fixture(fixture);
  bool changed = false;
  for (auto &entry : esphome::host_preferences.persisted) {
    if (entry.second.size() == (daily ? 760U : 592U)) {
      entry.second[20] ^= 1U;
      changed = true;
      break;
    }
  }
  assert(changed);
  const auto before = esphome::host_preferences.persisted;
  RoomAggregateHistory history;
  history.setup();
  assert(history.corrupt_pages() >= 1);
  assert(history.hourly_count() == (daily ? 1U : 0U));
  assert(history.daily_count() == (daily ? 0U : 1U));
  assert(esphome::host_preferences.save_calls == 0);
  assert(esphome::host_preferences.persisted == before);
}

static void test_new_page_sizes() {
  using namespace esphome::usp_room_aggregate_history;
  reset_host();
  RoomAggregateHistory history;
  history.setup();
  assert(history.record(1820000000, 2026, 303, 1, 20, 50, true));
  assert(history.flush());
  bool hour = false;
  bool day = false;
  for (const auto &entry : esphome::host_preferences.persisted) {
    hour = hour || entry.second.size() == HOUR_PAGE_BYTES;
    day = day || entry.second.size() == DAY_PAGE_BYTES;
  }
  assert(hour && day && NVS_PAYLOAD_BYTES == 29004);
}

int main(int argc, char **argv) {
  assert(argc == 3);
  test_normal_day_cursor();
  test_dst_reboots();
  test_partial_save_retry();
  test_sync_retry();
  test_legacy_midday(argv[1]);
  test_legacy_dst(argv[2]);
  test_legacy_checksum(argv[1], false);
  test_legacy_checksum(argv[1], true);
  test_new_page_sizes();
  assert(host_live_bytes == 0);
  std::cout << "ROOM_CURSOR_N6=PASS normal288 dst300 reboot3 partial_retry sync_retry legacy_preserved nvs29004\n";
}

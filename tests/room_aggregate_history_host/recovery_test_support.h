#pragma once
#include "nvs.h"
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
  if (clear_nvs) marker_host::reset();

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

static const Aggregate *last_day(const RoomAggregateHistory &history) {
  assert(history.daily_count() != 0);
  return history.daily_sample(history.daily_count() - 1);
}

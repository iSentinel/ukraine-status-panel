#pragma once
#include "neptun_json_string.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::usp_neptun {

static constexpr size_t REGION_ALERT_COUNT = 26;

enum class RegionAlertLevel : uint8_t {
  NONE = 0,
  YELLOW = 1,
  RED = 2,
};

struct AlertScanResult {
  bool saw_alerts{false};
  bool overflow{false};
  uint32_t bytes{0};
  uint16_t entries{0};
  uint16_t unknown_regions{0};
  uint16_t unknown_levels{0};
  std::array<uint8_t, REGION_ALERT_COUNT> levels{};
};

class AlertScanner {
 public:
  void feed(const char *data, size_t len);
  bool is_overflow() const { return result_.overflow; }
  const AlertScanResult &result() const { return result_; }
  bool complete() const {
    return seen_arrays_ == 3 && root_closed_ && !parse_error_ && !result_.overflow &&
        object_depth_ == 0 && array_depth_ == 0 && !in_string_;
  }

 private:
  static constexpr uint32_t MAX_BODY_BYTES = 131072;
  static constexpr size_t TOKEN_CAPACITY = 128;

  void process_(char value);
  void object_(char value);
  void array_(char value);
  void colon_();
  void string_character_(char value);
  void clear_pending_();
  void finish_string_();
  void start_object_();
  void finish_object_();
  void accept_value_();
  void reset_token_();

  AlertScanResult result_{};
  bool root_closed_{false}, parse_error_{false}, in_alert_array_{false};
  uint8_t expect_array_{0}, seen_arrays_{0};
  uint16_t array_depth_{0}, alert_array_depth_{0};
  uint16_t object_depth_{0};
  bool in_string_{false};
  JsonStringDecoder string_decoder_{};
  bool token_overflow_{false};
  bool expect_value_{false};
  char token_[TOKEN_CAPACITY]{};
  size_t token_len_{0};
  char pending_key_[16]{};
  char object_oblast_[64]{};
  char object_name_[64]{};
  char object_level_[16]{};
};

int region_alert_index(uint16_t region_id);
uint16_t region_id_from_neptun_oblast(const char *name);

}  // namespace esphome::usp_neptun

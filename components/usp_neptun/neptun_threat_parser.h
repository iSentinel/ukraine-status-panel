#pragma once
#include "neptun_json_string.h"

#include "neptun_alert_parser.h"
#include "neptun_threat_point.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::usp_neptun {

struct ThreatCounts {
  uint16_t total{0};
  uint16_t uav{0};
  uint16_t fpv{0};
  uint16_t recon{0};
  uint16_t missile{0};
  uint16_t ballistic{0};
  uint16_t kab{0};
  uint16_t mig31k{0};
  uint16_t unknown{0};
};

struct ThreatScanResult {
  bool saw_threats{false};
  bool overflow{false};
  uint32_t bytes{0};
  // Received records remain diagnostic, including completed threats.
  uint16_t entries{0};
  uint16_t unknown_regions{0};
  // Presentation counts include active, non-advisory records only.
  ThreatCounts global{};
  std::array<ThreatCounts, REGION_ALERT_COUNT> regions{};
  std::array<ThreatCounts, REGION_ALERT_COUNT> active_regions{};
  uint32_t server_epoch{0};
  uint16_t point_candidates{0};
  uint16_t point_count{0};
  uint16_t point_dropped_capacity{0};
  std::array<ThreatPoint, THREAT_POINT_CAPACITY> points{};
};

class ThreatScanner {
 public:
  void feed(const char *data, size_t len);
  bool is_overflow() const { return result_.overflow; }
  const ThreatScanResult &result() const { return result_; }
  bool complete() const {
    return result_.saw_threats && root_closed_ && !parse_error_ &&
        !result_.overflow && object_depth_ == 0 && array_depth_ == 0 &&
        !in_string_ && !in_primitive_;
  }

 private:
  static constexpr uint32_t MAX_BODY_BYTES = 1048576;
  static constexpr size_t TOKEN_CAPACITY = 128;

  void process_(char value);
  void structure_(char value);
  void colon_();
  void string_character_(char value);
  void primitive_character_(char value);
  void clear_pending_();
  void finish_string_();
  void finish_primitive_();
  void start_object_();
  void finish_object_();
  void accept_string_();
  void accept_primitive_();
  void maybe_add_point_();
  void insert_point_(const ThreatPoint &point);
  void finalize_points_();
  void reset_token_();
  bool threat_key_() const;

  ThreatScanResult result_{};
  bool root_closed_{false}, parse_error_{false}, in_primitive_{false};
  bool object_active_{true}, object_advisory_{false};
  bool object_status_seen_{false}, object_area_only_{false};
  bool object_presumptive_course_{false};
  bool object_lat_seen_{false}, object_lon_seen_{false};
  bool object_heading_seen_{false};
  uint16_t object_depth_{0};
  uint16_t array_depth_{0};
  uint16_t threats_array_depth_{0};
  bool in_threats_array_{false};
  bool expect_threats_array_{false};
  bool in_string_{false};
  JsonStringDecoder string_decoder_{};
  bool token_overflow_{false};
  bool expect_value_{false};
  char token_[TOKEN_CAPACITY]{};
  size_t token_len_{0};
  char pending_key_[24]{};
  char object_region_[64]{};
  char object_type_[16]{};
  uint32_t object_id_hash_{0};
  uint32_t object_updated_epoch_{0};
  float object_latitude_{0.0f};
  float object_longitude_{0.0f};
  float object_heading_{0.0f};
  ThreatPointQuality object_quality_{ThreatPointQuality::UNKNOWN};
  size_t stored_points_{0};
};

}  // namespace esphome::usp_neptun

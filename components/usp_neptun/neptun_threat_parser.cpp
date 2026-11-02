#include "neptun_threat_parser.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace esphome::usp_neptun {
namespace {
void add_type(ThreatCounts *counts, const char *value) {
  ++counts->total;
  if (std::strcmp(value, "uav") == 0) ++counts->uav;
  else if (std::strcmp(value, "fpv") == 0) ++counts->fpv;
  else if (std::strcmp(value, "recon") == 0) ++counts->recon;
  else if (std::strcmp(value, "missile") == 0) ++counts->missile;
  else if (std::strcmp(value, "ballistic") == 0) ++counts->ballistic;
  else if (std::strcmp(value, "kab") == 0) ++counts->kab;
  else if (std::strcmp(value, "mig31k") == 0) ++counts->mig31k;
  else ++counts->unknown;
}

bool parse_float(const char *text, float *value) {
  if (text == nullptr || value == nullptr || text[0] == '\0') return false;
  char *end = nullptr;
  const float parsed = std::strtof(text, &end);
  if (end == text || end == nullptr || *end != '\0' || !std::isfinite(parsed))
    return false;
  *value = parsed;
  return true;
}

bool newer(const ThreatPoint &left, const ThreatPoint &right) {
  if (left.updated_epoch != right.updated_epoch)
    return left.updated_epoch > right.updated_epoch;
  return left.id_hash < right.id_hash;
}
}  // namespace

void ThreatScanner::feed(const char *data, size_t len) {
  if (data == nullptr || len == 0 || result_.overflow) return;
  result_.bytes += static_cast<uint32_t>(len);
  if (result_.bytes > MAX_BODY_BYTES) {
    result_.overflow = true;
    return;
  }
  for (size_t index = 0; index < len; ++index) process_(data[index]);
}

void ThreatScanner::reset_token_() {
  token_len_ = 0;
  token_[0] = '\0';
  token_overflow_ = false;
  string_decoder_.reset();
}

void ThreatScanner::clear_pending_() {
  pending_key_[0] = '\0';
  expect_value_ = false;
}

void ThreatScanner::start_object_() {
  object_region_[0] = object_type_[0] = '\0';
  object_active_ = true;
  object_advisory_ = object_status_seen_ = object_area_only_ = false;
  object_presumptive_course_ = false;
  object_lat_seen_ = object_lon_seen_ = object_heading_seen_ = false;
  object_id_hash_ = object_updated_epoch_ = 0;
  object_latitude_ = object_longitude_ = object_heading_ = 0.0f;
  object_quality_ = ThreatPointQuality::UNKNOWN;
}

void ThreatScanner::finish_object_() {
  if (object_type_[0] == '\0') return;
  ++result_.entries;
  maybe_add_point_();
  const int index = region_alert_index(
      region_id_from_neptun_oblast(object_region_));
  // Keep unknown-region diagnostics for every received threat record.
  if (index < 0) ++result_.unknown_regions;
  // Completed threats and advisories must not inflate the visible counters.
  // Missing status keeps the existing compatibility with older responses.
  if (!object_active_ || object_advisory_) return;
  add_type(&result_.global, object_type_);
  if (index < 0) return;
  add_type(&result_.regions[static_cast<size_t>(index)], object_type_);
  add_type(&result_.active_regions[static_cast<size_t>(index)], object_type_);
}

bool ThreatScanner::threat_key_() const {
  static constexpr const char *keys[] = {
      "region", "type", "status", "advisory", "areaOnly", "id",
      "lat", "lon", "heading", "positionQuality", "updatedAt",
      "presumptiveCourse"};
  for (const auto *key : keys)
    if (std::strcmp(pending_key_, key) == 0) return true;
  return false;
}

void ThreatScanner::accept_string_() {
  if (object_depth_ == 1 && array_depth_ == 0 &&
      std::strcmp(pending_key_, "serverTime") == 0) {
    parse_iso8601_utc(token_, &result_.server_epoch);
    return;
  }
  if (!in_threats_array_ || object_depth_ != 2U ||
      array_depth_ != threats_array_depth_) return;
  if (std::strcmp(pending_key_, "region") == 0) {
    if (token_len_ >= sizeof(object_region_)) { parse_error_ = true; return; }
    std::strncpy(object_region_, token_, sizeof(object_region_) - 1);
  } else if (std::strcmp(pending_key_, "type") == 0)
    std::strncpy(object_type_, token_, sizeof(object_type_) - 1);
  else if (std::strcmp(pending_key_, "status") == 0) {
    object_status_seen_ = true;
    object_active_ = object_active_ && std::strcmp(token_, "active") == 0;
  } else if (std::strcmp(pending_key_, "id") == 0)
    object_id_hash_ = threat_id_hash(token_);
  else if (std::strcmp(pending_key_, "positionQuality") == 0)
    object_quality_ = threat_point_quality(token_);
  else if (std::strcmp(pending_key_, "updatedAt") == 0)
    parse_iso8601_utc(token_, &object_updated_epoch_);
  else if (std::strcmp(pending_key_, "advisory") == 0)
    object_advisory_ = true;
  else if (std::strcmp(pending_key_, "areaOnly") == 0)
    object_area_only_ = true;
  else if (std::strcmp(pending_key_, "presumptiveCourse") == 0)
    object_presumptive_course_ = true;
}

void ThreatScanner::accept_primitive_() {
  if (std::strcmp(pending_key_, "advisory") == 0) {
    object_advisory_ = token_overflow_ || std::strcmp(token_, "false") != 0;
  } else if (std::strcmp(pending_key_, "areaOnly") == 0) {
    object_area_only_ = token_overflow_ || std::strcmp(token_, "false") != 0;
  } else if (std::strcmp(pending_key_, "presumptiveCourse") == 0) {
    object_presumptive_course_ = token_overflow_ || std::strcmp(token_, "false") != 0;
  } else if (std::strcmp(pending_key_, "lat") == 0) {
    object_lat_seen_ = !token_overflow_ && parse_float(token_, &object_latitude_);
  } else if (std::strcmp(pending_key_, "lon") == 0) {
    object_lon_seen_ = !token_overflow_ && parse_float(token_, &object_longitude_);
  } else if (std::strcmp(pending_key_, "heading") == 0) {
    object_heading_seen_ = !token_overflow_ && parse_float(token_, &object_heading_);
  } else if (std::strcmp(pending_key_, "status") == 0) {
    object_status_seen_ = true;
    object_active_ = false;
  }
}

void ThreatScanner::finish_string_() {
  if (token_overflow_) {
    if (expect_value_ && in_threats_array_) object_active_ = false;
    reset_token_();
    clear_pending_();
    return;
  }
  token_[token_len_] = '\0';
  if (expect_value_) accept_string_();
  else {
    std::strncpy(pending_key_, token_, sizeof(pending_key_) - 1);
    pending_key_[sizeof(pending_key_) - 1] = '\0';
  }
  reset_token_();
  if (expect_value_) clear_pending_();
}

void ThreatScanner::finish_primitive_() {
  token_[token_len_] = '\0';
  if (in_threats_array_ && object_depth_ == 2U &&
      array_depth_ == threats_array_depth_) accept_primitive_();
  reset_token_();
  clear_pending_();
}

void ThreatScanner::string_character_(char value) {
  const auto result = string_decoder_.consume(value);
  if (result.kind == JsonStringDecoder::END) {
    in_string_ = false;
    finish_string_();
  } else if (result.kind == JsonStringDecoder::ERROR) {
    parse_error_ = true;
  } else if (result.kind != JsonStringDecoder::MORE &&
             !append_json_character(result, token_, sizeof(token_), token_len_)) {
    token_overflow_ = true;
  }
}

void ThreatScanner::primitive_character_(char value) {
  if (value == ',' || value == '}' || value == ']' ||
      std::isspace(static_cast<unsigned char>(value))) {
    in_primitive_ = false;
    finish_primitive_();
    process_(value);
    return;
  }
  if (token_len_ + 1 < sizeof(token_)) token_[token_len_++] = value;
  else token_overflow_ = true;
}

void ThreatScanner::colon_() {
  if (object_depth_ == 1 && array_depth_ == 0 &&
      std::strcmp(pending_key_, "threats") == 0) {
    expect_threats_array_ = true;
    clear_pending_();
    return;
  }
  if (object_depth_ == 1 && array_depth_ == 0 &&
      std::strcmp(pending_key_, "serverTime") == 0) {
    expect_value_ = true;
    return;
  }
  expect_value_ = in_threats_array_ && object_depth_ == 2U &&
      array_depth_ == threats_array_depth_ && threat_key_();
  if (!expect_value_) clear_pending_();
}

void ThreatScanner::insert_point_(const ThreatPoint &point) {
  size_t position = 0;
  while (position < stored_points_ && newer(result_.points[position], point))
    ++position;
  if (position >= THREAT_POINT_CAPACITY) return;
  const size_t end = std::min(stored_points_, THREAT_POINT_CAPACITY - 1);
  for (size_t index = end; index > position; --index)
    result_.points[index] = result_.points[index - 1];
  result_.points[position] = point;
  if (stored_points_ < THREAT_POINT_CAPACITY) ++stored_points_;
}

void ThreatScanner::maybe_add_point_() {
  const auto kind = threat_point_kind(object_type_);
  if (!object_status_seen_ || !object_active_ || object_advisory_ ||
      object_area_only_ || kind == ThreatPointKind::UNSUPPORTED ||
      object_quality_ == ThreatPointQuality::UNKNOWN || object_id_hash_ == 0 ||
      object_updated_epoch_ == 0 || !object_lat_seen_ || !object_lon_seen_)
    return;
  float x = 0.0f, y = 0.0f;
  if (!threat_source_point(object_latitude_, object_longitude_, &x, &y)) return;
  ThreatPoint point{object_id_hash_, object_updated_epoch_, object_latitude_,
                    object_longitude_, -1, kind, object_quality_};
  if (object_heading_seen_ && !object_presumptive_course_ &&
      object_heading_ >= 0.0f && object_heading_ < 360.0f)
    point.heading_deg = static_cast<int16_t>(std::lround(object_heading_));
  ++result_.point_candidates;
  insert_point_(point);
}

void ThreatScanner::finalize_points_() {
  size_t output = 0;
  for (size_t index = 0; index < stored_points_; ++index) {
    const auto &point = result_.points[index];
    if (!threat_point_fresh(result_.server_epoch, point.updated_epoch)) continue;
    if (output != index) result_.points[output] = point;
    ++output;
  }
  for (size_t index = output; index < result_.points.size(); ++index)
    result_.points[index] = {};
  result_.point_count = static_cast<uint16_t>(output);
  result_.point_dropped_capacity = result_.point_candidates > THREAT_POINT_CAPACITY
      ? result_.point_candidates - THREAT_POINT_CAPACITY : 0;
}

void ThreatScanner::structure_(char value) {
  if (value == '{') {
    if (in_threats_array_ && object_depth_ == 1 &&
        array_depth_ == threats_array_depth_) start_object_();
    ++object_depth_;
  } else if (value == '}') {
    if (in_threats_array_ && object_depth_ == 2 &&
        array_depth_ == threats_array_depth_) finish_object_();
    if (object_depth_ == 0) parse_error_ = true;
    else if (--object_depth_ == 0) {
      finalize_points_();
      root_closed_ = true;
    }
  } else if (value == '[') {
    ++array_depth_;
    if (expect_threats_array_) {
      expect_threats_array_ = false;
      in_threats_array_ = true;
      threats_array_depth_ = array_depth_;
      result_.saw_threats = true;
    }
  } else if (value == ']') {
    if (in_threats_array_ && array_depth_ == threats_array_depth_)
      in_threats_array_ = false;
    if (array_depth_ == 0) parse_error_ = true;
    else --array_depth_;
  }
  clear_pending_();
}

void ThreatScanner::process_(char value) {
  if (in_string_) {
    string_character_(value);
    return;
  }
  if (in_primitive_) {
    primitive_character_(value);
    return;
  }
  if (std::isspace(static_cast<unsigned char>(value))) return;
  if (root_closed_) {
    parse_error_ = true;
    return;
  }
  if (expect_value_ && value != '"') {
    in_primitive_ = true;
    reset_token_();
    primitive_character_(value);
    return;
  }
  if (value == '"') {
    in_string_ = true;
    reset_token_();
    return;
  }
  if (value == ':') {
    colon_();
    return;
  }
  if (value == '{' || value == '}' || value == '[' || value == ']')
    structure_(value);
  else if (value == ',') clear_pending_();
}

}  // namespace esphome::usp_neptun

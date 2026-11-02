#include "neptun_alert_parser.h"

#include <algorithm>
#include <cstring>

namespace esphome::usp_neptun {

namespace {

struct RegionNameMap {
  const char *name;
  uint16_t region_id;
};

constexpr uint16_t REGION_IDS[REGION_ALERT_COUNT] = {
    3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 31, 9999};

constexpr RegionNameMap REGION_NAMES[] = {
    {"Хмельницька область", 3},
    {"Вінницька область", 4},
    {"Рівненська область", 5},
    {"Волинська область", 8},
    {"Дніпропетровська область", 9},
    {"Житомирська область", 10},
    {"Закарпатська область", 11},
    {"Запорізька область", 12},
    {"Івано-Франківська область", 13},
    {"Київська область", 14},
    {"Кіровоградська область", 15},
    {"Луганська область", 16},
    {"Миколаївська область", 17},
    {"Одеська область", 18},
    {"Полтавська область", 19},
    {"Сумська область", 20},
    {"Тернопільська область", 21},
    {"Харківська область", 22},
    {"Херсонська область", 23},
    {"Черкаська область", 24},
    {"Чернігівська область", 25},
    {"Чернівецька область", 26},
    {"Львівська область", 27},
    {"Донецька область", 28},
    {"м. Київ", 31},
    {"Київ", 31},
    {"місто Київ", 31},
    {"Автономна Республіка Крим", 9999},
    {"АР Крим", 9999},
    {"Севастополь", 9999},
};

/** Accept ordinary whitespace and the two common non-breaking spaces. */
size_t space_width(const unsigned char *value) {
  if (*value == ' ' || *value == '\t' || *value == '\n' ||
      *value == '\r' || *value == '\f' || *value == '\v') return 1;
  if (value[0] == 0xC2 && value[1] == 0xA0) return 2;
  if (value[0] == 0xE2 && value[1] == 0x80 && value[2] == 0xAF) return 3;
  return 0;
}

/** Trim and collapse spaces into a bounded name while preserving letters. */
bool normalize_region_name(const char *name, char *output, size_t capacity) {
  if (name == nullptr || capacity == 0) return false;
  const auto *input = reinterpret_cast<const unsigned char *>(name);
  size_t length = 0;
  bool pending_space = false;
  while (*input) {
    const size_t width = space_width(input);
    if (width) {
      pending_space = length > 0;
      input += width;
      continue;
    }
    if (pending_space) {
      if (length + 1 >= capacity) return false;
      output[length++] = ' ';
      pending_space = false;
    }
    if (length + 1 >= capacity) return false;
    output[length++] = static_cast<char>(*input++);
  }
  output[length] = '\0';
  return length > 0;
}

uint8_t level_from_text(const char *value) {
  if (std::strcmp(value, "yellow") == 0) {
    return static_cast<uint8_t>(RegionAlertLevel::YELLOW);
  }
  if (std::strcmp(value, "red") == 0) {
    return static_cast<uint8_t>(RegionAlertLevel::RED);
  }
  return UINT8_MAX;
}

}  // namespace

int region_alert_index(uint16_t region_id) {
  for (size_t index = 0; index < REGION_ALERT_COUNT; ++index) {
    if (REGION_IDS[index] == region_id) return static_cast<int>(index);
  }
  return -1;
}

uint16_t region_id_from_neptun_oblast(const char *name) {
  char normalized[64]{};
  if (!normalize_region_name(name, normalized, sizeof(normalized))) return 0;
  for (const auto &entry : REGION_NAMES) {
    if (std::strcmp(normalized, entry.name) == 0) return entry.region_id;
  }
  return 0;
}

void AlertScanner::feed(const char *data, size_t len) {
  if (data == nullptr || len == 0 || result_.overflow) return;
  result_.bytes += static_cast<uint32_t>(len);
  if (result_.bytes > MAX_BODY_BYTES) { result_.overflow = true; return; }
  for (size_t index = 0; index < len; ++index) process_(data[index]);
}

void AlertScanner::reset_token_() {
  token_len_ = 0;
  token_[0] = '\0';
  token_overflow_ = false;
  string_decoder_.reset();
}

void AlertScanner::clear_pending_() {
  pending_key_[0] = '\0';
  expect_value_ = false;
}

void AlertScanner::start_object_() {
  object_oblast_[0] = object_name_[0] = object_level_[0] = '\0';
}

void AlertScanner::finish_object_() {
  if (object_level_[0] == '\0') return;
  const char *name = object_oblast_[0] ? object_oblast_ : object_name_;
  ++result_.entries;
  const int index = region_alert_index(region_id_from_neptun_oblast(name));
  const uint8_t level = level_from_text(object_level_);
  if (index < 0) { ++result_.unknown_regions; return; }
  if (level == UINT8_MAX) { ++result_.unknown_levels; return; }
  result_.levels[static_cast<size_t>(index)] = std::max(
      result_.levels[static_cast<size_t>(index)], level);
}

void AlertScanner::accept_value_() {
  if (!in_alert_array_ || object_depth_ != 2 || array_depth_ != alert_array_depth_) {
    clear_pending_(); return;
  }
  if (std::strcmp(pending_key_, "oblast") == 0) {
    if (token_len_ >= sizeof(object_oblast_)) { parse_error_ = true; return; }
    std::strncpy(object_oblast_, token_, sizeof(object_oblast_) - 1);
    object_oblast_[sizeof(object_oblast_) - 1] = '\0';
  } else if (std::strcmp(pending_key_, "name") == 0) {
    if (token_len_ >= sizeof(object_name_)) { parse_error_ = true; return; }
    std::strncpy(object_name_, token_, sizeof(object_name_) - 1);
    object_name_[sizeof(object_name_) - 1] = '\0';
  } else if (std::strcmp(pending_key_, "level") == 0) {
    if (token_len_ >= sizeof(object_level_)) { parse_error_ = true; return; }
    std::strncpy(object_level_, token_, sizeof(object_level_) - 1);
    object_level_[sizeof(object_level_) - 1] = '\0';
  }
  clear_pending_();
}

void AlertScanner::finish_string_() {
  if (token_overflow_) {
    if (expect_value_) parse_error_ = true;
    reset_token_(); clear_pending_(); return;
  }
  token_[token_len_] = '\0';
  if (expect_value_) accept_value_();
  else {
    std::strncpy(pending_key_, token_, sizeof(pending_key_) - 1);
    pending_key_[sizeof(pending_key_) - 1] = '\0';
  }
  reset_token_();
}

void AlertScanner::string_character_(char value) {
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

void AlertScanner::colon_() {
  if (object_depth_ == 1 && array_depth_ == 0) {
    if (std::strcmp(pending_key_, "raions") == 0) expect_array_ = 1;
    if (std::strcmp(pending_key_, "oblasts") == 0) expect_array_ = 2;
    clear_pending_(); return;
  }
  expect_value_ = in_alert_array_ && object_depth_ == 2 &&
      array_depth_ == alert_array_depth_ &&
      (std::strcmp(pending_key_, "oblast") == 0 ||
       std::strcmp(pending_key_, "name") == 0 ||
       std::strcmp(pending_key_, "level") == 0);
  if (!expect_value_) clear_pending_();
}

void AlertScanner::array_(char value) {
  if (value == '[') {
    ++array_depth_;
    if (expect_array_) {
      seen_arrays_ |= expect_array_;
      expect_array_ = 0;
      in_alert_array_ = true;
      alert_array_depth_ = array_depth_;
      result_.saw_alerts = true;
    }
  } else {
    if (in_alert_array_ && array_depth_ == alert_array_depth_) in_alert_array_ = false;
    if (array_depth_ == 0) parse_error_ = true;
    else --array_depth_;
  }
  clear_pending_();
}

void AlertScanner::object_(char value) {
  if (value == '{') {
    if (in_alert_array_ && object_depth_ == 1 && array_depth_ == alert_array_depth_)
      start_object_();
    ++object_depth_;
  } else {
    if (in_alert_array_ && object_depth_ == 2 && array_depth_ == alert_array_depth_)
      finish_object_();
    if (object_depth_ == 0) parse_error_ = true;
    else if (--object_depth_ == 0) root_closed_ = true;
  }
  clear_pending_();
}

void AlertScanner::process_(char value) {
  if (in_string_) { string_character_(value); return; }
  if (value == ' ' || value == '\t' || value == '\n' || value == '\r') return;
  if (root_closed_) { parse_error_ = true; return; }
  if (value == '"') { in_string_ = true; reset_token_(); return; }
  if (value == ':') { colon_(); return; }
  if (value == '[' || value == ']') { array_(value); return; }
  if (value == '{' || value == '}') { object_(value); return; }
  if (value == ',') clear_pending_();
}
}  // namespace esphome::usp_neptun

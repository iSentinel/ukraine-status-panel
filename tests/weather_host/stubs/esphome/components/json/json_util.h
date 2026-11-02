#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <string>
namespace esphome::json {
inline JsonDocument parse_json(const uint8_t *data, size_t len) {
  JsonDocument document;
  if (data == nullptr || deserializeJson(document, data, len)) document.clear();
  return document;
}
inline JsonDocument parse_json(const std::string &body) {
  return parse_json(reinterpret_cast<const uint8_t *>(body.data()), body.size());
}
}  // namespace esphome::json

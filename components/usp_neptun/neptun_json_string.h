#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome::usp_neptun {
/** Decode one JSON string incrementally without allocating memory. */
class JsonStringDecoder {
 public:
  enum Kind { MORE, BYTE, CODEPOINT, END, ERROR };
  struct Result { Kind kind; uint32_t value{0}; };

  void reset() {
    escaped_ = false;
    digits_ = low_prefix_ = 0;
    code_ = high_ = 0;
  }

  Result consume(char input) {
    const auto value = static_cast<uint8_t>(input);
    if (digits_) return unicode_character_(value);
    if (high_) return surrogate_prefix_(value);
    if (escaped_) return escape_character_(value);
    if (value == '"') return {END};
    if (value == '\\') { escaped_ = true; return {MORE}; }
    if (value < 0x20) return {ERROR};
    return {BYTE, value};
  }

 private:
  static int hex_(uint8_t value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  }

  Result escape_character_(uint8_t value) {
    escaped_ = false;
    switch (value) {
      case '"': case '\\': case '/': return {BYTE, value};
      case 'b': return {BYTE, '\b'};
      case 'f': return {BYTE, '\f'};
      case 'n': return {BYTE, '\n'};
      case 'r': return {BYTE, '\r'};
      case 't': return {BYTE, '\t'};
      case 'u': digits_ = 4; code_ = 0; return {MORE};
      default: return {ERROR};
    }
  }

  Result surrogate_prefix_(uint8_t value) {
    if (low_prefix_ == 1 && value == '\\') {
      low_prefix_ = 2;
      return {MORE};
    }
    if (low_prefix_ == 2 && value == 'u') {
      low_prefix_ = 0; digits_ = 4; code_ = 0;
      return {MORE};
    }
    return {ERROR};
  }

  Result unicode_character_(uint8_t value) {
    const int hex = hex_(value);
    if (hex < 0) return {ERROR};
    code_ = (code_ << 4U) | static_cast<uint32_t>(hex);
    if (--digits_) return {MORE};
    if (high_) {
      if (code_ < 0xDC00 || code_ > 0xDFFF) return {ERROR};
      const uint32_t point = 0x10000U + ((high_ - 0xD800U) << 10U) +
          (code_ - 0xDC00U);
      high_ = 0;
      return {CODEPOINT, point};
    }
    if (code_ >= 0xD800 && code_ <= 0xDBFF) {
      high_ = code_; low_prefix_ = 1;
      return {MORE};
    }
    // Embedded NUL cannot safely represent a field consumed as a C string.
    if (code_ == 0 || (code_ >= 0xDC00 && code_ <= 0xDFFF)) return {ERROR};
    return {CODEPOINT, code_};
  }

  bool escaped_{false};
  uint8_t digits_{0}, low_prefix_{0};
  uint32_t code_{0}, high_{0};
};

/** Append one decoded value atomically, always preserving a trailing NUL slot. */
inline bool append_json_character(JsonStringDecoder::Result result,
                                  char *buffer, size_t capacity, size_t &length) {
  char bytes[4]{};
  size_t count = 1;
  const uint32_t value = result.value;
  if (result.kind == JsonStringDecoder::BYTE || value <= 0x7F) {
    bytes[0] = static_cast<char>(value);
  } else if (value <= 0x7FF) {
    count = 2;
    bytes[0] = static_cast<char>(0xC0 | (value >> 6U));
    bytes[1] = static_cast<char>(0x80 | (value & 0x3F));
  } else if (value <= 0xFFFF) {
    count = 3;
    bytes[0] = static_cast<char>(0xE0 | (value >> 12U));
    bytes[1] = static_cast<char>(0x80 | ((value >> 6U) & 0x3F));
    bytes[2] = static_cast<char>(0x80 | (value & 0x3F));
  } else {
    count = 4;
    bytes[0] = static_cast<char>(0xF0 | (value >> 18U));
    bytes[1] = static_cast<char>(0x80 | ((value >> 12U) & 0x3F));
    bytes[2] = static_cast<char>(0x80 | ((value >> 6U) & 0x3F));
    bytes[3] = static_cast<char>(0x80 | (value & 0x3F));
  }
  if (length >= capacity || count >= capacity - length) return false;
  for (size_t index = 0; index < count; ++index) buffer[length++] = bytes[index];
  return true;
}
}  // namespace esphome::usp_neptun

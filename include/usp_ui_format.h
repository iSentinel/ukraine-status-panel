#pragma once

#include <cstdint>
#include <string>

namespace usp_ui {

/**
 * Convert an unsigned integer to decimal text without printf.
 */
inline std::string unsigned_decimal(uint32_t value) {
  char digits[10];
  size_t used = 0;

  do {
    digits[used++] = static_cast<char>('0' + (value % 10U));
    value /= 10U;
  } while (value != 0U);

  std::string text;
  text.reserve(used);
  while (used > 0U) {
    text.push_back(digits[--used]);
  }
  return text;
}

/**
 * Convert a signed integer to decimal text without printf.
 */
inline std::string signed_decimal(int32_t value) {
  if (value >= 0) {
    return unsigned_decimal(static_cast<uint32_t>(value));
  }

  const auto magnitude =
      static_cast<uint32_t>(-static_cast<int64_t>(value));
  return std::string("-") + unsigned_decimal(magnitude);
}

/**
 * Round a finite float to the nearest integer and return decimal text.
 */
inline std::string rounded_decimal(float value) {
  const float adjusted = value >= 0.0F ? value + 0.5F : value - 0.5F;
  return signed_decimal(static_cast<int32_t>(adjusted));
}

/**
 * Format a finite float with exactly two decimal places without printf.
 */
inline std::string fixed_two(float value) {
  const bool negative = value < 0.0F;
  const float magnitude = negative ? -value : value;
  const auto scaled =
      static_cast<uint32_t>(magnitude * 100.0F + 0.5F);
  const uint32_t whole = scaled / 100U;
  const uint32_t fraction = scaled % 100U;

  std::string text = negative ? "-" : "";
  text += unsigned_decimal(whole);
  text.push_back('.');
  text.push_back(static_cast<char>('0' + fraction / 10U));
  text.push_back(static_cast<char>('0' + fraction % 10U));
  return text;
}

}  // namespace usp_ui

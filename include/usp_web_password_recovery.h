#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "esp_random.h"

namespace usp_web_password_recovery {

// Sixteen random bytes provide 128 bits. Lowercase hexadecimal also fits
// the existing LCD button without reducing its font size.
inline std::string make_password() {
  static constexpr char alphabet[] = "0123456789abcdef";
  std::array<uint8_t, 16> random{};
  esp_fill_random(random.data(), random.size());
  std::string password;
  password.reserve(random.size() * 2U);
  for (const uint8_t value : random) {
    password.push_back(alphabet[value >> 4U]);
    password.push_back(alphabet[value & 15U]);
  }
  return password;
}

}  // namespace usp_web_password_recovery

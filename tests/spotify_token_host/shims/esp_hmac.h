#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

enum hmac_key_id_t : int {
  HMAC_KEY0 = 0,
  HMAC_KEY1 = 1,
  HMAC_KEY2 = 2,
  HMAC_KEY3 = 3,
  HMAC_KEY4 = 4,
  HMAC_KEY5 = 5,
  HMAC_KEY_MAX = 6,
};

inline esp_err_t esp_hmac_calculate(
    hmac_key_id_t, const void *, size_t, uint8_t *) {
  return ESP_FAIL;
}

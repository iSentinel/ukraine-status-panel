#pragma once

#include <cstdint>

enum esp_efuse_block_t : int {
  EFUSE_BLK_KEY0 = 4,
  EFUSE_BLK_KEY1 = 5,
  EFUSE_BLK_KEY2 = 6,
  EFUSE_BLK_KEY3 = 7,
  EFUSE_BLK_KEY4 = 8,
  EFUSE_BLK_KEY5 = 9,
};

enum esp_efuse_purpose_t : int {
  ESP_EFUSE_KEY_PURPOSE_USER = 0,
  ESP_EFUSE_KEY_PURPOSE_HMAC_UP = 8,
};

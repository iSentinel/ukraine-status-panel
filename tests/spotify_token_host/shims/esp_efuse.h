#pragma once

#include "esp_efuse_chip.h"

inline bool esp_efuse_key_block_unused(esp_efuse_block_t) { return true; }
inline esp_efuse_purpose_t esp_efuse_get_key_purpose(esp_efuse_block_t) {
  return ESP_EFUSE_KEY_PURPOSE_USER;
}
inline bool esp_efuse_get_key_dis_read(esp_efuse_block_t) { return false; }
inline bool esp_efuse_get_key_dis_write(esp_efuse_block_t) { return false; }
inline bool esp_efuse_get_keypurpose_dis_write(esp_efuse_block_t) { return false; }

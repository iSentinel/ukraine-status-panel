#pragma once

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace esphome {

inline std::unordered_map<uint32_t, std::vector<uint8_t>> host_preferences_data;

class ESPPreferenceObject {
 public:
  explicit ESPPreferenceObject(uint32_t key = 0) : key_(key) {}

  template<typename T> bool load(T *value) const {
    const auto found = host_preferences_data.find(key_);
    if (!value || found == host_preferences_data.end() ||
        found->second.size() != sizeof(T)) return false;
    std::memcpy(value, found->second.data(), sizeof(T));
    return true;
  }

  template<typename T> bool save(const T *value) const {
    if (!value) return false;
    const auto *first = reinterpret_cast<const uint8_t *>(value);
    host_preferences_data[key_] = std::vector<uint8_t>(first, first + sizeof(T));
    return true;
  }

 private:
  uint32_t key_{0};
};

class ESPPreferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t key, bool) {
    return ESPPreferenceObject(key);
  }
  bool sync() { return true; }
};

inline ESPPreferences host_preferences;
inline ESPPreferences *global_preferences = &host_preferences;

}  // namespace esphome

#pragma once
#include "component.h"
namespace esphome {
struct ESPPreferenceObject {
  template<typename T> bool load(T *) const { return false; }
  template<typename T> bool save(const T *) const { return true; }
};
struct Preferences {
  template<typename T> ESPPreferenceObject make_preference(uint32_t, bool) { return {}; }
};
inline Preferences host_preferences;
inline Preferences *global_preferences = &host_preferences;
}

#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace usp_weather_timezone {
/** Accept bounded provider identifiers; this does not resolve an IANA database. */
inline bool valid_name(const std::string &name) {
  if (name.empty() || name.size() > 63 || name.front() == '/' ||
      name.back() == '/' || name.find("//") != std::string::npos) return false;
  return std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' || c == '/';
  });
}
/** Bound contemporary civil UTC offsets without deriving future DST rules. */
inline bool valid_offset(int32_t seconds) {
  return seconds >= -43200 && seconds <= 50400 && seconds % 60 == 0;
}
/** Render an explicit response offset; missing context stays unavailable. */
inline std::string offset_label(int32_t seconds, bool valid) {
  if (!valid || !valid_offset(seconds)) return "--";
  const unsigned minutes = static_cast<unsigned>(seconds < 0 ? -seconds : seconds) / 60;
  char text[16];
  std::snprintf(text, sizeof(text), "UTC%c%02u:%02u",
                seconds < 0 ? '-' : '+', minutes / 60, minutes % 60);
  return text;
}
}  // namespace usp_weather_timezone

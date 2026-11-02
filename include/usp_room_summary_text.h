#pragma once

namespace usp_room_summary_text {

// Keep format types and English fallback exactly as the existing branches.
inline constexpr const char *FORMATS[3] = {
  "%u вимірів · %u пропусків",
  "%u измерений · %u пропусков",
  "%u samples · %u gaps",
};

constexpr const char *format(unsigned language) {
  return FORMATS[language < 2 ? language : 2];
}

}  // namespace usp_room_summary_text

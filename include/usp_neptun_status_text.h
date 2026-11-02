#pragma once

namespace usp_neptun_status_text {

// Named labels preserve the existing English fallback for other languages.
enum class Label : unsigned {
  AIR_ALERT = 0,
  AIR_THREAT = 1,
  NO_ACTIVE = 2,
  CACHED_SUFFIX = 3,
  RECON = 4,
};

inline constexpr const char *LABELS[5][3] = {
  {"Повітряна тривога", "Воздушная тревога", "Air alert"},
  {"Повітряна загроза", "Воздушная угроза", "Air threat"},
  {"Немає активної", "Нет активной", "No active"},
  {" · кеш", " · кэш", " · cached"},
  {"РОЗВ", "РАЗВ", "REC"},
};

// Invalid keys produce no text and never access the table.
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  const auto lang = language < 2 ? language : 2;
  return index < 5 ? LABELS[index][lang] : "";
}

}  // namespace usp_neptun_status_text

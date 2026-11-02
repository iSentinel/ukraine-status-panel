#pragma once

namespace usp_wifi_status_text {

// Named labels preserve the existing English fallback for other languages.
enum class Label : unsigned {
  ONLINE = 0,
  OFFLINE = 1,
};

inline constexpr const char *LABELS[2][3] = {
  {"В мережі", "В сети", "Online"},
  {"Офлайн", "Офлайн", "Offline"},
};

// Invalid keys produce no text and never access the table.
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  const auto lang = language < 2 ? language : 2;
  return index < 2 ? LABELS[index][lang] : "";
}

}  // namespace usp_wifi_status_text

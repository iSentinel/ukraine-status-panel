#pragma once
namespace usp_live_badge_text {
enum class Label : unsigned { NO_DATA = 0, LIVE = 1, STALE = 2 };
inline constexpr const char *LABELS[3][3] = {
  {"НЕМАЄ ДАНИХ", "АКТУАЛЬНО", "ЗАСТАРІЛО"},
  {"НЕТ ДАННЫХ", "АКТУАЛЬНО", "УСТАРЕЛО"},
  {"NO DATA", "LIVE", "STALE"}
};
// The existing badge uses English for every language other than0/1.
constexpr unsigned language_index(unsigned language) {
  return language < 2 ? language : 2;
}
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  return index < 3 ? LABELS[language_index(language)][index] : "";
}
}  // namespace usp_live_badge_text

#pragma once
namespace usp_fullscreen_context_text {
enum class Quality : unsigned { MISSING = 0, LIVE = 1, STALE = 2 };
inline constexpr const char *QUALITY[3][3] = {
    {"НЕМАЄ ДАНИХ", "АКТУАЛЬНО", "ЗАСТАРІЛО"},
    {"НЕТ ДАННЫХ", "АКТУАЛЬНО", "УСТАРЕЛО"},
    {"NO DATA", "LIVE", "STALE"}
};
inline constexpr const char *CLEAR[] = {"Відбій", "Отбой", "Clear"};
inline constexpr const char *ALERT[] = {"Тривога", "Тревога", "Alert"};
/** Preserve Ukrainian as the established fallback language. */
inline constexpr unsigned language_index(unsigned language) {
  return language < 3 ? language : 0;
}
/** Return a caption without choosing or changing the current data state. */
inline const char *quality(unsigned language, Quality value) {
  unsigned index = static_cast<unsigned>(value);
  return QUALITY[language_index(language)][index < 3 ? index : 0];
}
/** Return the established clear caption without choosing a state. */
inline const char *clear_label(unsigned language) {
  return CLEAR[language_index(language)];
}
/** Return the established alert caption without choosing a state. */
inline const char *alert_label(unsigned language) {
  return ALERT[language_index(language)];
}
}

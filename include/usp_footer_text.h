#pragma once

namespace usp_footer_text {

enum class Label : unsigned {
  HOME = 0,
  MAP = 1,
  POWER = 2,
  HISTORY = 3,
  SETTINGS = 4,
};

inline constexpr const char *LABELS[3][5] = {
    {"ГОЛОВНА", "МАПА", "ЖИВЛЕННЯ", "ІСТОРІЯ", "НАЛАШТУВАННЯ"},
    {"ГЛАВНАЯ", "КАРТА", "ПИТАНИЕ", "ИСТОРИЯ", "НАСТРОЙКИ"},
    {"HOME", "MAP", "POWER", "HISTORY", "SETTINGS"},
};

/** Keep the established localization fallback for catalog-only callers. */
inline unsigned language_index(unsigned language) {
  return language < 3 ? language : 0;
}

struct TextRef {
  const char *value;
  constexpr const char *c_str() const { return value; }
};

/** Return one of the five established main-page footer captions without allocation. */
inline constexpr TextRef label(unsigned language, Label label) {
  const auto index = static_cast<unsigned>(label);
  return {index < 5 ? LABELS[language_index(language)][index] : ""};
}

}  // namespace usp_footer_text

#pragma once

namespace usp_diagnostics_text {

/** Stable semantic keys for Diagnostics captions and touch-test feedback. */
enum class Label : unsigned {
  PAGE_TITLE = 0,
  BACK = 1,
  HARDWARE_SECTION = 2,
  RGB_DISPLAY = 3,
  BOUNCE_BUFFER = 4,
  BOUNCE_VALUE = 5,
  TOUCH = 6,
  TOUCH_POLLING = 7,
  RUNTIME_SECTION = 8,
  TOUCH_TEST = 9,
  TOUCH_OK = 10
};

inline constexpr const char *LABELS[3][11] = {
  {"ДІАГНОСТИКА СИСТЕМИ", "НАЗАД", "ОБЛАДНАННЯ", "RGB екран", "DMA-буфер", "20 рядків", "Сенсор", "Опитування 50 мс", "РОБОТА", "ТЕСТ ДОТИКУ", "ДОТИК: OK"},
  {"ДИАГНОСТИКА СИСТЕМЫ", "НАЗАД", "ОБОРУДОВАНИЕ", "RGB экран", "DMA-буфер", "20 строк", "Сенсор", "Опрос 50 мс", "РАБОТА", "ТЕСТ КАСАНИЯ", "КАСАНИЕ: OK"},
  {"SYSTEM DIAGNOSTICS", "BACK", "HARDWARE", "RGB display", "Bounce buffer", "20 lines", "Touch", "Polling 50 ms", "RUNTIME", "TOUCH TEST", "TOUCH: OK"}
};

struct TextRef {
  const char *value;
  constexpr const char *c_str() const { return value; }
};

/** Preserve the established Ukrainian fallback for unexpected language ids. */
inline constexpr unsigned language_index(unsigned language) {
  return language < 3 ? language : 0;
}

/** Return immutable accepted text without allocation or widget side effects. */
inline constexpr TextRef label(unsigned language, Label key) {
  const unsigned index = static_cast<unsigned>(key);
  return {index < 11 ? LABELS[language_index(language)][index] : ""};
}

}  // namespace usp_diagnostics_text

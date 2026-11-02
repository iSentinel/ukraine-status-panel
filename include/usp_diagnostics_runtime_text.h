#pragma once
namespace usp_diagnostics_runtime_text {
enum class Field : unsigned { STACK=0, UPTIME, VOLTAGE, HEAP, PSRAM, BACKLIGHT };
struct Parts { const char *prefix; const char *unit; };
inline constexpr Parts TABLE[3][6] = {
  {{"Стек: ", " Б"}, {"Час роботи: ", " с"}, {"Напруга: ", " В"}, {"Heap: ", " кБ"}, {"PSRAM: ", " кБ"}, {"Підсвітка: ", "%"}},
  {{"Стек: ", " Б"}, {"Время работы: ", " с"}, {"Напряжение: ", " В"}, {"Heap: ", " кБ"}, {"PSRAM: ", " кБ"}, {"Подсветка: ", "%"}},
  {{"Stack: ", " B"}, {"Uptime: ", " s"}, {"Voltage: ", " V"}, {"Heap: ", " kB"}, {"PSRAM: ", " kB"}, {"Backlight: ", "%"}},
};
inline constexpr Parts parts(unsigned language, Field field) {
  const unsigned lang = language < 3 ? language : 2;
  const unsigned key = static_cast<unsigned>(field);
  return key < 6 ? TABLE[lang][key] : Parts{"", ""};
}
}  // namespace usp_diagnostics_runtime_text

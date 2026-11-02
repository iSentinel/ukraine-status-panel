#pragma once
namespace usp_sound_controls_text {
enum class Label : unsigned { SOUND_ON = 0, SOUND_OFF = 1, BUZZER_TEST = 2 };
inline constexpr const char *LABELS[3][3] = {
  {"ЗВУК: УВІМК", "ЗВУК: ВИМК", "ТЕСТ БУЗЕРА"},
  {"ЗВУК: ВКЛ", "ЗВУК: ВЫКЛ", "ТЕСТ БУЗЗЕРА"},
  {"SOUND: ON", "SOUND: OFF", "BUZZER TEST"}
};
constexpr unsigned language_index(unsigned language) {
  return language < 2 ? language : 2;
}
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  return index < 3 ? LABELS[language_index(language)][index] : "";
}
}  // namespace usp_sound_controls_text

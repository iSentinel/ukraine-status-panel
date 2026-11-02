#pragma once
namespace usp_restart_confirm_text {
enum class Label : unsigned { RESTART = 0, CONFIRM = 1 };
inline constexpr const char *LABELS[3][2] = {
  {"ПЕРЕЗАПУСК", "ЩЕ РАЗ?"},
  {"ПЕРЕЗАПУСК", "ЕЩЁ РАЗ?"},
  {"RESTART", "CONFIRM?"}
};
constexpr unsigned language_index(unsigned language) { return language < 2 ? language : 2; }
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  return index < 2 ? LABELS[language_index(language)][index] : "";
}
}  // namespace usp_restart_confirm_text

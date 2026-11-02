#pragma once

namespace usp_optional_hardware_text {

enum class Status : unsigned { NOT_FOUND = 0, STARTING };

inline constexpr const char *TABLE[3][2] = {
    {"НЕМАЄ", "ЗАПУСК"},
    {"НЕТ", "ЗАПУСК"},
    {"NOT FOUND", "STARTING"},
};

inline constexpr const char *label(unsigned language, Status status) {
  const unsigned lang = language < 3 ? language : 2;
  const unsigned key = static_cast<unsigned>(status);
  return key < 2 ? TABLE[lang][key] : "";
}

}  // namespace usp_optional_hardware_text

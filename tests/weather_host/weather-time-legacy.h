// Actual validators captured from accepted 0.115.279-dev.
// Kept only to verify parser compatibility; production does not include this file.
#pragma once
#include <string>
namespace legacy {
bool valid_date(const std::string &date) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
  for (size_t index = 0; index < date.size(); ++index) {
    if (index != 4 && index != 7 && (date[index] < '0' || date[index] > '9')) return false;
  }
  const int year = std::stoi(date.substr(0, 4));
  const int month = std::stoi(date.substr(5, 2));
  const int day = std::stoi(date.substr(8, 2));
  if (year < 1970 || month < 1 || month > 12) return false;
  static constexpr int DAYS[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day >= 1 && day <= DAYS[month - 1] + (month == 2 && leap ? 1 : 0);
}

bool valid_timestamp(const std::string &text) {
  if ((text.size() != 16 && text.size() != 19) || !valid_date(text.substr(0, 10)) ||
      text[10] != 'T' || text[13] != ':') return false;
  for (size_t index : {11U,12U,14U,15U}) {
    if (text[index] < '0' || text[index] > '9') return false;
  }
  if (std::stoi(text.substr(11, 2)) > 23 || std::stoi(text.substr(14, 2)) > 59) return false;
  return text.size() == 16 ||
         (text[16] == ':' && text[17] >= '0' && text[17] <= '5' && text[18] >= '0' && text[18] <= '9');
}

}

#pragma once
#include "usp_clock_presentation.h"
#include <string>

namespace usp_alarm_ui {
inline const char *pick(uint8_t language, const char *ua,
                        const char *ru, const char *en) {
  return usp_clock::pick(language, ua, ru, en);
}
inline const char *title(uint8_t language) {
  return pick(language, "Будильники", "Будильники", "Alarms");
}
inline const char *days(uint8_t language) {
  return pick(language, "Щодня\nПн–Пт\nСб–Нд",
              "Ежедневно\nПн–Пт\nСб–Вс", "Daily\nWeekdays\nWeekends");
}
// Compact preset buttons are always visible; full days are shown below.
inline const char *preset(uint8_t language, unsigned index) {
  if (index == 1) return pick(language, "Пн–Пт", "Пн–Пт", "Mon–Fri");
  if (index == 2) return pick(language, "Сб–Нд", "Сб–Вс", "Sat–Sun");
  return pick(language, "Усі", "Все", "Daily");
}
inline const char *summary(uint8_t language, unsigned index) {
  if (index == 1) return pick(language,
      "Понеділок · Вівторок · Середа · Четвер · П'ятниця",
      "Понедельник · Вторник · Среда · Четверг · Пятница",
      "Monday · Tuesday · Wednesday · Thursday · Friday");
  if (index == 2) return pick(language, "Субота · Неділя",
      "Суббота · Воскресенье", "Saturday · Sunday");
  return pick(language,
      "Понеділок · Вівторок · Середа · Четвер · П'ятниця · Субота · Неділя",
      "Понедельник · Вторник · Среда · Четверг · Пятница · Суббота · Воскресенье",
      "Monday · Tuesday · Wednesday · Thursday · Friday · Saturday · Sunday");
}
inline uint8_t mask(unsigned selected) {
  constexpr uint8_t masks[] = {127, 62, 65};
  return selected < 3 ? masks[selected] : 0;
}
inline unsigned selected(uint8_t mask) {
  return mask == 62 ? 1 : (mask == 65 ? 2 : 0);
}
inline const char *status(uint8_t language, bool storage, uint8_t error,
                          bool clock, uint8_t active, bool enabled) {
  if (!storage) return pick(language, "Помилка збереження", "Ошибка сохранения", "Storage error");
  if (error == 2) return pick(language, "Дані змінено: відкрийте сторінку знову",
      "Данные изменены: откройте страницу снова", "Changed elsewhere: reopen this page");
  if (error) return pick(language, "Перевірте час ЧЧ:ХХ", "Проверьте время ЧЧ:ММ", "Check HH:MM");
  if (active) return pick(language, "Будильник спрацював", "Будильник сработал", "Alarm active");
  if (!enabled) return pick(language, "Усі будильники вимкнено", "Все будильники выключены", "All alarms disabled");
  if (!clock) return pick(language, "Очікування синхронізації", "Ожидание синхронизации", "Waiting for time sync");
  return pick(language, "Розклад збережено · час Києва", "Расписание сохранено · время Киева", "Schedule saved · Kyiv time");
}
}

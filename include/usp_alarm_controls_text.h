      #pragma once
      namespace usp_alarm_controls_text {
      enum class Label : unsigned { CLOCK = 0, TIME = 1, REPEAT = 2, NOTE = 3, SOUND_ON = 4, SOUND_OFF = 5, ON = 6, OFF = 7, SAVE = 8, NOTIFY_SOUND_ON = 9, NOTIFY_SOUND_OFF = 10, DISMISS = 11 };
      inline constexpr const char *LABELS[12][3] = {
        {"Годинник", "Часы", "Clock"},
{"Час", "Время", "Time"},
{"Повтор", "Повтор", "Repeat"},
{"Після «Зберегти» · Час Києва · Звук будильника окремо", "После «Сохранить» · Время Киева · Звук будильника отдельно", "Changes need Save · Kyiv time · Alarm sound is separate"},
{"ЗВУК: УВІМК", "ЗВУК: ВКЛ", "SOUND: ON"},
{"ЗВУК: ВИМК", "ЗВУК: ВЫКЛ", "SOUND: OFF"},
{"УВІМК", "ВКЛ", "ON"},
{"ВИМК", "ВЫКЛ", "OFF"},
{"Зберегти", "Сохранить", "Save"},
{"Звук будильника увімкнено", "Звук будильника включён", "Alarm sound on"},
{"Звук будильника вимкнено", "Звук будильника выключен", "Alarm sound off"},
{"Закрити", "Закрыть", "Dismiss"}
      };
      constexpr unsigned language_index(unsigned language) { return language < 2 ? language : 2; }
      constexpr const char *label(unsigned language, Label key) {
        const auto index = static_cast<unsigned>(key);
        return index < 12 ? LABELS[index][language_index(language)] : "";
      }
      }  // namespace usp_alarm_controls_text

      #include <cassert>
      #include <cstring>
      #include <iostream>
      #include "usp_alarm_controls_text.h"
      int main() {
        using usp_alarm_controls_text::Label;
        const char *gold[12][3] = {
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
{"Закрити", "Закрыть", "Dismiss"},
        };
        const Label keys[] = {
          Label::CLOCK, Label::TIME, Label::REPEAT, Label::NOTE, Label::SOUND_ON, Label::SOUND_OFF, Label::ON, Label::OFF, Label::SAVE, Label::NOTIFY_SOUND_ON, Label::NOTIFY_SOUND_OFF, Label::DISMISS
        };
        unsigned named = 0, invalid = 0;
        for (unsigned language = 0; language < 256; ++language) {
          const unsigned li = language < 2 ? language : 2;
          for (unsigned key = 0; key < 12; ++key) {
            assert(std::strcmp(usp_alarm_controls_text::label(language, keys[key]), gold[key][li]) == 0);
            ++named;
          }
          for (unsigned key : {12u, 255u}) {
            assert(std::strcmp(usp_alarm_controls_text::label(language, static_cast<Label>(key)), "") == 0);
            ++invalid;
          }
        }
        std::cout << "ALARM_CONTROLS_PREFLIGHT_R2=PASS named=" << named << " invalid=" << invalid << "\n";
      }

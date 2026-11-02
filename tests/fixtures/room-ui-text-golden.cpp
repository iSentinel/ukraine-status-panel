      #include <cassert>
      #include <cstring>
      #include <iostream>
      #include "usp_room_ui_text.h"
      int main() {
        using usp_room_ui_text::Label;
        const char *gold[16][3] = {
        {"В кімнаті", "В комнате", "Room"},
{"24 год · крок 5 хв", "24 ч · шаг 5 мин", "24 h · 5 min"},
{"В приміщенні", "В помещении", "Indoor"},
{"Надворі", "На улице", "Outdoor"},
{"Історія 24 год", "История 24 ч", "24h history"},
{"ПОГОДА", "ПОГОДА", "WEATHER"},
{"ГОЛОВНА", "ГЛАВНАЯ", "HOME"},
{"SHT41/SHT45 · датчик не підключено", "SHT41/SHT45 · датчик не подключен", "SHT41/SHT45 · sensor not connected"},
{"SHT41/SHT45 · очікування даних", "SHT41/SHT45 · ожидание данных", "SHT41/SHT45 · waiting for data"},
{"Історія в RAM/PSRAM · після restart починається заново", "История в RAM/PSRAM · после restart начинается заново", "RAM/PSRAM history · restarts begin a new history"},
{"ТЕМПЕРАТУРА · 24 ГОД", "ТЕМПЕРАТУРА · 24 Ч", "TEMPERATURE · 24H"},
{"ВОЛОГІСТЬ · 24 ГОД", "ВЛАЖНОСТЬ · 24 Ч", "HUMIDITY · 24H"},
{"Немає історії", "Нет истории", "No history"},
{"-24 год", "-24 ч", "-24 h"},
{"зараз", "сейчас", "now"},
{"Останні сегменти; раніше пропущено", "Последние сегменты; ранее пропущено", "Latest segments; earlier runs omitted"},
        };
        const Label keys[] = {
          Label::ROOM, Label::PERIOD, Label::INDOOR, Label::OUTDOOR, Label::HISTORY_24H, Label::WEATHER, Label::HOME, Label::SENSOR_ABSENT, Label::SENSOR_WAITING, Label::HISTORY_NOTE, Label::TEMP_24H, Label::HUMIDITY_24H, Label::NO_HISTORY, Label::LEFT_24H, Label::NOW, Label::DROPPED_PREFIX
        };
        unsigned named=0, invalid=0;
        for(unsigned language=0; language<256; ++language) {
          const unsigned li=language<2?language:2;
          for(unsigned key=0; key<16; ++key) {
            assert(std::strcmp(usp_room_ui_text::label(language, keys[key]), gold[key][li])==0);
            ++named;
          }
          for(unsigned key : {16u,255u}) {
            assert(std::strcmp(usp_room_ui_text::label(language, static_cast<Label>(key)), "")==0);
            ++invalid;
          }
        }
        std::cout << "ROOM_UI_PREFLIGHT=PASS named=" << named << " invalid=" << invalid << "\n";
      }

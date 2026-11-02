#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_clock_ui_text.h"

int main() {
 using usp_clock_ui_text::Label;
 const Label keys[] = {Label::CLOCK, Label::OUTDOOR_WEATHER, Label::CLEAR, Label::ALERT, Label::REGION_STATUS, Label::MAP_UNAVAILABLE};
 const char *gold[6][3] = {
  {"Годинник", "Часы", "Clock"},
  {"Погода надворі", "Погода на улице", "Outdoor weather"},
  {"Відбій", "Отбой", "Clear"},
  {"Тривога", "Тревога", "Alert"},
  {"Статус регіону", "Статус региона", "Region status"},
  {"Карта недоступна", "Карта недоступна", "Map unavailable"},
 };
 unsigned named=0, invalid=0;
 for(unsigned language=0; language<256; ++language) {
  const unsigned li=language<2?language:2;
  for(unsigned key=0;key<6;++key) {
   assert(std::strcmp(usp_clock_ui_text::label(language, keys[key]),gold[key][li])==0);
   ++named;
  }
  for(unsigned key: {6u,255u}) {
   assert(std::strcmp(usp_clock_ui_text::label(language,static_cast<Label>(key)),"")==0);
   ++invalid;
  }
 }
 std::cout<<"CLOCK_UI_PREFLIGHT=PASS named="<<named<<" invalid="<<invalid<<"\n";
}

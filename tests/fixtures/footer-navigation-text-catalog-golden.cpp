
#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_footer_text.h"
static const char *golden[3][5] = {
  {"ГОЛОВНА","МАПА","ЖИВЛЕННЯ","ІСТОРІЯ","НАЛАШТУВАННЯ"},
  {"ГЛАВНАЯ","КАРТА","ПИТАНИЕ","ИСТОРИЯ","НАСТРОЙКИ"},
  {"HOME","MAP","POWER","HISTORY","SETTINGS"}};
int main() {
  const usp_footer_text::Label keys[] = {
    usp_footer_text::Label::HOME, usp_footer_text::Label::MAP,
    usp_footer_text::Label::POWER, usp_footer_text::Label::HISTORY,
    usp_footer_text::Label::SETTINGS};
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    const unsigned old = language < 3 ? language : 0;
    for (unsigned index = 0; index < 5; ++index) {
      const auto key = keys[index];
      assert(std::strcmp(golden[old][index], usp_footer_text::label(language, key).c_str()) == 0);
      ++checked;
    }
  }
  assert(checked == 1280);
  assert(std::strcmp("", usp_footer_text::label(0, static_cast<usp_footer_text::Label>(99)).c_str()) == 0);
  std::cout << "FOOTER_NAVIGATION_TEXT_CATALOG_NATIVE=PASS checked=" << checked << "\n";
}

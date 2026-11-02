#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_fullscreen_context_text.h"
static const char *old_quality[3][3] = {
    {"НЕМАЄ ДАНИХ", "АКТУАЛЬНО", "ЗАСТАРІЛО"},
    {"НЕТ ДАННЫХ", "АКТУАЛЬНО", "УСТАРЕЛО"},
    {"NO DATA", "LIVE", "STALE"}
};
static const char *old_clear[] = {"Відбій", "Отбой", "Clear"};
static const char *old_alert[] = {"Тривога", "Тревога", "Alert"};
int main() {
  using namespace usp_fullscreen_context_text;
  const Quality keys[] = {Quality::MISSING, Quality::LIVE, Quality::STALE};
  unsigned checked = 0;
  for (unsigned lang = 0; lang < 256; ++lang) {
    const unsigned old = lang < 3 ? lang : 0;
    for (unsigned index = 0; index < 3; ++index) {
      assert(std::strcmp(old_quality[old][index], quality(lang, keys[index])) == 0);
      ++checked;
    }
    assert(std::strcmp(old_clear[old], clear_label(lang)) == 0);
    assert(std::strcmp(old_alert[old], alert_label(lang)) == 0);
    checked += 2;
  }
  assert(std::strcmp(old_quality[0][0], quality(0, static_cast<Quality>(99))) == 0);
  assert(checked == 1280);
  std::cout << "FULLSCREEN_CONTEXT_TEXT_NATIVE=PASS checked=" << checked << "\n";
}

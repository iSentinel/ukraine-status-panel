#include "usp_sound_controls_text.h"
#include <cassert>
#include <iostream>
#include <string>
std::string sound_baseline(unsigned language, bool enabled) {
if (language == 0U) {
  return enabled ? std::string("ЗВУК: УВІМК") : std::string("ЗВУК: ВИМК");
}
if (language == 1U) {
  return enabled ? std::string("ЗВУК: ВКЛ") : std::string("ЗВУК: ВЫКЛ");
}
return enabled ? std::string("SOUND: ON") : std::string("SOUND: OFF");
}
std::string sound_candidate(unsigned language, bool enabled) {
if (language == 0U) {
  return enabled ? std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::SOUND_ON)) : std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::SOUND_OFF));
}
if (language == 1U) {
  return enabled ? std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::SOUND_ON)) : std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::SOUND_OFF));
}
return enabled ? std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::SOUND_ON)) : std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::SOUND_OFF));
}
std::string buzzer_baseline(unsigned language) {
if (language == 0U) return std::string("ТЕСТ БУЗЕРА");
if (language == 1U) return std::string("ТЕСТ БУЗЗЕРА");
return std::string("BUZZER TEST");
}
std::string buzzer_candidate(unsigned language) {
if (language == 0U) return std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::BUZZER_TEST));
if (language == 1U) return std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::BUZZER_TEST));
return std::string(usp_sound_controls_text::label(language, usp_sound_controls_text::Label::BUZZER_TEST));
}
constexpr const char *EXPECTED[3][3] = {
  {"ЗВУК: УВІМК", "ЗВУК: ВИМК", "ТЕСТ БУЗЕРА"},
  {"ЗВУК: ВКЛ", "ЗВУК: ВЫКЛ", "ТЕСТ БУЗЗЕРА"},
  {"SOUND: ON", "SOUND: OFF", "BUZZER TEST"}
};
int main() {
  using usp_sound_controls_text::Label;
  const Label keys[] = {Label::SOUND_ON, Label::SOUND_OFF, Label::BUZZER_TEST};
  unsigned callback_checked = 0;
  unsigned named_checked = 0;
  unsigned invalid_checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    for (bool enabled : {false, true}) {
      assert(sound_baseline(language, enabled) == sound_candidate(language, enabled));
      ++callback_checked;
    }
    assert(buzzer_baseline(language) == buzzer_candidate(language));
    ++callback_checked;
    for (unsigned key = 0; key < 3; ++key) {
      assert(std::string(usp_sound_controls_text::label(language, keys[key]))
             == EXPECTED[language < 2 ? language : 2][key]);
      ++named_checked;
    }
    for (unsigned key : {3u, 255u}) {
      assert(std::string(usp_sound_controls_text::label(
          language, static_cast<Label>(key))).empty());
      ++invalid_checked;
    }
  }
  std::cout << "SOUND_CONTROLS_NATIVE=PASS callback=" << callback_checked
            << " named=" << named_checked << " invalid=" << invalid_checked << std::endl;
}

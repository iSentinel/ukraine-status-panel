#include "usp_restart_confirm_text.h"
#include <cassert>
#include <cstring>
#include <iostream>

const char *accepted(unsigned raw_language, bool pending) {
  const char *normal[] = {"ПЕРЕЗАПУСК", "ПЕРЕЗАПУСК", "RESTART"};
  const char *confirm[] = {"ЩЕ РАЗ?", "ЕЩЁ РАЗ?", "CONFIRM?"};
  const unsigned language = raw_language < 3 ? raw_language : 0;
  return pending ? confirm[language] : normal[language];
}

const char *candidate(unsigned raw_language, bool pending) {
  const unsigned language = raw_language < 3 ? raw_language : 0;
  return usp_restart_confirm_text::label(
      language, pending ? usp_restart_confirm_text::Label::CONFIRM
                        : usp_restart_confirm_text::Label::RESTART);
}

int main() {
  using usp_restart_confirm_text::Label;
  const char *named[3][2] = {
      {"ПЕРЕЗАПУСК", "ЩЕ РАЗ?"},
      {"ПЕРЕЗАПУСК", "ЕЩЁ РАЗ?"},
      {"RESTART", "CONFIRM?"},
  };
  unsigned callback = 0;
  unsigned named_checked = 0;
  unsigned invalid = 0;
  for (unsigned language = 0; language < 256; ++language) {
    for (bool pending : {false, true}) {
      assert(std::strcmp(accepted(language, pending), candidate(language, pending)) == 0);
      ++callback;
    }
    const unsigned li = language < 2 ? language : 2;
    for (unsigned key = 0; key < 2; ++key) {
      assert(std::strcmp(
          usp_restart_confirm_text::label(language, static_cast<Label>(key)),
          named[li][key]) == 0);
      ++named_checked;
    }
    for (unsigned key : {2u, 255u}) {
      assert(std::strcmp(
          usp_restart_confirm_text::label(language, static_cast<Label>(key)), "") == 0);
      ++invalid;
    }
  }
  std::cout << "RESTART_CONFIRM_NATIVE=PASS callback=" << callback
            << " named=" << named_checked << " invalid=" << invalid << "\n";
}

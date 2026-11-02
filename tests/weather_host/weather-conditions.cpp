#include "weather_client.h"
#include "usp_weather_codes.h"
#include "usp_ui_i18n_0_115_107.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string>

namespace codes = usp_weather_codes;

// Check supported provider values and retain unknown values as unknown.
static void all_codes() {
  constexpr std::array<uint8_t, 29> known{
      0,1,2,3,45,48,51,53,55,56,57,61,63,65,66,67,
      71,73,75,77,80,81,82,85,86,95,96,97,99};
  for (unsigned code = 0; code < 256; ++code) {
    const bool expected = std::find(known.begin(), known.end(), code) != known.end();
    assert((codes::lookup(code) != nullptr) == expected);
    assert((codes::group(code) != codes::UNKNOWN) == expected);
    for (uint8_t language = 0; language < 3; ++language) {
      const std::string text = codes::description(code, language);
      assert(!text.empty());
      if (expected) {
        const char *compact = codes::compact_description(text, language);
        assert(compact != nullptr);
        assert(usp_ui_i18n::compact_weather_condition(text, language) == compact);
      }
    }
  }
  assert(std::string(codes::description(200, 2)) == "Unknown");
  assert(std::string(codes::description(66, 255)) == codes::description(66, 2));
  assert(codes::compact_description("User text", 0) == nullptr);
}

// Hazard distinctions come from provider codes, never from air temperature.
static void ice_and_hail() {
  for (unsigned code = 0; code < 256; ++code) {
    const bool ice = code == 48 || code == 56 || code == 57 || code == 66 || code == 67;
    assert(codes::is_ice_related(code) == ice);
    assert(codes::has_hail(code) == (code == 96 || code == 99));
  }
  assert(std::string(codes::description(56, 2)).find("freezing") != std::string::npos);
  assert(std::string(codes::description(66, 2)).find("freezing") != std::string::npos);
  assert(std::string(codes::description(96, 2)).find("hail") != std::string::npos);
  for (uint8_t lang = 0; lang < 3; ++lang) {
    assert(std::string(codes::description(45, lang)) != codes::description(48, lang));
    assert(std::string(codes::description(95, lang)) != codes::description(96, lang));
    assert(std::string(codes::description(96, lang)) != codes::description(99, lang));
  }
  assert(codes::group(97) == codes::THUNDERSTORM);
  assert(std::string(usp_ui_i18n::compact_weather_icon(97)) == "⚡");
}

// Check intensity differences and the real component formatter.
static void intensity_and_component() {
  using I = codes::Intensity;
  assert(codes::intensity(61) == I::LIGHT);
  assert(codes::intensity(63) == I::MODERATE);
  assert(codes::intensity(65) == I::HEAVY);
  assert(codes::intensity(55) == I::DENSE);
  assert(codes::intensity(82) == I::VIOLENT);
  assert(codes::intensity(97) == I::HEAVY);
  assert(codes::intensity(200) == I::NONE);
  using Client = esphome::usp_weather::WeatherClient;
  assert(Client::condition_text(66, 2) != Client::condition_text(61, 2));
  assert(Client::condition_text(96, 2) != Client::condition_text(95, 2));
  assert(Client::condition_text(65, 2) != Client::condition_text(61, 2));
  assert(Client::condition_text(97, 2) != "Unknown");
}

int main() {
  all_codes();
  ice_and_hail();
  intensity_and_component();
  std::puts("WX3_NATIVE=PASS codes256 known29 languages3 ice hail intensity code97");
}

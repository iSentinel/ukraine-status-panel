#include "weather_client.h"
#include <array>
#include <atomic>
#include <thread>
#include <cassert>
#include <cstdio>
#include <string>

class ProbeWeather : public esphome::usp_weather::WeatherClient {
 public:
  using WeatherClient::build_search_url_;
  using WeatherClient::search_request_copy_;
  using WeatherClient::publish_search_;
  using WeatherClient::parse_search_;
  using WeatherClient::SearchResult;
  ProbeWeather() { use_default_city_(); }
};

static void url_languages() {
  ProbeWeather client;
  for (unsigned language = 0; language < 256; ++language) {
    const char *expected = language == 0 ? "uk" : language == 1 ? "ru" : "en";
    const auto url = client.build_search_url_("Kyiv &x=1", language);
    assert(url.find(std::string("&language=") + expected + "&format=json") != std::string::npos);
    assert(url.find("name=Kyiv%20%26x%3D1&count=3") != std::string::npos);
    assert(url.find("?name=") == url.rfind("?name="));
  }
  assert(client.build_search_url_("Kyiv").find("&language=en&") != std::string::npos);
  assert(client.build_search_url_("Київ", 0).find(
      "%D0%9A%D0%B8%D1%97%D0%B2") != std::string::npos);
}

// A language-only change must supersede the same query's older response.
static void language_ownership() {
  ProbeWeather client;
  assert(client.request_city_search("Kyiv", 2));
  const auto old = client.search_request_copy_();
  assert(old.query == "Kyiv" && old.language == 2);
  assert(client.request_city_search("Kyiv", 0));
  const auto current = client.search_request_copy_();
  assert(current.language == 0 && current.generation == old.generation + 1);
  std::array<ProbeWeather::SearchResult, ProbeWeather::SEARCH_RESULTS> rows{};
  assert(!client.publish_search_(old, rows));
  assert(client.publish_search_(current, rows));
  assert(old.language == 2 && old.query == "Kyiv");
  assert(client.city_name() == "Kyiv");  // Searching does not select a city.
  assert(client.request_city_search(" Kyiv ", 255));
  assert(client.search_request_copy_().language == 2);
  assert(!client.request_city_search(" ", 0));
}

// Unicode results use the existing parser and appear only after explicit selection.
static void localized_selection() {
  ProbeWeather client;
  assert(client.request_city_search("Kyiv", 0));
  std::array<ProbeWeather::SearchResult, ProbeWeather::SEARCH_RESULTS> rows{};
  const std::string body = R"({"results":[{"id":703448,"name":"Київ","country":"Україна","latitude":50.45466,"longitude":30.5238,"timezone":"Europe/Kyiv"}]})";
  assert(client.parse_search_(reinterpret_cast<const uint8_t *>(body.data()), body.size(), &rows));
  assert(client.publish_search_(client.search_request_copy_(), rows));
  assert(client.city_name() == "Kyiv");
  assert(client.select_search_result(0));
  assert(client.city_name() == "Київ" && client.country_name() == "Україна");
  assert(!client.snapshot().valid);
}

// A copied request must never combine one query with another query's language.
static void concurrent_request_copies() {
  ProbeWeather client;
  assert(client.request_city_search("UA", 0));
  std::atomic<bool> done{false};
  std::thread writer([&]() {
    for (unsigned i = 0; i < 10000; ++i) {
      if (i % 2) assert(client.request_city_search("UA", 0));
      else assert(client.request_city_search("RU", 1));
    }
    done = true;
  });
  unsigned reads = 0;
  do {
    const auto view = client.search_request_copy_();
    assert((view.query == "UA" && view.language == 0) ||
           (view.query == "RU" && view.language == 1));
    ++reads;
  } while (!done || reads < 10000);
  writer.join();
}

int main() {
  url_languages();
  language_ownership();
  localized_selection();
  concurrent_request_copies();
  std::puts("WX2_NATIVE=PASS languages256 URL encoding same_query_generation ownership explicit_selection unicode concurrent10000");
}

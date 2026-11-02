#include "weather_client.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

class ProbeWeather : public esphome::usp_weather::WeatherClient {
 public:
  ProbeWeather() { use_default_city_(); }
  void publish(float temperature, uint8_t code) {
    WeatherSnapshot value;
    value.valid = true;
    value.fetch_ok = true;
    value.temperature_c = temperature;
    value.weather_code = code;
    value.received_ms = esphome::millis();
    value.forecast_count = 1;
    value.days[0].high_c = temperature;
    value.days[0].code = code;
    std::snprintf(value.days[0].date, sizeof(value.days[0].date), "%s", "2026-10-23");
    assert(publish_forecast_(forecast_request_copy_(), std::move(value), true));
  }
  bool choose_city() {
    assert(request_city_search("Lviv"));
    std::array<SearchResult, SEARCH_RESULTS> rows{};
    const std::string body = R"({"results":[{"id":1,"name":"Lviv","country":"Ukraine","latitude":49.84,"longitude":24.03,"timezone":"Europe/Kyiv"}]})";
    assert(parse_search_(reinterpret_cast<const uint8_t *>(body.data()),
                         body.size(), &rows));
    assert(publish_search_(search_request_copy_(), std::move(rows)));
    return select_search_result(0);
  }
  void fail_fetch() {
    assert(publish_forecast_(forecast_request_copy_(), {}, false));
  }
};

// A retained copy must not change after a newer forecast is published.
static void retained_snapshot() {
  ProbeWeather client;
  assert(!client.snapshot().valid && client.snapshot().stale_latched);
  client.publish(7.5F, 2);
  const auto old = client.snapshot();
  client.publish(22.0F, 95);
  const auto latest = client.snapshot();
  assert(old.valid && old.temperature_c == 7.5F && old.weather_code == 2);
  assert(old.days[0].high_c == 7.5F && old.days[0].code == 2);
  assert(latest.temperature_c == 22.0F && latest.weather_code == 95);
  assert(latest.sequence == old.sequence + 1);
  assert(latest.city == client.city_name());
  assert(latest.country == client.country_name());
  assert(ProbeWeather::condition_text(old.weather_code, 0) ==
         ProbeWeather::condition_text(2, 0));
  client.fail_fetch();
  assert(!client.snapshot().fetch_ok && old.fetch_ok);
}

// Selecting another city invalidates the new view without changing a saved copy.
static void city_ownership() {
  ProbeWeather client;
  client.publish(7.5F, 2);
  const auto old = client.snapshot();
  assert(client.choose_city());
  const auto current = client.snapshot();
  assert(current.city == "Lviv" && !current.valid);
  assert(current.stale_latched && current.forecast_count == 0);
  assert(current.sequence == old.sequence + 1);
  assert(old.valid && old.forecast_count == 1);
  assert(old.city != "Lviv" && std::string(old.days[0].date) == "2026-10-23");
}

// A freshness latch must keep its existing strict boundary and wrap behavior.
static void freshness() {
  ProbeWeather client;
  client.set_stale_after(100);
  esphome::test_millis = UINT32_MAX - 10;
  client.publish(7.5F, 2);
  esphome::test_millis += 100;
  assert(!client.snapshot().stale_latched);
  ++esphome::test_millis;
  assert(client.snapshot().stale_latched);
  esphome::test_millis = UINT32_MAX - 10;
  assert(client.snapshot().stale_latched);
  client.publish(22.0F, 95);
  assert(!client.snapshot().stale_latched);
}

// The reader must see either complete generation even during real threading.
static void concurrent_snapshots() {
  ProbeWeather client;
  client.publish(7.5F, 2);
  std::atomic<bool> done{false};
  std::thread writer([&]() {
    for (unsigned i = 0; i < 10000; ++i) {
      if (i % 2) client.publish(7.5F, 2);
      else client.publish(22.0F, 95);
    }
    done = true;
  });
  unsigned reads = 0;
  do {
    const auto view = client.snapshot();
    const bool first = view.temperature_c == 7.5F && view.weather_code == 2;
    const bool second = view.temperature_c == 22.0F && view.weather_code == 95;
    assert(view.valid && (first || second));
    assert(view.forecast_count == 1);
    assert(view.days[0].high_c == view.temperature_c);
    assert(view.days[0].code == view.weather_code);
    ++reads;
  } while (!done || reads < 10000);
  writer.join();
}

int main() {
  retained_snapshot();
  freshness();
  city_ownership();
  concurrent_snapshots();
  std::puts("WX1_SNAPSHOT_NATIVE=PASS owned_copy freshness wrap concurrent10000");
}

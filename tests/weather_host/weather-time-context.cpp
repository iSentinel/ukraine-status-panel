#include "weather-fixture.h"
#include "usp_weather_timezone.h"
#include <thread>

/** Keep response metadata and valid UTC zero distinct from missing context. */
void time_context_valid_cases() {
  struct Case { const char *zone; int32_t offset; const char *label; };
  const Case cases[] = {{"Europe/Kyiv", 10800, "UTC+03:00"},
      {"Europe/Kyiv", 7200, "UTC+02:00"}, {"Etc/UTC", 0, "UTC+00:00"},
      {"Asia/Kathmandu", 20700, "UTC+05:45"},
      {"Pacific/Kiritimati", 50400, "UTC+14:00"}, {"Etc/GMT+12", -43200, "UTC-12:00"}};
  TestWeather client;
  for (const auto &item : cases) {
    auto document = valid_payload();
    document["timezone"] = item.zone; document["utc_offset_seconds"] = item.offset;
    assert(client.apply(serialized(document)));
    const auto full = client.snapshot();
    const auto current = client.current_snapshot();
    assert(full.time_context_valid && current.time_context_valid);
    assert(full.timezone == item.zone && current.timezone == item.zone);
    assert(full.utc_offset_seconds == item.offset && current.utc_offset_seconds == item.offset);
    assert(full.updated_time == "2026-10-20T13:15");
    assert(usp_weather_timezone::offset_label(item.offset, true) == item.label);
    document["timezone"] = "Changed";
    assert(current.timezone == item.zone);
  }
  const auto previous = client.current_snapshot();
  assert(client.apply(serialized(valid_payload())));
  const auto missing = client.current_snapshot();
  assert(!missing.time_context_valid && missing.timezone.empty());
  assert(missing.utc_offset_seconds == 0 && previous.timezone == "Etc/GMT+12");
  assert(usp_weather_timezone::offset_label(0, false) == "--");
  auto document = valid_payload();
  document["timezone"] = "Europe/Kyiv"; document["utc_offset_seconds"] = 10800;
  assert(client.apply(serialized(document)));
  select(client, "Tokyo", 35.68, 139.69);
  const auto switched = client.current_snapshot();
  assert(!switched.valid && !switched.time_context_valid && switched.timezone.empty());
}

/** Bad metadata must not fabricate a timezone or reject otherwise good weather. */
void time_context_invalid_cases() {
  TestWeather client;
  const std::string bad_names[] = {"", "/Europe/Kyiv", "Europe/Kyiv/",
      "Europe//Kyiv", "Europe/Kyiv\n", std::string(64, 'a'), "Європа/Київ",
      "Europe/Kyiv<script>", std::string("Europe/Kyiv") + char(0) + "Bad"};
  for (const auto &name : bad_names) {
    auto document = valid_payload();
    document["timezone"] = name; document["utc_offset_seconds"] = 10800;
    TestWeather::WeatherSnapshot value;
    assert(client.parse_forecast_text(serialized(document), &value) && value.valid);
    assert(!value.time_context_valid && value.timezone.empty());
  }
  for (const char *raw : {"true", "null", "123", "[]", "{}"}) {
    auto document = valid_payload(); JsonDocument name;
    assert(!deserializeJson(name, raw));
    document["timezone"] = name.as<JsonVariantConst>();
    document["utc_offset_seconds"] = 10800;
    TestWeather::WeatherSnapshot value;
    assert(client.parse_forecast_text(serialized(document), &value) && value.valid);
    assert(!value.time_context_valid);
  }
  for (const char *raw : {"true", "null", "\"10800\"", "10800.5", "-43260",
                           "50460", "61", "999999999999", "[]", "{}"}) {
    auto document = valid_payload(); JsonDocument number;
    assert(!deserializeJson(number, raw));
    document["timezone"] = "Europe/Kyiv";
    document["utc_offset_seconds"] = number.as<JsonVariantConst>();
    TestWeather::WeatherSnapshot value;
    assert(client.parse_forecast_text(serialized(document), &value) && value.valid);
    assert(!value.time_context_valid);
  }
  assert(usp_weather_timezone::offset_label(50460, true) == "--");
  assert(usp_weather_timezone::offset_label(INT32_MIN, true) == "--");
}

/** Malformed provider text is rejected instead of accepting a truncated prefix. */
void embedded_control_bytes() {
  TestWeather client;
  auto document = valid_payload();
  document["current"]["time"] = std::string("2026-10-20T13:15") + char(0) + "Bad";
  assert(!client.decode(serialized(document)));
  document = valid_payload();
  document["daily"]["time"][0] = std::string("2026-10-20") + char(0) + "Bad";
  TestWeather::WeatherSnapshot snapshot;
  assert(client.parse_forecast_text(serialized(document), &snapshot));
  assert(snapshot.forecast_count == 1);
  assert(std::string(snapshot.days[0].date) == "2026-10-21");
  assert(!deserializeJson(document, city_result("Kyiv")));
  document["results"][0]["name"] = std::string("Kyiv") + char(0) + "Bad";
  std::array<TestWeather::SearchResult, 3> results{};
  client.parse_search_text(serialized(document), &results);
  assert(!results[0].valid);
}

/** A publication exposes timezone and offset together under both view APIs. */
void time_context_atomic_pairs() {
  TestWeather client; std::atomic<bool> finished{false};
  const auto request = client.forecast_request_copy_();
  std::thread publisher([&] {
    for (unsigned i = 0; i < 10000; ++i) {
      TestWeather::WeatherSnapshot next;
      next.valid = next.fetch_ok = next.time_context_valid = true;
      next.timezone = i % 2 ? "Asia/Kathmandu" : "Etc/UTC";
      next.utc_offset_seconds = i % 2 ? 20700 : 0;
      assert(client.publish_forecast_(request, std::move(next), true));
    }
    finished = true;
  });
  unsigned reads = 0;
  do {
    const auto current = client.current_snapshot();
    const auto full = client.snapshot();
    for (const auto *view : {&current, static_cast<const TestWeather::WeatherCurrent *>(&full)}) {
      if (!view->time_context_valid) continue;
      assert((view->timezone == "Etc/UTC" && view->utc_offset_seconds == 0) ||
             (view->timezone == "Asia/Kathmandu" && view->utc_offset_seconds == 20700));
    }
    ++reads;
  } while (!finished || reads < 10000);
  publisher.join();
}

int main() {
  time_context_valid_cases();
  time_context_invalid_cases();
  time_context_atomic_pairs();
  embedded_control_bytes();
  std::cout << "WEATHER_TIME_CONTEXT_NATIVE=PASS owning/city/UTC0/types/bounds/pairs10000\n";
}

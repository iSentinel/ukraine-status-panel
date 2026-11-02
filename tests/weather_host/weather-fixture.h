#pragma once
#include <cmath>
#include <fstream>
#include <iterator>
#include "weather_client.h"
#include "usp_ui_i18n_0_115_107.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>

/** Expose controlled native test operations without device access. */
class TestWeather : public esphome::usp_weather::WeatherClient {
 public:
  using WeatherClient::ForecastRequest;
  using WeatherClient::WeatherSnapshot;
  using WeatherClient::SearchResult;
  using WeatherClient::StoredCity;
  using WeatherClient::forecast_request_copy_;
  using WeatherClient::search_request_copy_;
  using WeatherClient::parse_forecast_;
  using WeatherClient::parse_search_;
  using WeatherClient::build_forecast_url_;
  using WeatherClient::publish_forecast_;
  using WeatherClient::publish_search_;
  using WeatherClient::condition_group_;
  using WeatherClient::condition_text_;
  using WeatherClient::use_default_city_;
  using WeatherClient::valid_stored_city_;
  using WeatherClient::city_from_search_result_;
  TestWeather() { use_default_city_(); }
  bool parse_forecast_text(
      const std::string &body, WeatherSnapshot *value) {
    return parse_forecast_(
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), value);
  }
  bool parse_search_text(
      const std::string &body,
      std::array<SearchResult, SEARCH_RESULTS> *values) {
    return parse_search_(
        reinterpret_cast<const uint8_t *>(body.data()), body.size(), values);
  }
  bool decode(const std::string &body) {
    WeatherSnapshot value;
    return parse_forecast_text(body, &value);
  }
  bool apply(const std::string &body) {
    WeatherSnapshot value;
    const bool ok = parse_forecast_text(body, &value);
    return publish_forecast_(forecast_request_copy_(), std::move(value), ok);
  }
  bool results(const std::string &body) {
    std::array<SearchResult, SEARCH_RESULTS> values{};
    const bool ok = parse_search_text(body, &values);
    return ok && publish_search_(search_request_copy_(), std::move(values));
  }
};

std::string serialized(const JsonDocument &document) {
  std::string text;
  serializeJson(document, text);
  return text;
}

JsonDocument valid_payload() {
  JsonDocument document;
  assert(!deserializeJson(document, R"({"current":{"time":"2026-10-20T13:15","temperature_2m":7.5,
    "apparent_temperature":5.0,"relative_humidity_2m":60,"weather_code":2,"is_day":1,"wind_speed_10m":12.3},
    "daily":{"time":["2026-10-20","2026-10-21"],"weather_code":[2,61],
    "temperature_2m_max":[9,10],"temperature_2m_min":[3,4],"precipitation_probability_max":[0,70]}})"));
  return document;
}

std::string city_result(const char *name, double latitude = 49.84, double longitude = 24.03) {
  JsonDocument document;
  auto row = document["results"].to<JsonArray>().add<JsonObject>();
  row["id"] = 100;
  row["name"] = name;
  row["country"] = "Ukraine";
  row["timezone"] = "Europe/Kyiv";
  row["latitude"] = latitude;
  row["longitude"] = longitude;
  return serialized(document);
}

void select(TestWeather &client, const char *name, double latitude = 49.84, double longitude = 24.03) {
  assert(client.request_city_search(name));
  assert(client.results(city_result(name, latitude, longitude)));
  assert(client.select_search_result(0));
}


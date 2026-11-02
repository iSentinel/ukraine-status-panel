#include "weather-fixture.h"

void required_current_values() {
  TestWeather client;
  assert(!client.decode(R"({"current":{},"daily":{}})"));
  assert(client.decode(serialized(valid_payload())));
  for (const char *field : {"temperature_2m", "apparent_temperature", "relative_humidity_2m",
                            "weather_code", "is_day", "wind_speed_10m", "time"}) {
    auto document = valid_payload();
    document["current"][field] = nullptr;
    assert(!client.decode(serialized(document)));
    document["current"].as<JsonObject>().remove(field);
    assert(!client.decode(serialized(document)));
    document["current"][field] = true;
    assert(!client.decode(serialized(document)));
  }
  for (const char *field : {"relative_humidity_2m", "is_day", "weather_code"}) {
    auto document = valid_payload();
    document["current"][field] = -1;
    assert(!client.decode(serialized(document)));
    document["current"][field] = 300;
    assert(!client.decode(serialized(document)));
  }
  auto document = valid_payload();
  document["current"]["temperature_2m"] = 1e30;
  assert(!client.decode(serialized(document)));
  document["current"]["temperature_2m"] = 0;
  document["current"]["wind_speed_10m"] = 0;
  document["current"]["relative_humidity_2m"] = 0;
  assert(client.decode(serialized(document)));  // Real zeros remain valid.
}

void forecast_rows_are_not_fabricated() {
  TestWeather client;
  auto document = valid_payload();
  document["daily"]["temperature_2m_max"][0] = nullptr;
  assert(client.apply(serialized(document)));
  assert(client.forecast_count() == 1 && client.forecast_date(0) == "2026-10-21");
  document["daily"]["precipitation_probability_max"][1] = 101;
  assert(client.apply(serialized(document)) && client.forecast_count() == 0);
  document = valid_payload();
  document["daily"]["time"][0] = "2026-02-30";
  document["daily"]["temperature_2m_min"][1] = 100;
  assert(client.apply(serialized(document)) && client.forecast_count() == 0);
  document = valid_payload();
  document["current"]["time"] = "2026-13-20T13:15";
  assert(!client.decode(serialized(document)));
}

void city_ownership() {
  TestWeather client;
  const auto old_request = client.forecast_request_copy_();
  assert(client.apply(serialized(valid_payload())) && client.weather_valid());
  select(client, "Lviv");
  assert(!client.weather_valid() && client.updated_time().empty());
  TestWeather::WeatherSnapshot old_value;
  assert(client.parse_forecast_text(serialized(valid_payload()), &old_value));
  assert(!client.publish_forecast_(old_request, old_value, true));
  assert(!client.publish_forecast_(old_request, old_value, false));
  assert(client.city_name() == "Lviv" && !client.weather_valid());
  assert(client.apply(serialized(valid_payload())) && client.last_fetch_ok());
  auto current = client.forecast_request_copy_();
  assert(client.publish_forecast_(current, {}, false));
  assert(client.weather_valid() && !client.last_fetch_ok() && client.temperature_c() == 7.5F);
  select(client, "Kyiv", 50.45466, 30.52380);
  assert(!client.publish_forecast_(old_request, old_value, true));  // A -> B -> A.
}

void search_ownership() {
  TestWeather client;
  assert(client.request_city_search("First"));
  const auto old = client.search_request_copy_();
  std::array<TestWeather::SearchResult, TestWeather::SEARCH_RESULTS> rows{};
  assert(client.parse_search_text(city_result("First"), &rows));
  assert(client.request_city_search("Second"));
  assert(!client.publish_search_(old, rows) && client.search_result_count() == 0);
  assert(client.results(city_result("Second")) && client.search_result_count() == 1);
  assert(client.request_city_search("Third") && !client.select_search_result(0));
  assert(client.results(R"({"results":[{}, {"id":1,"name":"Bad","latitude":null,"longitude":20}]})"));
  assert(client.search_result_count() == 0 && !client.select_search_result(0));
  assert(client.results(city_result("Third")) && client.search_result_count() == 1);
  assert(!client.request_city_search("  ") && !client.select_search_result(0));
  assert(client.search_result_count() == 0);
}

void stored_city_and_unicode() {
  TestWeather client;
  auto city = client.forecast_request_copy_().city;
  assert(client.valid_stored_city_(city));
  std::memset(city.name, 'x', sizeof(city.name));
  assert(!client.valid_stored_city_(city));
  city = client.forecast_request_copy_().city;
  std::memset(city.country, 'x', sizeof(city.country));
  assert(!client.valid_stored_city_(city));
  city = client.forecast_request_copy_().city;
  std::memset(city.timezone, 'x', sizeof(city.timezone));
  assert(!client.valid_stored_city_(city));
  city = client.forecast_request_copy_().city;
  city.latitude = std::numeric_limits<double>::quiet_NaN();
  assert(!client.valid_stored_city_(city));
  TestWeather::SearchResult result;
  for (int index = 0; index < 50; ++index) result.name += "К";
  city = client.city_from_search_result_(result);
  assert(std::strlen(city.name) == 62);  // Never split the two-byte letter.
}

void condition_codes_and_clock_wrap() {
  const std::map<int, int> groups = {{0,0},{1,1},{2,1},{3,2},{45,3},{48,3},
    {51,4},{53,4},{55,4},{56,4},{57,4},{61,5},{63,5},{65,5},{66,5},{67,5},
    {71,6},{73,6},{75,6},{77,6},{80,7},{81,7},{82,7},{85,8},{86,8},{95,9},{96,9},{97,9},{99,9}};
  for (int code = 0; code < 256; ++code) {
    const auto found = groups.find(code);
    const int expected = found == groups.end() ? 10 : found->second;
    assert(TestWeather::condition_group_(code) == expected);
  }
  assert(std::string(usp_ui_i18n::compact_weather_icon(200)) == "?");
  assert(usp_ui_i18n::compact_weather_icon_rgb(200, 20) == 0x94A3B8);
  assert(!usp_ui_i18n::compact_weather_has_rain(52));
  TestWeather client;
  esphome::test_millis = UINT32_MAX - 10;
  assert(client.apply(serialized(valid_payload())));
  esphome::test_millis = 10;
  assert(!client.weather_stale());
  esphome::test_millis += 2700001;
  assert(client.weather_stale());
  esphome::test_millis = UINT32_MAX - 10;
  assert(client.weather_stale());  // Once stale, a clock wrap cannot revive it.
}

void add_week(JsonDocument &document) {
  auto daily = document["daily"];
  daily["time"].to<JsonArray>().clear();
  daily["weather_code"].to<JsonArray>().clear();
  daily["temperature_2m_max"].to<JsonArray>().clear();
  daily["temperature_2m_min"].to<JsonArray>().clear();
  daily["precipitation_probability_max"].to<JsonArray>().clear();
  auto sums = daily["precipitation_sum"].to<JsonArray>();
  auto rises = daily["sunrise"].to<JsonArray>();
  auto sets = daily["sunset"].to<JsonArray>();
  auto duv = daily["uv_index_max"].to<JsonArray>();
  for (int i = 0; i < 7; ++i) {
    char date[11], stamp[17];
    std::snprintf(date, sizeof(date), "2026-10-%02d", 20 + i);
    std::snprintf(stamp, sizeof(stamp), "2026-10-%02dT07:30", 20 + i);
    daily["time"].as<JsonArray>().add(date);
    daily["weather_code"].as<JsonArray>().add(i == 2 ? 61 : 2);
    daily["temperature_2m_max"].as<JsonArray>().add(10 + i);
    daily["temperature_2m_min"].as<JsonArray>().add(2 + i);
    daily["precipitation_probability_max"].as<JsonArray>().add(i * 10);
    sums.add(i * 0.5); rises.add(stamp);
    std::snprintf(stamp, sizeof(stamp), "2026-10-%02dT17:30", 20 + i);
    sets.add(stamp); duv.add(2.0 + i);
  }
}

void add_hours(JsonDocument &document) {
  auto hourly = document["hourly"].to<JsonObject>();
  auto times = hourly["time"].to<JsonArray>();
  auto temps = hourly["temperature_2m"].to<JsonArray>();
  auto apparent = hourly["apparent_temperature"].to<JsonArray>();
  auto prob = hourly["precipitation_probability"].to<JsonArray>();
  auto precip = hourly["precipitation"].to<JsonArray>();
  auto codes = hourly["weather_code"].to<JsonArray>();
  auto day = hourly["is_day"].to<JsonArray>();
  auto wind = hourly["wind_speed_10m"].to<JsonArray>();
  auto direction = hourly["wind_direction_10m"].to<JsonArray>();
  auto gust = hourly["wind_gusts_10m"].to<JsonArray>();
  auto pressure = hourly["surface_pressure"].to<JsonArray>();
  auto uv = hourly["uv_index"].to<JsonArray>();
  for (int i = 0; i < 30; ++i) {
    char stamp[17]; std::snprintf(stamp, sizeof(stamp), "2026-10-%02dT%02d:00", 20 + (13 + i) / 24, (13 + i) % 24);
    times.add(stamp); temps.add(7.0 + i); apparent.add(6.0 + i); prob.add(i % 101);
    precip.add(0.1 * i); codes.add(2); day.add(((13 + i) % 24) >= 6 && ((13 + i) % 24) < 18 ? 1 : 0);
    wind.add(10 + i); direction.add((i * 15) % 360);
    gust.add(20 + i); pressure.add(1000 + i); uv.add(1.0 + i * 0.1);
  }
}

JsonDocument extended_payload() {
  auto document = valid_payload();
  add_week(document);
  add_hours(document);
  return document;
}

void hourly_daily_model() {
  TestWeather client;
  auto document = extended_payload();
  TestWeather::WeatherSnapshot value;
  assert(client.parse_forecast_text(serialized(document), &value));
  assert(value.forecast_count == 7 && value.hourly_count == 24);
  assert(std::string(value.days[6].date) == "2026-10-26");
  assert(std::string(value.hours[0].time) == "2026-10-20T14:00");
  assert(std::string(value.hours[23].time) == "2026-10-21T13:00");
  assert(value.days[2].precip_sum_mm == 1.0F && value.hours[0].pressure_hpa == 1001.0F);
  assert(value.hours[0].is_day && !value.hours[10].is_day);
  const auto url = client.build_forecast_url_(client.forecast_request_copy_().city);
  for (const char *token : {"forecast_days=7", "forecast_hours=25", "hourly=temperature_2m", "weather_code,is_day", "wind_direction_10m", "surface_pressure", "uv_index", "sunrise", "sunset", "precipitation_sum"}) assert(url.find(token) != std::string::npos);
  assert(url.find("timezone=auto") != std::string::npos);
  // Missing hourly is a valid partial forecast; no rows are invented.
  document.remove("hourly");
  assert(client.parse_forecast_text(serialized(document), &value) && value.hourly_count == 0 && value.forecast_count == 7);
}

void extended_missing_values() {
  TestWeather client;
  auto document = extended_payload();
  document["daily"]["precipitation_sum"][0] = nullptr;
  document["daily"]["uv_index_max"][1] = -1;
  document["daily"]["sunrise"][2] = "2026-10-22T99:00";
  document["hourly"]["surface_pressure"][1] = nullptr;
  document["hourly"]["wind_direction_10m"][2] = 361;
  document["hourly"]["precipitation"][3] = -1;
  document["hourly"]["time"][4] = "2026-10-20T13:00";
  document["hourly"]["time"][5] = "2026-10-20T18:00:30";
  TestWeather::WeatherSnapshot value;
  assert(client.parse_forecast_text(serialized(document), &value));
  assert(std::isnan(value.days[0].precip_sum_mm));
  assert(std::isnan(value.days[1].uv_max));
  assert(value.days[2].sunrise[0] == '\0');
  assert(value.hourly_count == 22);
  assert(std::string(value.hours[0].time) == "2026-10-20T14:00");
  assert(std::isnan(value.hours[0].pressure_hpa));
  assert(std::isnan(value.hours[1].wind_direction_deg));
  assert(std::isnan(value.hours[2].precip_mm));
  // Duplicate/backward times are skipped even if all core measurements are valid.
  document["hourly"]["time"][7] = document["hourly"]["time"][6];
  assert(client.parse_forecast_text(serialized(document), &value));
  assert(value.hourly_count == 21);
  document["hourly"]["uv_index"].to<JsonArray>().clear();
  assert(client.parse_forecast_text(serialized(document), &value));
  assert(value.hourly_count == 21 && value.forecast_count == 7);
  for (size_t index = 0; index < value.hourly_count; ++index)
    assert(std::isnan(value.hours[index].uv_index));
  document = extended_payload();
  document["hourly"]["is_day"][1] = 2;
  assert(client.parse_forecast_text(serialized(document), &value));
  assert(value.hourly_count == 23);
}

void provider_payload(const char *path) {
  std::ifstream file(path);
  assert(file.good());
  const std::string body((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  TestWeather client;
  TestWeather::WeatherSnapshot value;
  assert(client.parse_forecast_text(body, &value));
  assert(value.forecast_count == 7 && value.hourly_count == 24);
  std::cout << "PROVIDER_NATIVE=PASS days=7 hours=24 bytes="
            << body.size() << "\n";
}

int main(int argc, char **argv) {
  if (argc == 2) {
    provider_payload(argv[1]);
    return 0;
  }
  required_current_values();
  hourly_daily_model();
  extended_missing_values();
  forecast_rows_are_not_fabricated();
  city_ownership();
  search_ownership();
  stored_city_and_unicode();
  condition_codes_and_clock_wrap();
  std::cout << "WEATHER_INTEGRITY_NATIVE=PASS parser ownership search saved_city WMO256 clock_wrap\n";
}

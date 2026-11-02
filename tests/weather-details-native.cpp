#include "weather_client.h"
#include "usp_forecast_details_view.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string_view>
int main() {
  using W = esphome::usp_weather::WeatherClient;
  W::ForecastHour hour;
  hour.apparent_temperature_c = 0.0F; hour.precip_mm = 0.0F;
  hour.wind_kmh = 12.4F; hour.wind_direction_deg = 270.0F; hour.gust_kmh = 22.0F;
  hour.pressure_hpa = 1008.2F; hour.uv_index = 0.0F;
  auto hv = usp_forecast_details::hour(hour);
  assert(std::string_view(hv.feels.data()) == "0.0°C");
  assert(std::string_view(hv.precip.data()) == "0.0 mm");
  assert(std::string_view(hv.wind.data()).find("12 km/h") != std::string_view::npos);
  assert(std::string_view(hv.pressure.data()) == "1008.2 hPa");
  assert(std::string_view(hv.uv.data()) == "0.0");
  hour.uv_index = NAN; hour.pressure_hpa = NAN; hour.wind_kmh = NAN;
  hv = usp_forecast_details::hour(hour);
  assert(std::string_view(hv.uv.data()) == "--");
  assert(std::string_view(hv.pressure.data()) == "--");
  assert(std::string_view(hv.wind.data()) == "--");
  W::ForecastDay day; day.precip_sum_mm = 0.0F; day.uv_max = NAN;
  std::strcpy(day.sunrise, "07:31"); day.sunset[0] = 0;
  auto dv = usp_forecast_details::day(day);
  assert(std::string_view(dv.precip_sum.data()) == "0.0 mm");
  assert(std::string_view(dv.uv_max.data()) == "--");
  assert(std::string_view(dv.sunrise.data()) == "07:31");
  assert(std::string_view(dv.sunset.data()) == "--");
  std::cout << "WEATHER_DETAILS_NATIVE=PASS bytes=" << sizeof(usp_forecast_details::View)
            << " zero_distinct_missing=1 units=explicit\n";
}

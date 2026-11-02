#pragma once

#include <cstdint>
#include <string>

#include "usp_ui_format.h"
#include "usp_weather_codes.h"

namespace usp_ui_i18n {

/** Return the localized fallback used for an unsupported threat state. */
inline std::string unknown_threat(uint8_t language) {
  if (language == 0) return "Невідомо";
  if (language == 1) return "Неизвестно";
  return "Unknown";
}

/** Format a positive minute delta with localized text and unit suffixes. */
inline std::string relative_change(int minutes, uint8_t language) {
  const unsigned total = minutes > 0 ? static_cast<unsigned>(minutes) : 0U;
  const std::string hours = usp_ui::unsigned_decimal(total / 60U);
  const std::string mins = usp_ui::unsigned_decimal(total % 60U);

  if (language == 0) {
    return "Зміна через " + hours + " год " + mins + " хв";
  }
  if (language == 1) {
    return "Смена через " + hours + " ч " + mins + " мин";
  }
  return "Next in " + hours + "h " + mins + "m";
}

/** Format a minute count using language-specific hour/minute suffixes. */
inline std::string duration_minutes(
    int minutes, uint8_t language, bool pad_minutes = false) {
  const unsigned total = minutes > 0 ? static_cast<unsigned>(minutes) : 0U;
  const std::string hours = usp_ui::unsigned_decimal(total / 60U);
  std::string mins = usp_ui::unsigned_decimal(total % 60U);
  if (pad_minutes && mins.size() < 2U) mins.insert(mins.begin(), '0');

  if (language == 0) return hours + " год " + mins + " хв";
  if (language == 1) return hours + " ч " + mins + " мин";
  return hours + "h" + mins + "m";
}

/** Format the Power page Today/Tomorrow outage duration summary. */
inline std::string outage_day_summary(
    int today_minutes, int tomorrow_minutes, uint8_t language) {
  const std::string today = today_minutes < 0
      ? "--" : duration_minutes(today_minutes, language);
  const std::string tomorrow = tomorrow_minutes < 0
      ? "--" : duration_minutes(tomorrow_minutes, language);
  if (language == 0) return "Сьогодні " + today + " · Завтра " + tomorrow;
  if (language == 1) return "Сегодня " + today + " · Завтра " + tomorrow;
  return "Today " + today + " · Tomorrow " + tomorrow;
}

/** Return a short localized energy state for the 70 px header badge. */
inline std::string compact_energy_status(
    uint8_t status, bool valid, bool known, uint8_t language) {
  if (!valid) return "--";
  if (!known) return "#" + usp_ui::unsigned_decimal(status);

  if (status == 3) return language == 2 ? "OK" : "НОРМ";
  if (status == 4) return language == 2 ? "LOW" : "ДЕФ";
  if (status == 9) {
    if (language == 0) return "ВИМК";
    if (language == 1) return "ВЫКЛ";
    return "OFF";
  }
  if (status == 10) return language == 2 ? "CRIT" : "КРИТ";
  return "#" + usp_ui::unsigned_decimal(status);
}


/** Return a short localized connection state for the 58 px Wi-Fi badge. */
inline std::string compact_wifi_status(bool connected, uint8_t language) {
  if (language == 0) return connected ? "Є" : "НІ";
  if (language == 1) return connected ? "ЕСТЬ" : "НЕТ";
  return connected ? "ON" : "OFF";
}


/** Format Celsius values consistently for the display UI. */
inline std::string temperature_c(int value) {
  return usp_ui::signed_decimal(value) + "°C";
}

/** Return the Celsius placeholder used when a temperature is unavailable. */
inline std::string temperature_c_placeholder() { return "--°C"; }

/** Return true when a WMO code represents rain/drizzle/showers. */
inline bool compact_weather_has_rain(uint8_t code) {
  return usp_weather_codes::has_rain(code);
}

/** Return a compact top-bar icon for an Open-Meteo/WMO code. */
inline const char *compact_weather_icon(uint8_t code) {
  if (code == 0) return "☀";
  if (code <= 3 || code == 45 || code == 48) return "☁";
  if (compact_weather_has_rain(code)) return "☁";
  if (usp_weather_codes::has_snow(code)) {
    return "❄";
  }
  if (usp_weather_codes::group(code) == usp_weather_codes::THUNDERSTORM) return "⚡";
  return "?";
}

/** Return the RGB color used for the compact weather icon. */
inline uint32_t compact_weather_icon_rgb(uint8_t code, int temperature) {
  if (code == 0) return temperature >= 30 ? 0xF97316U : 0xFACC15U;
  if (code == 1 || code == 2) return 0x60A5FAU;
  if (code == 3 || code == 45 || code == 48 || compact_weather_has_rain(code)) {
    return 0x94A3B8U;
  }
  if (usp_weather_codes::has_snow(code)) {
    return 0xF8FAFCU;
  }
  if (usp_weather_codes::group(code) == usp_weather_codes::THUNDERSTORM) return 0xFBBF24U;
  return 0x94A3B8U;
}

/** Return the RGB color used for rain drops under the cloud icon. */
inline uint32_t compact_weather_rain_rgb() { return 0x38BDF8U; }

/** Format the top-bar weather badge for diagnostics/backward compatibility. */
inline std::string compact_weather_badge(
    bool valid, uint8_t code, int temperature) {
  if (!valid) return temperature_c_placeholder();
  return std::string(compact_weather_icon(code)) + " "
      + temperature_c(temperature);
}

/** Format a high/low Celsius pair for forecast cards. */
inline std::string temperature_c_range(int high, int low) {
  return usp_ui::signed_decimal(high) + " / "
      + usp_ui::signed_decimal(low) + "°C";
}

/** Return the high/low placeholder used by forecast cards. */
inline std::string temperature_c_range_placeholder() { return "-- / --°C"; }


/** Format the compact NEPTUN threat layer status for fullscreen map. */
inline std::string neptun_summary(
    uint16_t uav, uint16_t fpv, uint16_t recon, uint16_t missile,
    uint16_t ballistic,
    uint16_t kab, uint16_t mig31k, uint16_t unknown, uint8_t language,
    bool stale, bool fetch_ok) {
  const unsigned drones = static_cast<unsigned>(uav + fpv + recon);
  const unsigned missiles = static_cast<unsigned>(missile + ballistic);
  const unsigned other = static_cast<unsigned>(mig31k + unknown);
  const std::string suffix = stale || !fetch_ok
      ? (language == 0 ? " · кеш" : language == 1 ? " · кэш" : " · cached")
      : "";
  if (language == 0) {
    return "БпЛА " + usp_ui::unsigned_decimal(drones)
        + " · Ракети " + usp_ui::unsigned_decimal(missiles)
        + " · КАБ " + usp_ui::unsigned_decimal(kab)
        + " · Інше " + usp_ui::unsigned_decimal(other) + suffix;
  }
  if (language == 1) {
    return "БпЛА " + usp_ui::unsigned_decimal(drones)
        + " · Ракеты " + usp_ui::unsigned_decimal(missiles)
        + " · КАБ " + usp_ui::unsigned_decimal(kab)
        + " · Прочее " + usp_ui::unsigned_decimal(other) + suffix;
  }
  return "UAV " + usp_ui::unsigned_decimal(drones)
      + " · Missiles " + usp_ui::unsigned_decimal(missiles)
      + " · KAB " + usp_ui::unsigned_decimal(kab)
      + " · Other " + usp_ui::unsigned_decimal(other) + suffix;
}

/** Localized NEPTUN initial state before the first successful snapshot. */
inline std::string neptun_waiting(uint8_t language) {
  if (language == 0) return "NEPTUN · очікування даних";
  if (language == 1) return "NEPTUN · ожидание данных";
  return "NEPTUN · waiting for data";
}

/** Visible provider attribution required by the NEPTUN API terms. */
inline std::string neptun_source(uint8_t language) {
  if (language == 0) return "Дані: NEPTUN · neptun.in.ua";
  if (language == 1) return "Данные: NEPTUN · neptun.in.ua";
  return "Source: NEPTUN · neptun.in.ua";
}

/** Localized live status for the Yasno outage provider. */
inline std::string yasno_live_status(uint8_t language) {
  if (language == 0) return "Yasno · актуально";
  if (language == 1) return "Yasno · актуально";
  return "Yasno · live";
}

/** Localized weather-data attribution while preserving the license name. */
inline std::string weather_data_attribution(
    uint8_t language, const std::string &separator) {
  if (language == 0) return "Погодні дані" + separator + "CC BY 4.0";
  if (language == 1) return "Данные о погоде" + separator + "CC BY 4.0";
  return "Weather data" + separator + "CC BY 4.0";
}

/** Localized Open-Meteo attribution while preserving the provider brand. */
inline std::string weather_open_meteo_attribution(uint8_t language) {
  if (language == 0) return "Open-Meteo · погодні дані · CC BY 4.0";
  if (language == 1) return "Open-Meteo · данные о погоде · CC BY 4.0";
  return "Open-Meteo · weather data · CC BY 4.0";
}

/** Format the compact outage schedule group badge. */
inline std::string outage_group_label(
    const std::string &group, bool configured, uint8_t language) {
  const std::string prefix = language == 2 ? "G" : "Г";
  return configured ? prefix + group : prefix + " --";
}

/** Return a compact outage state for the 160 px Home schedule label. */
inline std::string compact_outage_status(
    bool configured, bool valid, bool stale, bool fetch_ok,
    uint8_t status, uint8_t language) {
  if (!configured) {
    if (language == 0) return "Не налаштовано";
    if (language == 1) return "Не настроено";
    return "Not configured";
  }
  if (!valid) {
    if (language == 0) return "Очікування";
    if (language == 1) return "Ожидание";
    return "Waiting";
  }
  if (stale) {
    if (language == 0) return "Застаріло";
    if (language == 1) return "Устарело";
    return "Stale";
  }
  if (!fetch_ok) {
    if (language == 0) return "Кешовані";
    if (language == 1) return "Кэшированные";
    return "Cached";
  }

  static constexpr const char *TEXT[5][3] = {
      {"Невідомо", "Неизвестно", "Unknown"},
      {"Очікування", "Ожидание", "Waiting"},
      {"Графік активний", "График активен", "Schedule active"},
      {"Екстрено", "Экстренно", "Emergency"},
      {"Без відключень", "Без отключений", "No outages"},
  };
  const uint8_t lang = language <= 2 ? language : 2;
  const uint8_t index = status <= 4 ? status : 0;
  return TEXT[index][lang];
}

/** Return the compact Home summary when no more changes are expected today. */
inline std::string compact_outage_no_changes(uint8_t language) {
  if (language == 0) return "Без змін";
  if (language == 1) return "Без изменений";
  return "No changes";
}

/** Format the next Home outage change within the 198 px summary label. */
inline std::string compact_relative_change(int minutes, uint8_t language) {
  const unsigned total = minutes > 0 ? static_cast<unsigned>(minutes) : 0U;
  const std::string hours = usp_ui::unsigned_decimal(total / 60U);
  const std::string mins = usp_ui::unsigned_decimal(total % 60U);
  if (language == 0) return "Зміна: " + hours + " год " + mins + " хв";
  if (language == 1) return "Смена: " + hours + " ч " + mins + " мин";
  return "Change: " + hours + "h " + mins + "m";
}

/** Return a compact Weather page state that fits before the refresh button. */
inline std::string compact_weather_status(
    bool valid, bool stale, bool fetch_ok, uint8_t language) {
  if (!valid) {
    if (language == 0) return "Очікування даних";
    if (language == 1) return "Ожидание данных";
    return "Waiting for data";
  }
  if (stale) {
    if (language == 0) return "Дані застаріли";
    if (language == 1) return "Данные устарели";
    return "Stale data";
  }
  if (!fetch_ok) {
    if (language == 0) return "Останні збережені дані";
    if (language == 1) return "Кэшированные данные";
    return "Last cached data";
  }
  if (language == 0 || language == 1) return "Актуально";
  return "Live";
}

/** Shorten only weather phrases that cannot fit the compact Weather cards. */
inline std::string compact_weather_condition(
    const std::string &value, uint8_t language) {
  const char *compact = usp_weather_codes::compact_description(value, language);
  return compact != nullptr ? std::string(compact) : value;
}

/** Format wind speed with a localized kilometre-per-hour unit. */
inline std::string wind_speed_kmh(
    int value, bool valid, uint8_t language) {
  std::string unit;
  if (language == 0) unit = "км/год";
  else if (language == 1) unit = "км/ч";
  else unit = "km/h";

  if (!valid) return "-- " + unit;
  const uint32_t speed = value > 0 ? static_cast<uint32_t>(value) : 0U;
  return usp_ui::unsigned_decimal(speed) + " " + unit;
}

}  // namespace usp_ui_i18n

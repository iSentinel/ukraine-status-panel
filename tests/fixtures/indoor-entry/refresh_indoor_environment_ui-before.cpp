const bool detected = id(environment_sht4x).detected();
const bool valid = detected
    && id(environment_sht4x).last_read_ok()
    && std::isfinite(id(environment_temperature).state)
    && std::isfinite(id(environment_humidity).state);

std::string prefix;
if (id(ui_language) == 0) {
  prefix = "В приміщенні: ";
} else if (id(ui_language) == 1) {
  prefix = "В помещении: ";
} else {
  prefix = "Indoor: ";
}

std::string text;
uint32_t color = 0x94A3B8;
if (!detected) {
  if (id(ui_language) == 0) {
    text = prefix + "датчик не підключено";
  } else if (id(ui_language) == 1) {
    text = prefix + "датчик не подключен";
  } else {
    text = prefix + "sensor not connected";
  }
} else if (!valid) {
  color = 0xFACC15;
  if (id(ui_language) == 0) {
    text = prefix + "очікування даних";
  } else if (id(ui_language) == 1) {
    text = prefix + "ожидание данных";
  } else {
    text = prefix + "waiting for data";
  }
} else {
  color = 0x4ADE80;
  char buffer[96];
  std::snprintf(
      buffer, sizeof(buffer), "%s%.1f°C · %d%%",
      prefix.c_str(), id(environment_temperature).state,
      static_cast<int>(std::lround(
          id(environment_humidity).state)));
  text = buffer;
}

lv_label_set_text(id(weather_indoor_label), text.c_str());
lv_obj_set_style_text_color(
    id(weather_indoor_label), lv_color_hex(color), LV_PART_MAIN);
lv_label_set_text(
    id(fullscreen_weather_indoor_label), text.c_str());
lv_obj_set_style_text_color(
    id(fullscreen_weather_indoor_label),
    lv_color_hex(color), LV_PART_MAIN);
const uint8_t lang = id(ui_language) < 3 ? id(ui_language) : 0;
const char *labels[] = {
    "\u041a\u0406\u041c\u041d\u0410\u0422\u0410 \u00b7 24\u0413",
    "\u041a\u041e\u041c\u041d\u0410\u0422\u0410 \u00b7 24\u0427",
    "ROOM \u00b7 24H"};
usp_lcd_navigation::button_text(
    id(weather_room_button), labels[lang]);
usp_lcd_navigation::button_text(
    id(fullscreen_weather_room_button), labels[lang]);
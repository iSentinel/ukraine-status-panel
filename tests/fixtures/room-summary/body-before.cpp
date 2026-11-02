const uint8_t lang = id(ui_language) < 3 ? id(ui_language) : 0;
char summary[96];
if (lang == 0) {
  std::snprintf(summary, sizeof(summary), "%u \u0432\u0438\u043c\u0456\u0440\u0456\u0432 \u00b7 %u \u043f\u0440\u043e\u043f\u0443\u0441\u043a\u0456\u0432",
      id(room_history).valid_count(), id(room_history).gap_count());
} else if (lang == 1) {
  std::snprintf(summary, sizeof(summary), "%u \u0438\u0437\u043c\u0435\u0440\u0435\u043d\u0438\u0439 \u00b7 %u \u043f\u0440\u043e\u043f\u0443\u0441\u043a\u043e\u0432",
      id(room_history).valid_count(), id(room_history).gap_count());
} else {
  std::snprintf(summary, sizeof(summary), "%u samples \u00b7 %u gaps",
      id(room_history).valid_count(), id(room_history).gap_count());
}

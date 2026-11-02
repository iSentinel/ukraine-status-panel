#pragma once
template<typename... Args> inline void alarm_test_log(Args &&...) {}
#define ESP_LOGI(...) alarm_test_log(__VA_ARGS__)
#define ESP_LOGE(...) alarm_test_log(__VA_ARGS__)
#define ESP_LOGW(...) alarm_test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) alarm_test_log(__VA_ARGS__)
#define YESNO(value) ((value) ? "YES" : "NO")

#pragma once
namespace esphome {
template<class... Args> inline void history_test_log(Args&&...) {}
}
#define ESP_LOGD(...) esphome::history_test_log(__VA_ARGS__)
#define ESP_LOGI(...) esphome::history_test_log(__VA_ARGS__)
#define ESP_LOGW(...) esphome::history_test_log(__VA_ARGS__)
#define ESP_LOGE(...) esphome::history_test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) esphome::history_test_log(__VA_ARGS__)
#define YESNO(value) ((value) ? "YES" : "NO")

#pragma once
template<typename... Args> inline void room_test_log(Args &&...) {}
#define ESP_LOGI(...) room_test_log(__VA_ARGS__)
#define ESP_LOGW(...) room_test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) room_test_log(__VA_ARGS__)

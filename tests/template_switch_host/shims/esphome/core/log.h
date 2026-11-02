#pragma once
namespace esphome {
using LogString = char;
template<typename... T> inline void ignore_log(const T&...) {}
}
#define LOG_STR(x) (x)
#define LOG_STR_LITERAL(x) (x)
#define LOG_STR_ARG(x) (x)
#define ONOFF(x) ((x) ? "ON" : "OFF")
#define YESNO(x) ((x) ? "YES" : "NO")
#define ESP_LOGD(...) esphome::ignore_log(__VA_ARGS__)
#define ESP_LOGV(...) esphome::ignore_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) esphome::ignore_log(__VA_ARGS__)
#define LOG_ENTITY_ICON(...) esphome::ignore_log(__VA_ARGS__)
#define LOG_ENTITY_DEVICE_CLASS(...) esphome::ignore_log(__VA_ARGS__)

#pragma once
#include <cstring>
namespace boundary {
extern unsigned present_logs, ready_logs, warning_logs;
template<typename... Args> void log(const char *,const char *format,Args...) {
  if(std::strstr(format,"FULLSCREEN_CACHE_PRESENT")) ++present_logs;
  if(std::strstr(format,"FULLSCREEN_CACHE_COLD_READY")) ++ready_logs;
}
template<typename... Args> void warning(const char *,const char *,Args...) {++warning_logs;}
}
#define ESP_LOGI(...) boundary::log(__VA_ARGS__)
#define ESP_LOGD(...) boundary::log(__VA_ARGS__)
#define ESP_LOGW(...) boundary::warning(__VA_ARGS__)

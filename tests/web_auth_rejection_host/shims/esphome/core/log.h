#pragma once
template<class... Args> inline void native_log(Args&&...) {}
#define ESP_LOGV(...) native_log(__VA_ARGS__)
#define ESP_LOGW(...) native_log(__VA_ARGS__)
#define ESP_LOGE(...) native_log(__VA_ARGS__)

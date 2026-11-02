#pragma once
template<typename... Args> inline void host_log(Args&&...) {}
#define ESP_LOGCONFIG(...) host_log(__VA_ARGS__)
#define YESNO(value) ((value)?"YES":"NO")

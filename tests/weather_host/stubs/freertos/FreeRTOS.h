#pragma once
#include <cstdint>
using BaseType_t = int;
using TaskHandle_t = void *;
constexpr int pdPASS = 1;
constexpr int MALLOC_CAP_SPIRAM = 1;
constexpr int MALLOC_CAP_8BIT = 2;
#define pdMS_TO_TICKS(value) (value)

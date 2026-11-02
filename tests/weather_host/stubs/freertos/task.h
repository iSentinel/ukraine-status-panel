#pragma once
#include "FreeRTOS.h"
inline int xTaskCreatePinnedToCoreWithCaps(void (*)(void *), const char *, int,
                                        void *, int, void **, int, int) { return 1; }
inline void vTaskDelay(uint32_t) {}
inline void vTaskDelete(void *) {}

// Controlled host boundary; target availability is checked separately.
using StackType_t = uint8_t;
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }

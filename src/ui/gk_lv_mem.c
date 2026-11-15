// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#include "gk_lv_mem.h"

#if defined(MK_LVGL_PSRAM) && defined(ESP_PLATFORM)
#include <esp_heap_caps.h>

#define GK_LV_PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define GK_LV_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void* gk_lv_malloc(size_t size) {
    void* p = heap_caps_malloc(size, GK_LV_PSRAM);
    return p ? p : heap_caps_malloc(size, GK_LV_INTERNAL);
}

void* gk_lv_realloc(void* ptr, size_t size) {
    void* p = heap_caps_realloc(ptr, size, GK_LV_PSRAM);
    return p ? p : heap_caps_realloc(ptr, size, GK_LV_INTERNAL);
}

void gk_lv_free(void* ptr) {
    heap_caps_free(ptr);
}
#endif

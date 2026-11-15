// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#include "gk_lv_mem.h"

#if defined(ESP_PLATFORM) && !defined(MK_LVGL_INTERNAL)
#include <esp_heap_caps.h>

#define GK_LV_PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define GK_LV_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void* gk_lv_malloc(size_t size) {
    void* p = heap_caps_malloc(size, GK_LV_PSRAM);
    return p ? p : heap_caps_malloc(size, GK_LV_INTERNAL);
}

// heap_caps_realloc() accepts caps different from the block's original ones and
// moves the block across heaps when needed (esp_heap_caps.h), so a block that
// fell back to internal RAM migrates to PSRAM on growth and vice versa. On
// failure the original block is left intact, so trying internal is safe.
// LVGL never passes size 0 (lv_mem_realloc intercepts it).
void* gk_lv_realloc(void* ptr, size_t size) {
    void* p = heap_caps_realloc(ptr, size, GK_LV_PSRAM);
    return p ? p : heap_caps_realloc(ptr, size, GK_LV_INTERNAL);
}

void gk_lv_free(void* ptr) {
    heap_caps_free(ptr);
}

const char* gk_lv_allocator_name(void) {
    return "psram";
}
#else
const char* gk_lv_allocator_name(void) {
    return "internal";
}
#endif

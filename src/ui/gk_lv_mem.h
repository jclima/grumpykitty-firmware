// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// LVGL allocator used on the device unless -DMK_LVGL_INTERNAL is set (see lib/lv_conf.h):
// PSRAM first, internal RAM as the fallback. Only called from the main/LVGL task
// (never from an ISR). Memory is not DMA-capable.
void* gk_lv_malloc(size_t size);
void* gk_lv_realloc(void* ptr, size_t size);
void gk_lv_free(void* ptr);

// "psram" or "internal", for the startup log.
const char* gk_lv_allocator_name(void);

#ifdef __cplusplus
}
#endif

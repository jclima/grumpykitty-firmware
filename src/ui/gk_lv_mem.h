// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// LVGL allocator used only with -DMK_LVGL_PSRAM on the device (see lib/lv_conf.h):
// PSRAM first, internal RAM as the fallback.
void* gk_lv_malloc(size_t size);
void* gk_lv_realloc(void* ptr, size_t size);
void gk_lv_free(void* ptr);

#ifdef __cplusplus
}
#endif

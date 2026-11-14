// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// Save the current LVGL frame to the SD card as a 16-bit BMP.

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SHOT_OK, SHOT_UNSUPPORTED, SHOT_MSC, SHOT_NO_SD, SHOT_FULL, SHOT_IO
} shot_result_t;

/* Write /screenshots/shot_NNNN.bmp from the last full LVGL frame. On SHOT_OK
 * name_out receives just the file name ("shot_0007.bmp"). Main loop only, never
 * from inside lv_timer_handler (the frame buffer is reused two refreshes later);
 * the write is synchronous and takes a few hundred ms. */
shot_result_t screenshot_save(char *name_out, size_t n);

#ifdef __cplusplus
}
#endif

#endif

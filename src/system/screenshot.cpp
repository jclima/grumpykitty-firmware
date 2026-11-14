// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// Screenshot to SD: 320x240 RGB565 BMP (BI_BITFIELDS), bottom-up rows.

#include "screenshot.h"
#include "usb_msc.h"
#include "../bsp/porting/lv_port_disp.h"
#include "../ui/ui_sd_bridge.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#define SHOT_DIR      "/screenshots"
#define SHOT_W        320
#define SHOT_H        240
#define SHOT_ROW_B    (SHOT_W * 2)              /* 640, already 4-byte aligned */
#define SHOT_HDR_B    66                        /* 14 + 40 + 3 masks */
#define SHOT_IMG_B    (SHOT_ROW_B * SHOT_H)     /* 153600 */
#define SHOT_BOUNCE_ROWS 8
#define SHOT_MAX      9999

static int s_next = -1;     /* next candidate number, -1 = directory not scanned yet */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void build_header(uint8_t *h)
{
    memset(h, 0, SHOT_HDR_B);
    h[0] = 'B'; h[1] = 'M';
    put32(h + 2,  SHOT_HDR_B + SHOT_IMG_B);     /* bfSize */
    put32(h + 10, SHOT_HDR_B);                  /* bfOffBits */
    put32(h + 14, 40);                          /* biSize */
    put32(h + 18, SHOT_W);
    put32(h + 22, SHOT_H);                      /* positive: bottom-up */
    put16(h + 26, 1);                           /* planes */
    put16(h + 28, 16);                          /* bit count */
    put32(h + 30, 3);                           /* BI_BITFIELDS */
    put32(h + 34, SHOT_IMG_B);
    put32(h + 38, 2835);                        /* ~72 dpi */
    put32(h + 42, 2835);
    put32(h + 54, 0xF800);                      /* masks follow the 40-byte info header */
    put32(h + 58, 0x07E0);
    put32(h + 62, 0x001F);
}

/* One pass over the directory: highest shot_NNNN.bmp number + 1. */
static int scan_next(void)
{
    int next = 1;
    File dir = SD_MMC.open(SHOT_DIR);
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char *nm = f.name();
            const char *sl = strrchr(nm, '/');
            if (sl) nm = sl + 1;
            int num = 0;
            char tail[8] = {0};
            if (sscanf(nm, "shot_%4d.%7s", &num, tail) == 2 && strcasecmp(tail, "bmp") == 0 && num >= next)
                next = num + 1;
            f.close();
        }
    }
    if (dir) dir.close();
    return next;
}

shot_result_t screenshot_save(char *name_out, size_t n)
{
    const lv_color_t *frame = lv_port_disp_last_frame();
    if (!frame) return SHOT_UNSUPPORTED;
    if (usb_msc_is_active() || usb_msc_is_draining()) return SHOT_MSC;   /* before the SD check */
    if (!ui_sd_present()) return SHOT_NO_SD;

    if (!SD_MMC.exists(SHOT_DIR)) SD_MMC.mkdir(SHOT_DIR);
    if (s_next < 0) s_next = scan_next();

    char path[40];
    for (;;) {
        if (s_next > SHOT_MAX) return SHOT_FULL;
        snprintf(path, sizeof(path), SHOT_DIR "/shot_%04d.bmp", s_next);
        if (!SD_MMC.exists(path)) break;
        s_next++;
    }
    const int num = s_next;

    uint8_t *bounce = (uint8_t *)heap_caps_malloc(SHOT_BOUNCE_ROWS * SHOT_ROW_B, MALLOC_CAP_INTERNAL);
    if (!bounce) return SHOT_IO;

    const uint32_t t0 = millis();
    shot_result_t res = SHOT_IO;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) {
        uint8_t hdr[SHOT_HDR_B];
        build_header(hdr);
        bool ok = f.write(hdr, SHOT_HDR_B) == SHOT_HDR_B;
        /* Bottom-up: start with the last screen row, 8 rows per write. */
        for (int y = SHOT_H; ok && y > 0; y -= SHOT_BOUNCE_ROWS) {
            const int rows = (y >= SHOT_BOUNCE_ROWS) ? SHOT_BOUNCE_ROWS : y;
            for (int r = 0; r < rows; r++) {
                const lv_color_t *src = frame + (size_t)(y - 1 - r) * SHOT_W;
                uint8_t *dst = bounce + r * SHOT_ROW_B;
                for (int x = 0; x < SHOT_W; x++) {      /* little-endian RGB565 */
                    const uint16_t px = src[x].full;
                    dst[2 * x]     = (uint8_t)px;
                    dst[2 * x + 1] = (uint8_t)(px >> 8);
                }
            }
            ok = f.write(bounce, (size_t)rows * SHOT_ROW_B) == (size_t)rows * SHOT_ROW_B;
        }
        f.close();
        if (ok) {
            res = SHOT_OK;
        } else {
            SD_MMC.remove(path);        /* never leave a truncated image behind */
        }
    }
    heap_caps_free(bounce);

    Serial.printf("[Shot] %s %s in %lu ms\n", path, res == SHOT_OK ? "saved" : "FAILED",
                  (unsigned long)(millis() - t0));
    if (res == SHOT_OK) {
        s_next = num + 1;
        if (name_out && n) snprintf(name_out, n, "shot_%04d.bmp", num);
    }
    return res;
}

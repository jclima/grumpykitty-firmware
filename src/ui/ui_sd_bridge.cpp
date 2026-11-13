/**
 * @file ui_sd_bridge.cpp
 * @brief SD_MMC ↔ C UI bridge implementation.
 */
#include "ui_sd_bridge.h"
#include "../system/usb_msc.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <new>
#include "../bsp/config.h"

/* Cached classification of the slot: 0 missing, 1 ok, 2 unsupported.
 * Written from the launcher/UI thread only; plain int reads are atomic. */
static volatile int s_sd_state = UI_SD_MISSING;
static uint32_t     s_last_retry_ms = 0;

/* Retry window for failed (re)mounts: a missing or unsupported card is not
 * re-probed more often than this, so we never hammer the bus. */
static const uint32_t RETRY_MS = 3000;

/* Called after SD_MMC.begin() failed (driver already released the bus).
 * Checks whether a card answers at the SD protocol level: if so the card is
 * physically there and only the FAT mount failed (exFAT, unformatted, ...). */
static int classify_failed_mount(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = 10000;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)HAL_PIN_SD_CLK;
    slot.cmd = (gpio_num_t)HAL_PIN_SD_CMD;
    slot.d0  = (gpio_num_t)HAL_PIN_SD_D0;
    slot.width = 1;

    bool responded = false;
    if (host.init() == ESP_OK) {
        if (sdmmc_host_init_slot(host.slot, &slot) == ESP_OK) {
            static sdmmc_card_t card;
            memset(&card, 0, sizeof(card));
            responded = (sdmmc_card_init(&host, &card) == ESP_OK);
        }
        sdmmc_host_deinit();
    }
    if (responded) {
        if (s_sd_state != UI_SD_UNSUPPORTED)   /* log on transition only */
            Serial.println("[SD] card present but mount failed (unsupported format?)");
        return UI_SD_UNSUPPORTED;
    }
    return UI_SD_MISSING;
}

extern "C" void ui_sd_classify_failed_mount(void)
{
    SD_MMC.end();
    s_sd_state = classify_failed_mount();
    s_last_retry_ms = millis();
}

extern "C" int ui_sd_state(void)
{
    return s_sd_state;
}

extern "C" int ui_sd_present(void)
{
    /* USB mass storage owns the card (also while draining): never probe or
     * remount underneath it, report the state cached before it took over. */
    if (usb_msc_is_active() || usb_msc_is_draining()) return s_sd_state == UI_SD_OK;

    /* Actively probe — SD_MMC.cardType() is a cached value that does not
     * notice hot removals. Opening "/" forces a bus transaction. */
    File root = SD_MMC.open("/");
    if (!root) {
        /* Open failed: card may have been removed then reinserted.
         * Try to remount with the same parameters used at boot. */
        if (s_sd_state != UI_SD_OK && (uint32_t)(millis() - s_last_retry_ms) < RETRY_MS)
            return 0;   /* already known bad: do not remount again yet */
        s_last_retry_ms = millis();
        SD_MMC.end();
        if (!SD_MMC.begin("/sdcard", true, false, 10000)) {
            s_sd_state = classify_failed_mount();
            return 0;
        }
        root = SD_MMC.open("/");
        if (!root) { s_sd_state = UI_SD_MISSING; return 0; }
    }
    bool ok = root.isDirectory();
    root.close();
    s_sd_state = ok ? UI_SD_OK : UI_SD_MISSING;
    return ok ? 1 : 0;
}

extern "C" uint64_t ui_sd_total_bytes(void)
{
    return SD_MMC.totalBytes();
}

extern "C" uint64_t ui_sd_used_bytes(void)
{
    return SD_MMC.usedBytes();
}

extern "C" int ui_sd_list_dir(const char* path, ui_sd_visit_cb cb, void* user)
{
    if (!path || !cb) return -1;

    File dir = SD_MMC.open(path);
    if (!dir) return -1;
    if (!dir.isDirectory()) { dir.close(); return -1; }

    File f = dir.openNextFile();
    while (f) {
        const char* full = f.name();
        const char* slash = strrchr(full, '/');
        const char* base = slash ? slash + 1 : full;
        cb(base, f.isDirectory() ? 1 : 0, user);

        File next = dir.openNextFile();
        f.close();
        f = next;
    }
    dir.close();
    return 0;
}

struct ui_sd_dir_cursor { File dir; File next; bool failed; };

extern "C" ui_sd_dir_cursor_t * ui_sd_dir_open(const char *path)
{
    if (!path) return nullptr;
    ui_sd_dir_cursor_t *c = new (std::nothrow) ui_sd_dir_cursor_t();
    if (!c) return nullptr;
    c->dir = SD_MMC.open(path);
    if (!c->dir || !c->dir.isDirectory()) { c->failed = true; delete c; return nullptr; }
    c->next = c->dir.openNextFile();
    return c;
}

extern "C" int ui_sd_dir_next(ui_sd_dir_cursor_t *c, char *name,
                                uint32_t name_size, int *is_dir)
{
    if (!c || c->failed || !name || name_size == 0 || !is_dir) return -1;
    if (!c->next) return 0;
    const char *full = c->next.name();
    const char *slash = strrchr(full, '/');
    const char *base = slash ? slash + 1 : full;
    size_t len = strlen(base);
    if (len >= name_size) { c->next.close(); return -2; }
    memcpy(name, base, len + 1);
    name[name_size - 1] = '\0';
    *is_dir = c->next.isDirectory() ? 1 : 0;
    c->next.close();
    c->next = c->dir.openNextFile();
    return 1;
}

extern "C" void ui_sd_dir_close(ui_sd_dir_cursor_t *c)
{
    if (!c) return;
    c->next.close();
    c->dir.close();
    delete c;
}

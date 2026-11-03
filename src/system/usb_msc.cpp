/**
 * @file usb_msc.cpp
 * @brief USB MSC U-disk — exposes SD card raw sectors to a host PC.
 *
 * Architecture
 * ────────────
 * static USBMSC s_msc  ← file-scope; constructor calls tinyusb_enable_interface()
 *                          during C++ static-init, BEFORE initArduino()/USB.begin().
 * USB.begin() (ARDUINO_USB_CDC_ON_BOOT=1) starts a CDC+MSC composite device.
 * The MSC LUN has no media initially (mediaPresent=false, block_count=0).
 *
 * On usb_msc_enable():
 *   SD_MMC.end() → sdmmc raw re-init → set callbacks + begin(sectors,512)
 *   → mediaPresent(true)   host sees drive appear (like inserting a card)
 *
 * On usb_msc_disable():
 *   mediaPresent(false) → USBMSC.end() → sdmmc_host_deinit → SD_MMC.begin()
 *
 * Core data-path:
 *   Host USB (GPIO20/19) ↕ TinyUSB USBMSC
 *   ↕ _msc_read_cb / _msc_write_cb
 *   ↕ sdmmc_read/write_sectors()  [ESP-IDF]
 *   ↕ SDMMC hw slot 1, 1-bit  CLK=47 CMD=48 D0=21
 */
#include "usb_msc.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <atomic>

extern "C" {
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"
}

#include "../bsp/config.h"

/* File-scope global: constructor calls tinyusb_enable_interface() at static-init
 * time, before USB.begin() — so MSC is included in the composite descriptor. */
static USBMSC                 s_msc;
static std::atomic<sdmmc_card_t *> s_card{nullptr};
static std::atomic<int>       s_inflight{0};   /* callbacks currently using s_card */
static volatile int           s_running = 0;
static volatile unsigned long s_bytes   = 0;

/* ── Sector callbacks — prefixed to avoid USBMSC.h typedef-name clash ── */

static int32_t _msc_read_cb(uint32_t lba, uint32_t /*offset*/,
                              void * buf, uint32_t bufsize)
{
    s_inflight.fetch_add(1);
    sdmmc_card_t * card = s_card.load();
    int32_t ret = -1;
    if (card && sdmmc_read_sectors(card, buf, lba, bufsize / 512) == ESP_OK) {
        s_bytes += bufsize;
        ret = (int32_t)bufsize;
    }
    s_inflight.fetch_sub(1);
    return ret;
}

static int32_t _msc_write_cb(uint32_t lba, uint32_t /*offset*/,
                               uint8_t * buf, uint32_t bufsize)
{
    s_inflight.fetch_add(1);
    sdmmc_card_t * card = s_card.load();
    int32_t ret = -1;
    if (card && sdmmc_write_sectors(card, buf, lba, bufsize / 512) == ESP_OK) {
        s_bytes += bufsize;
        ret = (int32_t)bufsize;
    }
    s_inflight.fetch_sub(1);
    return ret;
}

static bool _msc_startstop_cb(uint8_t /*pwr*/, bool /*start*/, bool load_eject)
{
    if (load_eject) Serial.println("[MSC] host ejected");
    return true;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int usb_msc_enable(void)
{
    if (s_running) return 1;

    sdmmc_card_t * card = nullptr;
    s_bytes = 0;

    /* 1. Dismount FAT-FS — release exclusive SDMMC bus ownership */
    SD_MMC.end();

    /* 2. Re-init SDMMC in 1-bit raw mode (same params as Launcher::initSD) */
    sdmmc_host_t host  = SDMMC_HOST_DEFAULT();
    host.max_freq_khz  = SDMMC_FREQ_DEFAULT;    /* 20 MHz */
    host.slot          = SDMMC_HOST_SLOT_1;
    host.flags         = SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk   = (gpio_num_t)HAL_PIN_SD_CLK;
    slot.cmd   = (gpio_num_t)HAL_PIN_SD_CMD;
    slot.d0    = (gpio_num_t)HAL_PIN_SD_D0;
    slot.width = 1;
    slot.flags = 0;

    if (sdmmc_host_init() != ESP_OK) {
        Serial.println("[MSC] sdmmc_host_init failed");
        goto fail_remount;
    }
    if (sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) != ESP_OK) {
        Serial.println("[MSC] sdmmc_host_init_slot failed");
        sdmmc_host_deinit();
        goto fail_remount;
    }
    card = (sdmmc_card_t *)heap_caps_malloc(sizeof(sdmmc_card_t),
                                             MALLOC_CAP_DEFAULT);
    if (!card) {
        sdmmc_host_deinit();
        goto fail_remount;
    }
    if (sdmmc_card_init(&host, card) != ESP_OK) {
        Serial.println("[MSC] sdmmc_card_init failed");
        free(card);
        sdmmc_host_deinit();
        goto fail_remount;
    }
    s_card.store(card);

    /* 3. Wire callbacks, set LUN geometry, expose media to host.
     *    USB is already a CDC+MSC composite — host polls test_unit_ready
     *    every few seconds; mediaPresent(true) makes the drive appear. */
    s_msc.vendorID("GrumpyKt");
    s_msc.productID("SDCard");
    s_msc.productRevision("1.0");
    s_msc.onRead(_msc_read_cb);
    s_msc.onWrite(_msc_write_cb);
    s_msc.onStartStop(_msc_startstop_cb);
    s_msc.mediaPresent(true);
    if (!s_msc.begin((uint32_t)card->csd.capacity,
                     (uint16_t)card->csd.sector_size)) {
        Serial.println("[MSC] USBMSC.begin() failed");
        s_card.store(nullptr);
        free(card);
        sdmmc_host_deinit();
        goto fail_remount;
    }

    s_running = 1;
    Serial.printf("[MSC] enabled — %lu sectors × %u B\n",
                  (unsigned long)card->csd.capacity,
                  (unsigned)card->csd.sector_size);
    return 1;

fail_remount:
    SD_MMC.setPins(HAL_PIN_SD_CLK, HAL_PIN_SD_CMD, HAL_PIN_SD_D0);
    SD_MMC.begin("/sdcard", true, false, 10000);
    return 0;
}

void usb_msc_disable(void)
{
    if (!s_running) return;

    /* Signal host: media removed (host unmounts the drive) */
    s_msc.mediaPresent(false);
    s_msc.end();            /* clear callbacks + block info */

    /* Stop new callbacks from touching the card, then wait (bounded) for any
     * in-flight TinyUSB read/write to finish before tearing it down. */
    sdmmc_card_t * card = s_card.exchange(nullptr);
    {
        uint32_t t0 = millis();
        while (s_inflight.load() > 0 && (millis() - t0) < 500)
            delay(1);
    }
    const bool busy = s_inflight.load() > 0;
    if (busy)
        Serial.println("[MSC] warn: callbacks still in flight; leaking card/host (no deinit)");

    /* Release raw SDMMC resources (skipped when a callback may still use them) */
    if (card && !busy) {
        sdmmc_host_deinit();
        free(card);
    }
    s_running = 0;

    /* Remount FAT-FS for the rest of the firmware */
    SD_MMC.setPins(HAL_PIN_SD_CLK, HAL_PIN_SD_CMD, HAL_PIN_SD_D0);
    if (!SD_MMC.begin("/sdcard", true, false, 10000)) {
        Serial.println("[MSC] FS remount failed");
    } else {
        Serial.println("[MSC] FS remounted");
    }
}

int usb_msc_is_active(void)
{
    return s_running;
}

unsigned long usb_msc_bytes_transferred(void)
{
    return s_bytes;
}

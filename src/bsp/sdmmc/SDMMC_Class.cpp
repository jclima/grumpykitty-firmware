/**
 * @file SDMMC_Class.cpp
 * @brief SDMMC implementation with multi-frequency auto-negotiation.
 *
 * Frequency ladder (1-bit, most compatible):
 *   40 MHz — fast cards (Samsung/SanDisk U3, A2)
 *   20 MHz — standard high-speed (class 10)
 *   10 MHz — generic class 6 / cheap clone cards
 *    5 MHz — very slow / edge-case cards, also helps at cold boot
 *
 * The ESP32-S3 SDMMC controller uses internal CLK; no external 50-Ω
 * impedance matching is needed for 1-bit mode below 25 MHz.
 */
#include "SDMMC_Class.hpp"
#include "../config.h"
#include <Arduino.h>

/* Frequency ladder — descending, most compatible last */
static constexpr uint32_t kFreqTable[] = { 40000, 20000, 10000, 5000 };
static constexpr int       kFreqCount  = (int)(sizeof(kFreqTable) / sizeof(kFreqTable[0]));

/* ── Mount / Unmount ──────────────────────────────────────────── */

bool SDMMC_Class::begin(bool mode4bit)
{
    if (_mounted) return true;

    SD_MMC.setPins(HAL_PIN_SD_CLK, HAL_PIN_SD_CMD, HAL_PIN_SD_D0);

    for (int i = 0; i < kFreqCount; i++) {
        if (_tryMount(kFreqTable[i], mode4bit)) {
            Serial.printf("[SDMMC] Mounted at %u kHz (%s, %s)\n",
                          _freq_khz,
                          cardTypeStr(),
                          mode4bit ? "4-bit" : "1-bit");
            return true;
        }
        /* Brief settle before next attempt */
        delay(50);
    }

    Serial.println("[SDMMC] Mount failed at all frequencies");
    return false;
}

void SDMMC_Class::end()
{
    SD_MMC.end();
    _mounted  = false;
    _type     = CardType::NONE;
    _freq_khz = 0;
}

/* ── Capacity ─────────────────────────────────────────────────── */

uint64_t SDMMC_Class::cardSize()   const { return _mounted ? SD_MMC.cardSize()   : 0; }
uint64_t SDMMC_Class::totalBytes() const { return _mounted ? SD_MMC.totalBytes() : 0; }
uint64_t SDMMC_Class::usedBytes()  const { return _mounted ? SD_MMC.usedBytes()  : 0; }
uint64_t SDMMC_Class::freeBytes()  const
{
    if (!_mounted) return 0;
    const uint64_t total = totalBytes();
    const uint64_t used = usedBytes();
    return used <= total ? total - used : 0;
}

/* ── Active hot-plug probe ────────────────────────────────────── */

bool SDMMC_Class::isPresent()
{
    if (!_mounted) return false;
    File f = SD_MMC.open("/");
    bool ok = (bool)f;
    if (f) f.close();
    if (!ok) {
        /* Card was removed; release the stale mount and reset state. */
        end();
    }
    return ok;
}

/* ── String helpers ───────────────────────────────────────────── */

const char* SDMMC_Class::cardTypeStr() const
{
    switch (_type) {
        case CardType::MMC:  return "MMC";
        case CardType::SD:   return "SD";
        case CardType::SDHC: return "SDHC";
        case CardType::SDXC: return "SDXC";
        default:             return "NONE";
    }
}

/* ── Private ──────────────────────────────────────────────────── */

bool SDMMC_Class::_tryMount(uint32_t freq_khz, bool mode4bit)
{
    /* Arduino-ESP32 2.0.6 takes the SDMMC frequency in kHz. */
    bool ok = SD_MMC.begin("/sdcard", !mode4bit, false, freq_khz);
    if (!ok) return false;

    uint8_t ct = SD_MMC.cardType();
    if (ct == CARD_NONE) {
        SD_MMC.end();
        return false;
    }

    _mounted  = true;
    _freq_khz = freq_khz;
    _type     = _mapCardType(ct);
    return true;
}

SDMMC_Class::CardType SDMMC_Class::_mapCardType(uint8_t raw)
{
    switch (raw) {
        case CARD_MMC:  return CardType::MMC;
        case CARD_SD:   return CardType::SD;
        case CARD_SDHC: {
            /* SDHC ≤ 32 GB, SDXC > 32 GB — distinguish by size */
            uint64_t sz = SD_MMC.cardSize();
            return (sz > (uint64_t)32 * 1024 * 1024 * 1024) ? CardType::SDXC : CardType::SDHC;
        }
        default: return CardType::NONE;
    }
}

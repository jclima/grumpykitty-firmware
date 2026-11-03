/**
 * @file power_mgmt.cpp
 * @brief AXP173 power lifecycle — production implementation.
 */
#include "power_mgmt.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <cmath>

static AXP173_Class* s_pmu             = nullptr;
static void (*s_pre_shutdown_cb)(void) = nullptr;
static void (*s_warn_cb)(int)          = nullptr;

/* Warn state: reset to false when charging resumes */
static bool s_warn_shown = false;

/* millis() at power_init — used for boot grace period */
static uint32_t s_init_ms = 0;

/* Consecutive critical-battery samples seen by power_tick() */
static uint8_t s_crit_count = 0;
#define POWER_CRIT_CONSEC_SAMPLES 5

/* Last valid battery percentage (returned when a new sample is invalid) */
static int s_last_pct = 100;

/* Read one battery sample. Returns false (and leaves *pct untouched) when the
 * I2C read failed. */
static bool power_sample_pct(int* pct)
{
    if (!s_pmu) return false;
    float lvl = s_pmu->getBatLevel();
    if (std::isnan(lvl)) return false;
    if (lvl < 0)   lvl = 0;
    if (lvl > 100) lvl = 100;
    int p = (int)(lvl + 0.5f);
    s_last_pct = p;
    *pct = p;
    return true;
}

/* Idle timer — reset by power_reset_sleep_timer() on any user activity */
static uint32_t s_last_activity_ms = 0;

/* ── Init ─────────────────────────────────────────────── */

extern "C" void power_init(AXP173_Class* pmu)
{
    s_pmu = pmu;
    if (!s_pmu) return;

    /* Hardware undervoltage cutoff — AXP173 cuts all rails automatically.
     * Value matches devices.cpp setVoffVoltage(2900). */
    s_pmu->setVoffVoltage(POWER_VOFF_MV);

    /* Long-press 4 s → AXP173 hardware power-off (works even if firmware frozen) */
    s_pmu->setPowerOffTime(POWEROFF_4S);

    /* Short PEK press 512 ms → power on from off state */
    s_pmu->setPowerOnTime(POWERON_512mS);

    s_init_ms          = millis();
    s_last_activity_ms = millis();

    Serial.printf("[Power] init  bat=%d%%  vbus=%s  charging=%s\n",
                  power_battery_pct(),
                  s_pmu->isVBUSExist() ? "yes" : "no",
                  s_pmu->isCharging()  ? "yes" : "no");
}

/* ── Callbacks ────────────────────────────────────────── */

extern "C" void power_set_pre_shutdown_cb(void (*cb)(void)) { s_pre_shutdown_cb = cb; }
extern "C" void power_set_warn_cb(void (*cb)(int pct))      { s_warn_cb = cb; }

/* ── Idle timer ───────────────────────────────────────── */

extern "C" void power_reset_sleep_timer(void)
{
    s_last_activity_ms = millis();
}

extern "C" uint32_t power_idle_seconds(void)
{
    return (millis() - s_last_activity_ms) / 1000UL;
}

/* ── Poll (1 Hz) ──────────────────────────────────────── */

extern "C" bool power_is_ready(void)
{
    return (millis() - s_init_ms) >= (uint32_t)POWER_BOOT_GRACE_S * 1000u;
}

extern "C" void power_tick(void)
{
    if (!s_pmu) return;

    /* Skip all battery protection during boot grace — AXP173 ADC hasn't settled. */
    if (!power_is_ready()) return;

    /* Invalid (I2C failure) samples are ignored: they neither trigger nor
     * clear the critical counter. */
    int pct = 0;
    if (!power_sample_pct(&pct)) return;

    const bool charging = power_is_charging();
    const bool usb      = s_pmu->isVBUSExist();

    /* Reset warn state when charging restores battery above warn threshold */
    if (charging && s_warn_shown && pct > POWER_WARN_BAT_PCT) {
        s_warn_shown = false;
    }

    /* Never critical-shutdown while on USB power or charging. */
    if (charging || usb) {
        s_crit_count = 0;
        return;
    }

    /* ① Critical: require N consecutive critical samples (1 Hz tick) so a
     * single glitch/transient sag cannot power the device off. */
    if (pct <= POWER_CRIT_BAT_PCT) {
        if (++s_crit_count >= POWER_CRIT_CONSEC_SAMPLES) {
            Serial.printf("[Power] CRITICAL %d%% (%u samples) — shutting down\n",
                          pct, (unsigned)s_crit_count);
            power_shutdown();
            /* power_shutdown calls powerOFF() — never returns */
        }
    } else {
        s_crit_count = 0;
    }

    /* ② Warning: notify UI once per low-battery episode */
    if (pct <= POWER_WARN_BAT_PCT && !s_warn_shown) {
        s_warn_shown = true;
        Serial.printf("[Power] LOW BATTERY: %d%%\n", pct);
        if (s_warn_cb) s_warn_cb(pct);
    }
}

/* ── Shutdown ─────────────────────────────────────────── */

extern "C" void power_shutdown(void)
{
    if (!s_pmu) return;
    Serial.println("[Power] Shutting down...");
    Serial.flush();
    if (s_pre_shutdown_cb) s_pre_shutdown_cb();
    s_pmu->powerOFF();
    /* AXP173 cuts all rails — MCU loses power immediately */
}

/* ── Shipping mode ────────────────────────────────────── */

extern "C" void power_enter_ship_mode(void)
{
    if (!s_pmu) return;
    /* Ship mode = powerOFF. AXP173 auto-starts when VBUS is inserted next time.
     * This is the "开箱激活" mechanism: plug USB-C → device powers on, no button needed. */
    Serial.println("[Power] Entering ship mode — plug USB-C to activate");
    Serial.flush();
    delay(200);
    if (s_pre_shutdown_cb) s_pre_shutdown_cb();
    s_pmu->powerOFF();
}

/* ── Deep sleep ───────────────────────────────────────── */

extern "C" void power_deep_sleep(uint32_t wake_after_sec)
{
    Serial.printf("[Power] Deep sleep (%lus)\n", (unsigned long)wake_after_sec);
    Serial.flush();
    delay(50);

    if (wake_after_sec > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)wake_after_sec * 1000000ULL);
    }
    /* NOTE: HAL_PIN_PMU_IRQ = GPIO43 is not an RTC GPIO on ESP32-S3 and cannot
     * be used as EXT0/EXT1 wake source. Route AXP173 IRQ to a GPIO ≤ 21 in
     * hardware to enable PEK-press wake from deep sleep. */
    esp_deep_sleep_start();
}

/* ── Status queries ───────────────────────────────────── */

extern "C" int power_battery_pct(void)
{
    if (!s_pmu) return 0;
    int pct = 0;
    if (!power_sample_pct(&pct)) pct = s_last_pct;   /* I2C failure: last good value */
    /* During boot grace, a 0% reading is likely an unsettled coulometer.
     * Return 100 as a benign placeholder so the UI and warn_cb stay quiet. */
    if (pct == 0 && !power_is_ready()) return 100;
    return pct;
}

extern "C" bool power_is_charging(void)
{
    return s_pmu ? s_pmu->isCharging() : false;
}

extern "C" bool power_is_low_battery(void)
{
    return power_battery_pct() <= POWER_WARN_BAT_PCT && !power_is_charging();
}

extern "C" bool power_is_critical_battery(void)
{
    return power_battery_pct() <= POWER_CRIT_BAT_PCT && !power_is_charging();
}

extern "C" bool power_vbus_present(void)
{
    return s_pmu ? s_pmu->isVBUSExist() : false;
}

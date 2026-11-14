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

/* ── Battery percentage smoothing ─────────────────────────────────────────
 * Only power_tick() (1 Hz, main loop) touches the PMU. It feeds an EMA of the
 * battery voltage; the displayed percentage follows curve(EMA) with hysteresis
 * (down only on battery, up only on USB) at one point per PCT_STEP_MS.
 * power_battery_pct() just returns the cached value. */
#define PCT_STEP_MS        10000u   /* at most 1 displayed point per 10 s   */
#define PCT_MIN_DELTA      2        /* target must differ by >= 2 to step   */
#define VBUS_SETTLE_MS     5000u    /* ignore samples after plug/unplug     */
#define PCT_FULL_TARGET    90       /* charge-complete floor (no way to tell why PMU is not charging) */
#define PCT_DIVERGE_DELTA  5        /* |target - displayed| that counts as divergence */
#define PCT_DIVERGE_TICKS  30       /* consecutive valid ticks before snapping */
#define V_EMA_ALPHA        (1.0f / 16.0f)

static volatile int s_disp_pct   = 100;
static float        s_v_ema      = 0.0f;
static bool         s_seeded     = false;
static bool         s_last_vbus  = false;
static uint32_t     s_settle_until  = 0;
static uint32_t     s_last_step_ms  = 0;
static bool         s_settling      = false;   /* VBUS-change settle window active */
static bool         s_settle_unplug = false;   /* the settle in progress follows an unplug */
static uint8_t      s_div_count     = 0;       /* consecutive diverged valid ticks */
static bool         s_post_grace_seeded = false;

/* true once `now` has reached `until` (wrap-safe). */
static inline bool time_reached(uint32_t now, uint32_t until)
{
    return (uint32_t)(now - until) < 0x80000000u;
}

/* Raw reading: only an I2C failure (NaN) is invalid. A 0 V reading is valid
 * here so the critical-shutdown path counts it as 0%, as it always did. */
static bool read_voltage_raw(float* v)
{
    if (!s_pmu) return false;
    const float x = s_pmu->getBatVoltage();
    if (std::isnan(x)) return false;
    *v = x;
    return true;
}

/* Smoothing/display input: also skips implausible (< 1 V) readings. */
static bool voltage_plausible(float x) { return x >= 1.0f; }

static bool read_voltage(float* v)
{
    float x;
    if (!read_voltage_raw(&x) || !voltage_plausible(x)) return false;
    *v = x;
    return true;
}

static int curve_pct(float v)
{
    float lvl = AXP173_Class::batLevelFromVoltage(v);
    if (lvl < 0)   lvl = 0;
    if (lvl > 100) lvl = 100;
    return (int)(lvl + 0.5f);
}

/* Median of up to 3 spaced reads (the PMU ADC refreshes every ~25 ms).
 * Returns false when every read failed. */
static bool read_voltage_median(float* out)
{
    float r[3];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        float v;
        if (read_voltage(&v)) r[n++] = v;
        if (i < 2) delay(30);
    }
    if (n == 0) return false;
    if (n == 1) { *out = r[0]; return true; }
    if (n == 2) { *out = 0.5f * (r[0] + r[1]); return true; }
    if (r[0] > r[1]) { float t = r[0]; r[0] = r[1]; r[1] = t; }
    if (r[1] > r[2]) { float t = r[1]; r[1] = r[2]; r[2] = t; }
    if (r[0] > r[1]) { float t = r[0]; r[0] = r[1]; r[1] = t; }
    *out = r[1];
    return true;
}

/* Seed filter and displayed value from `v`. */
static void seed_all(float v, uint32_t now)
{
    s_v_ema        = v;
    s_seeded       = true;
    s_disp_pct     = curve_pct(v);
    s_last_step_ms = now;
}

/* Displayed percentage, capped by what the filtered voltage says. Used for
 * low-battery decisions so a stale/high displayed value can never mask a low cell. */
static int power_effective_pct(void)
{
    int pct = power_battery_pct();
    if (s_pmu && s_seeded && power_is_ready()) {
        const int e = curve_pct(s_v_ema);
        if (e < pct) pct = e;
    }
    return pct;
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

    s_last_vbus = s_pmu->isVBUSExist();
    s_settling  = false;
    float v0;
    if (read_voltage_median(&v0)) seed_all(v0, millis());

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

    uint32_t       now  = millis();
    const bool     usb  = s_pmu->isVBUSExist();
    float          v    = 0.0f;
    const bool     raw_valid = read_voltage_raw(&v);        /* critical path: NaN only */
    const bool     valid     = raw_valid && voltage_plausible(v);   /* display path */

    /* USB plug/unplug shifts the terminal voltage: ignore samples for a
     * while, then re-seed the filter (the displayed value is kept). */
    if (usb != s_last_vbus) {
        s_last_vbus    = usb;
        s_settling     = true;
        s_settle_unplug = !usb;
        s_settle_until = now + VBUS_SETTLE_MS;
        s_div_count    = 0;
    }
    if (valid) {
        if (s_settling) {
            if (time_reached(now, s_settle_until)) {
                float vm = v;
                read_voltage_median(&vm);
                s_v_ema    = vm;
                s_seeded   = true;
                s_settling = false;
                /* After an unplug the displayed value may have been seeded from
                 * the raised charging voltage: never keep it above the real one. */
                if (s_settle_unplug) {
                    const int t = curve_pct(vm);
                    if (t < s_disp_pct) s_disp_pct = t;
                }
            }
        } else if (!s_seeded) {
            seed_all(v, now);
        } else {
            s_v_ema += (v - s_v_ema) * V_EMA_ALPHA;
        }
    }

    /* Skip all battery protection during boot grace — AXP173 ADC hasn't settled. */
    if (!power_is_ready()) return;

    /* First tick after the grace window: re-seed from a fresh median. */
    if (!s_post_grace_seeded) {
        s_post_grace_seeded = true;
        float vm;
        if (read_voltage_median(&vm)) {
            now = millis();   /* the median read took ~60 ms */
            seed_all(vm, now);
            s_settling = false;
            s_div_count = 0;
        }
    }

    const bool charging = power_is_charging();

    /* Displayed percentage: hysteresis + rate limit. */
    if (s_seeded && !s_settling) {
        const int  target_raw = curve_pct(s_v_ema);
        /* Charge complete: VBUS present, battery present, PMU done charging. */
        const bool full       = usb && !charging && target_raw >= PCT_FULL_TARGET &&
                                s_pmu->isBatExist();
        const int  target     = full ? 100 : target_raw;
        const int  cur        = s_disp_pct;

        /* Divergence escape: displayed value stuck far from the filtered one. */
        if (valid) {
            const int d = target_raw - cur;
            if (d >= PCT_DIVERGE_DELTA || d <= -PCT_DIVERGE_DELTA) {
                if (++s_div_count >= PCT_DIVERGE_TICKS) {
                    s_disp_pct     = target_raw;
                    s_last_step_ms = now;
                    s_div_count    = 0;
                }
            } else {
                s_div_count = 0;
            }
        }

        if (s_disp_pct == cur && (uint32_t)(now - s_last_step_ms) >= PCT_STEP_MS) {
            if (!usb && target <= cur - PCT_MIN_DELTA) {
                s_disp_pct = cur - 1;
                s_last_step_ms = now;
            } else if (usb && cur < target &&
                       (target - cur >= PCT_MIN_DELTA || full)) {
                s_disp_pct = cur + 1;
                s_last_step_ms = now;
            }
        }
    }

    /* Invalid (I2C failure) samples are ignored: they neither trigger nor
     * clear the critical counter. */
    if (!raw_valid) return;

    /* Critical shutdown keeps its original input: the unfiltered reading of
     * this tick (0 V counts as 0%), and the same N-consecutive-sample rule. */
    const int raw_pct = curve_pct(v);
    const int pct     = power_effective_pct();   /* warning: never above the filtered voltage */

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
    if (raw_pct <= POWER_CRIT_BAT_PCT) {
        if (++s_crit_count >= POWER_CRIT_CONSEC_SAMPLES) {
            Serial.printf("[Power] CRITICAL %d%% (%u samples) — shutting down\n",
                          raw_pct, (unsigned)s_crit_count);
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

/* powerOFF() is an I2C write and can fail. If the rails do not drop, callers
 * have already closed apps and must not keep running (or call shutdown again
 * every second): retry once, then park in deep sleep with no wake source so
 * the firmware never resumes. */
static void power_off_or_halt(void)
{
    s_pmu->powerOFF();
    delay(200);                 /* rails normally drop well within this */
    Serial.println("[Power] powerOFF did not take effect — retrying");
    Serial.flush();
    s_pmu->powerOFF();
    delay(200);
    Serial.println("[Power] powerOFF failed — halting in deep sleep");
    Serial.flush();
    esp_deep_sleep_start();     /* no wake source configured: never returns */
}

extern "C" void power_shutdown(void)
{
    if (!s_pmu) return;
    Serial.println("[Power] Shutting down...");
    Serial.flush();
    /* pre_shutdown_cb closes a running app first (onClose), flushes settings. */
    if (s_pre_shutdown_cb) s_pre_shutdown_cb();
    power_off_or_halt();
    /* AXP173 cuts all rails — MCU loses power immediately; never returns */
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
    power_off_or_halt();
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
    const int pct = s_disp_pct;   /* cached; sampled only by power_tick()/init */
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
    return power_effective_pct() <= POWER_WARN_BAT_PCT && !power_is_charging();
}

extern "C" bool power_is_critical_battery(void)
{
    return power_effective_pct() <= POWER_CRIT_BAT_PCT && !power_is_charging();
}

extern "C" bool power_vbus_present(void)
{
    return s_pmu ? s_pmu->isVBUSExist() : false;
}

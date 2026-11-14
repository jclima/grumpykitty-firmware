#include "time_sync.h"
#include "settings_bridge.h"
#include "../ui/ui_rtc_bridge.h"
#include "../ui/ui_wifi_bridge.h"
#include <Arduino.h>
#include <time.h>
#include <lwip/apps/sntp.h>
#include <esp_sntp.h>

/* 2024-01-01 00:00:00 UTC: anything earlier means the clock is not set. */
static constexpr time_t   kEarliestValid = 1704067200;
static constexpr uint32_t kWaitMs        = 60000;                 /* give up after a connect */
static constexpr uint32_t kMinRewriteMs  = 6UL * 3600UL * 1000UL; /* periodic SNTP updates */

static volatile bool s_sync_flag = false;   /* set by the SNTP callback only */

static wifi_bridge_status_t s_prev_status = WIFI_BRIDGE_IDLE;
static bool     s_prev_auto  = false;
static bool     s_waiting    = false;   /* a request is in flight */
static bool     s_ignore     = false;   /* gave up: ignore late results until the next request */
static uint32_t s_wait_start = 0;
static bool     s_have_write = false;   /* RTC written at least once since boot */
static uint32_t s_write_ms   = 0;
static char     s_status_hm[6] = "";    /* "HH:MM" local time of the last write */

static void _on_sync(struct timeval *tv)
{
    (void)tv;
    s_sync_flag = true;
}

extern "C" void time_sync_init(void)
{
    sntp_set_time_sync_notification_cb(_on_sync);
}

/* Write the RTC from the system UTC clock + the configured offset. */
static bool _write_rtc(void)
{
    const time_t utc = time(nullptr);
    if (utc < kEarliestValid) return false;
    const time_t local = utc + (time_t)settings_get_tz_off_min() * 60;
    struct tm tm;
    gmtime_r(&local, &tm);
    if (!ui_rtc_bridge_set_datetime((uint16_t)(tm.tm_year + 1900), (uint8_t)(tm.tm_mon + 1),
                                    (uint8_t)tm.tm_mday, (uint8_t)tm.tm_hour,
                                    (uint8_t)tm.tm_min, (uint8_t)tm.tm_sec))
        return false;
    snprintf(s_status_hm, sizeof(s_status_hm), "%02d:%02d", tm.tm_hour, tm.tm_min);
    s_have_write = true;
    s_write_ms   = millis();
    return true;
}

static void _start_request(void)
{
    /* Fresh sync: start SNTP, or restart it when something (OTA, an earlier
     * connect) already enabled it so a new query goes out right away. */
    s_sync_flag = false;
    if (!sntp_enabled()) configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    else                 sntp_restart();
    s_waiting    = true;
    s_ignore     = false;
    s_wait_start = millis();
}

extern "C" void time_sync_tick(void)
{
    const bool auto_on = settings_get_time_auto() != 0;
    const wifi_bridge_status_t st = ui_wifi_bridge_get_status();
    const bool connected_edge = (st != s_prev_status) && st == WIFI_BRIDGE_CONNECTED;
    const bool auto_edge      = auto_on && !s_prev_auto && st == WIFI_BRIDGE_CONNECTED;
    s_prev_status = st;
    s_prev_auto   = auto_on;

    if (!auto_on) {
        s_sync_flag = false;
        s_waiting   = false;
        return;
    }

    if (connected_edge || auto_edge) _start_request();

    if (s_waiting && (uint32_t)(millis() - s_wait_start) >= kWaitMs) {
        s_waiting = false;
        s_ignore  = true;
    }

    if (!s_sync_flag) return;
    s_sync_flag = false;
    if (s_ignore) return;
    /* The first result after a request always writes; later periodic SNTP
     * updates rewrite the RTC at most every 6 h. */
    if (!s_waiting && s_have_write && (uint32_t)(millis() - s_write_ms) < kMinRewriteMs) return;
    if (_write_rtc()) s_waiting = false;
}

extern "C" bool time_sync_status(char *buf, int len)
{
    if (!buf || len <= 0) return false;
    if (s_have_write) {
        snprintf(buf, (size_t)len, "Synced %s", s_status_hm);
        return true;
    }
    snprintf(buf, (size_t)len, "Not synced");
    return false;
}

extern "C" void time_sync_offset_changed(void)
{
    if (!settings_get_time_auto() || !s_have_write) return;
    _write_rtc();
}

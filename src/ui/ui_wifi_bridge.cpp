/**
 * @file ui_wifi_bridge.cpp
 * @brief WiFi bridge — Arduino WiFi for LVGL screens (ESP32-S3).
 *
 * Root causes fixed vs original:
 *  1. WiFi.scanNetworks() fails when driver is busy connecting.
 *     Fix: disconnect before scan, save target and reconnect after.
 *  2. Polling WiFi.status() gives unreliable state.
 *     Fix: WiFi.onEvent() event handler updates s_status atomically.
 *  3. WiFi.scanDelete() race between LVGL task and scan task.
 *     Fix: only the scan task calls scanDelete() on its own results.
 *  4. Intentional pre-scan disconnect must not be reported as FAILED.
 */
#include "ui_wifi_bridge.h"
#include "../system/persist.h"
#include "../system/settings_bridge.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include <esp_wifi.h>
#include <nvs.h>

/* ── NVS keys ────────────────────────────────────────────────── */
static constexpr char kKeySsid[] = PKEY_WIFI_SSID;
static constexpr char kKeyPass[] = PKEY_WIFI_PASS;

/* ── Connection state  (written by event handler, read by LVGL) ─
 *  volatile: 32-bit aligned enum reads/writes are atomic on Xtensa */
static volatile wifi_bridge_status_t s_status = WIFI_BRIDGE_IDLE;

/* Current SSID / IP — updated from event handler; short strings,
 * single-writer, read races are benign for UI purposes.           */
static char s_current_ssid[33] = "";
static char s_current_ip[16]   = "";

/* ── Scan state ──────────────────────────────────────────────────
 *  -2 = idle / not started
 *  -1 = scan task running
 *  ≥0 = results ready (count of APs found)
 */
static volatile int s_scan_count = -2;
static volatile int s_scan_gen   = 0;

/* Suppress disconnect events caused by interrupting a connection attempt. */
static volatile bool s_prescan_disconnect = false;

/* Reconnect after scan: saved here before we disconnect for the scan. */
static char  s_post_scan_ssid[33] = "";
static char  s_post_scan_pass[65] = "";
static bool  s_need_reconnect     = false;

/* Pending SSID from T9 keyboard screen */
static char s_pending_ssid[33] = "";

static bool s_inited = false;

/* Master radio switch (the Settings Wi-Fi toggle). While false nothing may
 * scan, connect or auto-reconnect. Seeded from settings in init. */
static volatile bool s_radio_on = true;

/* Protects scan-result reads/frees and the scan task bookkeeping below. */
static SemaphoreHandle_t s_lock = nullptr;
static inline void _lock(void)   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static inline void _unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

/* One worker owns scan results and deferred radio actions. */
static bool s_scan_task_active = false;
static bool s_monitor_active = false;

enum wifi_radio_action_t { WIFI_ACTION_NONE, WIFI_ACTION_CONNECT,
                           WIFI_ACTION_DISCONNECT, WIFI_ACTION_FORGET,
                           WIFI_ACTION_RADIO_OFF, WIFI_ACTION_RADIO_ON };
static wifi_radio_action_t s_pending_action = WIFI_ACTION_NONE;
static bool s_wipe_pending = false;   /* a Forget's IDF config wipe is still owed (under lock) */
static char s_pending_action_ssid[33] = "";
static char s_pending_action_pass[65] = "";

/* Credentials of the connection attempt in flight. Persisted to NVS only once
 * the link reaches GOT_IP, so a typo never overwrites working saved creds. */
static char s_try_ssid[33] = "";
static char s_try_pass[65] = "";
static bool s_try_valid    = false;

/* ── WiFi event handler ──────────────────────────────────────────
 *  Runs on the WiFi event task, NOT the LVGL/Arduino-loop task.
 *  Only touches volatile primitives and short char arrays.
 */
static void _wifi_event(WiFiEvent_t event, WiFiEventInfo_t /*info*/)
{
    switch (event) {

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            if (!s_radio_on) break;   /* radio being switched off: ignore */
            s_status = WIFI_BRIDGE_CONNECTED;
            {
                String ssid = WiFi.SSID();
                String ip   = WiFi.localIP().toString();
                strncpy(s_current_ssid, ssid.c_str(), sizeof(s_current_ssid) - 1);
                s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
                strncpy(s_current_ip, ip.c_str(), sizeof(s_current_ip) - 1);
                s_current_ip[sizeof(s_current_ip) - 1] = '\0';
            }
            Serial.printf("[wifi] Connected  SSID=%s  IP=%s\n",
                          s_current_ssid, s_current_ip);
            /* Persist credentials only now that they are proven to work. */
            {
                char try_ssid[33], try_pass[65];
                bool valid;
                _lock();
                valid = s_try_valid && strcmp(s_try_ssid, s_current_ssid) == 0;
                if (valid) {
                    strcpy(try_ssid, s_try_ssid);
                    strcpy(try_pass, s_try_pass);
                    s_try_valid = false;
                }
                _unlock();
                if (valid) {
                    char sv_ssid[33] = {}, sv_pass[65] = {};
                    persist_get_str(kKeySsid, sv_ssid, sizeof(sv_ssid), "");
                    persist_get_str(kKeyPass, sv_pass, sizeof(sv_pass), "");
                    if (strcmp(sv_ssid, try_ssid) != 0 || strcmp(sv_pass, try_pass) != 0) {
                        persist_set_str(kKeySsid, try_ssid);
                        persist_set_str(kKeyPass, try_pass);
                        Serial.println("[wifi] credentials saved");
                    }
                    memset(try_pass, 0, sizeof(try_pass));
                }
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            /* Ignore disconnect events that we ourselves trigger before a scan */
            if (s_prescan_disconnect) break;

            if (s_status == WIFI_BRIDGE_CONNECTING) {
                s_status = WIFI_BRIDGE_FAILED;
                Serial.println("[wifi] Connect failed");
            } else if (s_status == WIFI_BRIDGE_CONNECTED) {
                s_status = WIFI_BRIDGE_IDLE;
                s_current_ssid[0] = '\0';
                s_current_ip[0]   = '\0';
                Serial.println("[wifi] Link lost");
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            /* Authenticated with AP but no IP yet — keep CONNECTING */
            break;

        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            if (s_status == WIFI_BRIDGE_CONNECTED) {
                s_status = WIFI_BRIDGE_IDLE;
                s_current_ip[0] = '\0';
                Serial.println("[wifi] IP lost");
            }
            break;

        default:
            break;
    }
}

/* ── Scan FreeRTOS task ──────────────────────────────────────── */
static void _scan_task(void * /*arg*/)
{
    for (;;) {
        char ssid[33] = {}, pass[65] = {};
        _lock();
        const int gen = s_scan_gen;
        wifi_radio_action_t action = s_pending_action;
        s_pending_action = WIFI_ACTION_NONE;
        /* Owed wipe is consumed by FORGET or RADIO_OFF only. */
        bool do_wipe = false;
        if (action == WIFI_ACTION_FORGET || action == WIFI_ACTION_RADIO_OFF) {
            do_wipe = s_wipe_pending;
            s_wipe_pending = false;
        }

        if (action == WIFI_ACTION_NONE && s_scan_count == -1) {
            const bool interrupt = s_need_reconnect &&
                                   s_status == WIFI_BRIDGE_CONNECTING;
            if (!interrupt) s_need_reconnect = false;
            _unlock();

            if (interrupt) {
                s_prescan_disconnect = true;
                WiFi.disconnect(false);
                vTaskDelay(pdMS_TO_TICKS(200));
                s_prescan_disconnect = false;
            }

            _lock();
            const bool current = gen == s_scan_gen;
            _unlock();
            if (!current) continue;

            const int count = WiFi.scanNetworks(false, false, false, 400);
            _lock();
            if (gen == s_scan_gen) {
                s_scan_count = count >= 0 ? count : 0;
            } else {
                WiFi.scanDelete();
            }
            _unlock();
            continue;
        }

        if (action == WIFI_ACTION_CONNECT) {
            strcpy(ssid, s_pending_action_ssid);
            strcpy(pass, s_pending_action_pass);
        } else if (action == WIFI_ACTION_NONE && s_need_reconnect) {
            action = WIFI_ACTION_CONNECT;
            strcpy(ssid, s_post_scan_ssid);
            strcpy(pass, s_post_scan_pass);
        }
        s_need_reconnect = false;

        if (action == WIFI_ACTION_NONE) {
            s_scan_task_active = false;
            _unlock();
            break;
        }
        if (action == WIFI_ACTION_CONNECT) {
            strcpy(s_try_ssid, ssid);
            strcpy(s_try_pass, pass);
            s_try_valid = true;
        } else {
            s_try_valid = false;
        }
        _unlock();

        /* Go IDLE first so the disconnect event cannot flip it to FAILED. */
        if (action == WIFI_ACTION_RADIO_OFF) s_status = WIFI_BRIDGE_IDLE;

        /* Swallow the old link's DISCONNECTED event so it cannot fail the
         * attempt that follows. */
        s_prescan_disconnect = true;
        WiFi.disconnect(false);
        vTaskDelay(pdMS_TO_TICKS(200));
        s_prescan_disconnect = false;
        if (action == WIFI_ACTION_CONNECT) {
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(false);
            s_status = WIFI_BRIDGE_CONNECTING;
            if (WiFi.begin(ssid, pass) == WL_CONNECT_FAILED) {
                s_status = WIFI_BRIDGE_FAILED;
            }
        } else if (action == WIFI_ACTION_RADIO_OFF) {
            /* Wipe while the driver is still initialised (esp_wifi_restore
             * needs it), before the mode change. */
            if (do_wipe) ui_wifi_bridge_wipe_stored_config();
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            s_status = WIFI_BRIDGE_IDLE;
            s_current_ssid[0] = '\0';
            s_current_ip[0] = '\0';
        } else if (action == WIFI_ACTION_RADIO_ON) {
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(false);
            s_status = WIFI_BRIDGE_IDLE;
        } else {
            s_status = WIFI_BRIDGE_IDLE;
            s_current_ssid[0] = '\0';
            s_current_ip[0] = '\0';
            if (action == WIFI_ACTION_FORGET && do_wipe) ui_wifi_bridge_wipe_stored_config();
        }
    }

    vTaskDelete(NULL);
}

/* ── Init ────────────────────────────────────────────────────── */
void ui_wifi_bridge_init(void)
{
    if (s_inited) return;
    s_inited = true;

    if (!s_lock) s_lock = xSemaphoreCreateMutex();

    /* Arduino core default is persistent=true (WIFI_STORAGE_FLASH): SSID and
     * password would be written in plaintext to the IDF 'nvs.net80211'
     * namespace. Keep the IDF config in RAM; we store creds via persist.cpp.
     * Must run before the first WiFi.mode()/begin(). */
    WiFi.persistent(false);

    WiFi.onEvent(_wifi_event);
    s_radio_on = settings_get_wifi_en() != 0;
    if (!s_radio_on) {
        Serial.println("[wifi] disabled in settings — radio off");
        WiFi.mode(WIFI_OFF);
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   /* we manage reconnect explicitly */

    char ssid[33] = {};
    char pass[65] = {};
    persist_get_str(kKeySsid, ssid, sizeof(ssid), "");
    persist_get_str(kKeyPass, pass, sizeof(pass), "");
    if (ssid[0] != '\0') {
        Serial.printf("[wifi] Auto-connect → %s\n", ssid);
        s_status = WIFI_BRIDGE_CONNECTING;
        WiFi.begin(ssid, pass);
    }
}

/* Caller holds the lock.  Remember an in-flight connection attempt so the scan
 * worker can restore it after scanning. */
static void _capture_reconnect_locked(void)
{
    s_need_reconnect = (s_status == WIFI_BRIDGE_CONNECTING);
    if (!s_need_reconnect) return;
    if (s_try_valid) {   /* attempt in flight: not saved yet */
        strcpy(s_post_scan_ssid, s_try_ssid);
        strcpy(s_post_scan_pass, s_try_pass);
    } else {
        persist_get_str(kKeySsid, s_post_scan_ssid, sizeof(s_post_scan_ssid), "");
        persist_get_str(kKeyPass, s_post_scan_pass, sizeof(s_post_scan_pass), "");
    }
    if (s_post_scan_ssid[0] == '\0') s_need_reconnect = false;
}

/* ── Scan ────────────────────────────────────────────────────── */
void ui_wifi_bridge_start_scan(void)
{
    _lock();
    if (s_monitor_active) { _unlock(); return; }
    if (!s_radio_on) {   /* finished, empty */
        s_scan_count = 0;
        _unlock();
        return;
    }

    /* A scan task is already running: don't spawn a second one, ask it to
     * scan again (it discards its current results first).  Unless a queued
     * action will run first, an attempt in flight must still be restored. */
    if (s_scan_task_active) {
        if (s_pending_action == WIFI_ACTION_NONE) _capture_reconnect_locked();
        s_scan_gen++;
        s_scan_count   = -1;
        _unlock();
        Serial.println("[wifi] scan already running — rescan queued");
        return;
    }

    /* If we are currently connecting/connected, save the target so we
     * can reconnect after the scan finishes.                          */
    /* Only an interrupted connection attempt needs restoration. An associated
     * station remains connected while the ESP32 performs a normal scan. */
    _capture_reconnect_locked();

    /* Advance generation (invalidates any previous results) */
    s_scan_gen++;
    s_scan_count   = -1;      /* mark: scan in progress */

    int gen = (int)s_scan_gen;
    BaseType_t rc = xTaskCreate(_scan_task, "wifi_scan", 4096, NULL, 3, NULL);
    if (rc != pdPASS) {
        s_scan_count   = 0;
        s_need_reconnect = false;
        Serial.println("[wifi] ERROR: scan task creation failed");
    } else {
        s_scan_task_active = true;
        Serial.printf("[wifi] scan task started (gen=%d, reconnect=%d)\n",
                      gen, (int)s_need_reconnect);
    }
    _unlock();
}

void ui_wifi_bridge_scan(void) { ui_wifi_bridge_start_scan(); }

void ui_wifi_bridge_stop_scan(void)
{
    _lock();
    s_scan_gen++;          /* invalidate running task's generation */
    s_scan_count  = -2;    /* idle */
    _unlock();
    /* Do NOT call WiFi.scanDelete() here — the scan task owns the buffer
     * and will call scanDelete() itself when it detects gen mismatch.   */
    Serial.println("[wifi] scan stopped");
}

int  ui_wifi_bridge_get_scan_count(void)
{
    int n = (int)s_scan_count;
    return (n < 0) ? 0 : n;
}

bool ui_wifi_bridge_is_scanning(void)   { return (s_scan_count == -1); }
bool ui_wifi_bridge_scan_complete(void) { return (s_scan_count >= 0);  }

bool ui_wifi_bridge_radio_busy(void)
{
    _lock();
    bool busy = s_scan_task_active || s_monitor_active;
    _unlock();
    return busy;
}

bool ui_wifi_bridge_try_acquire_monitor(void)
{
    if (!s_lock) return false;
    _lock();
    const bool idle = !s_scan_task_active && !s_monitor_active && s_status != WIFI_BRIDGE_CONNECTING;
    if (idle) s_monitor_active = true;
    _unlock();
    return idle;
}

void ui_wifi_bridge_release_monitor(void)
{
    _lock();
    s_monitor_active = false;
    _unlock();
}

void ui_wifi_bridge_get_ssid(int idx, char *buf, int len)
{
    if (!buf || !len) return;
    buf[0] = '\0';
    _lock();
    if (idx >= 0 && idx < (int)s_scan_count) {
        String s = WiFi.SSID(idx);
        strncpy(buf, s.c_str(), len - 1);
        buf[len - 1] = '\0';
    }
    _unlock();
}

int ui_wifi_bridge_get_rssi(int idx)
{
    int r = -100;
    _lock();
    if (idx >= 0 && idx < (int)s_scan_count) r = WiFi.RSSI(idx);
    _unlock();
    return r;
}

bool ui_wifi_bridge_is_encrypted(int idx)
{
    bool e = false;
    _lock();
    if (idx >= 0 && idx < (int)s_scan_count) e = (WiFi.encryptionType(idx) != WIFI_AUTH_OPEN);
    _unlock();
    return e;
}

bool ui_wifi_bridge_get_ap(int i, char *ssid, int ssid_len, int *pct_out, bool *saved_out)
{
    int rssi = -100;
    _lock();
    int n = (int)s_scan_count;
    if (n < 0 || i < 0 || i >= n) { _unlock(); return false; }
    String s = WiFi.SSID(i);
    rssi = WiFi.RSSI(i);
    _unlock();

    strncpy(ssid, s.c_str(), ssid_len - 1);
    ssid[ssid_len - 1] = '\0';

    if (pct_out) {
        int pct  = (rssi < -100) ? 0 : (rssi > -50) ? 100 : 2 * (rssi + 100);
        *pct_out = pct;
    }

    if (saved_out) {
        char saved[33] = {};
        persist_get_str(kKeySsid, saved, sizeof(saved), "");
        *saved_out = (strcmp(ssid, saved) == 0);
    }

    return true;
}

/* ── Connect ─────────────────────────────────────────────────── */
void ui_wifi_bridge_connect(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') return;

    /* Stop any in-progress scan before connecting. The scan task (sole owner
     * of the results) notices the generation change and frees them itself. */
    _lock();
    if (s_monitor_active || !s_radio_on) { _unlock(); return; }
    bool was_scanning = s_scan_task_active;
    if (was_scanning) {
        s_scan_gen++;
        s_scan_count  = -2;
        s_need_reconnect = false;   /* we are choosing a new target */
        s_pending_action = WIFI_ACTION_CONNECT;
        strncpy(s_pending_action_ssid, ssid, sizeof(s_pending_action_ssid) - 1);
        s_pending_action_ssid[sizeof(s_pending_action_ssid) - 1] = '\0';
        strncpy(s_pending_action_pass, pass ? pass : "", sizeof(s_pending_action_pass) - 1);
        s_pending_action_pass[sizeof(s_pending_action_pass) - 1] = '\0';
        /* Report the new attempt now, not a stale FAILED/CONNECTED. */
        s_status = WIFI_BRIDGE_CONNECTING;
        s_current_ssid[0] = '\0';
        s_current_ip[0]   = '\0';
    }
    /* Remember the attempt in RAM; persisted only on GOT_IP. Deferred
     * connects are promoted by the worker when the radio is available. */
    if (!was_scanning) {
        strncpy(s_try_ssid, ssid, sizeof(s_try_ssid) - 1);
        s_try_ssid[sizeof(s_try_ssid) - 1] = '\0';
        strncpy(s_try_pass, pass ? pass : "", sizeof(s_try_pass) - 1);
        s_try_pass[sizeof(s_try_pass) - 1] = '\0';
        s_try_valid = true;
    }
    _unlock();
    if (was_scanning) {
        Serial.printf("[wifi] connect queued until scan worker finishes → %s\n", ssid);
        return;
    }

    Serial.printf("[wifi] Connecting → %s\n", ssid);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    s_status = WIFI_BRIDGE_CONNECTING;
    WiFi.begin(s_try_ssid, s_try_pass);
}

void ui_wifi_bridge_connect_saved(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return;
    char sv_ssid[33] = {};
    char sv_pass[65] = {};
    persist_get_str(kKeySsid, sv_ssid, sizeof(sv_ssid), "");
    persist_get_str(kKeyPass, sv_pass, sizeof(sv_pass), "");
    const char *pass = (strcmp(sv_ssid, ssid) == 0) ? sv_pass : "";
    ui_wifi_bridge_connect(ssid, pass);
}

void ui_wifi_bridge_set_pending_ssid(const char *ssid)
{
    if (!ssid) return;
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';
}

void ui_wifi_bridge_connect_pending(const char *password)
{
    if (s_pending_ssid[0] == '\0') return;
    ui_wifi_bridge_connect(s_pending_ssid, password ? password : "");
    /* Keep the target available for retries from the same provisioning screen. */
}

void ui_wifi_bridge_disconnect(void)
{
    _lock();
    if (s_monitor_active) { _unlock(); return; }
    s_try_valid = false;
    bool was_scanning = s_scan_task_active;
    if (was_scanning) {
        s_scan_gen++;
        s_scan_count = -2;
        s_need_reconnect = false;
        if (s_pending_action != WIFI_ACTION_RADIO_OFF)
            s_pending_action = WIFI_ACTION_DISCONNECT;
        s_status = WIFI_BRIDGE_IDLE;
        s_current_ssid[0] = '\0';
        s_current_ip[0]   = '\0';
    }
    _unlock();
    if (was_scanning) {
        Serial.println("[wifi] disconnect queued until scan worker finishes");
        return;
    }
    WiFi.disconnect(false);
    s_status = WIFI_BRIDGE_IDLE;
    s_current_ssid[0] = '\0';
    s_current_ip[0]   = '\0';
}

/* ── Radio on/off (Settings Wi-Fi switch) ────────────────────── */
bool ui_wifi_bridge_set_enabled(bool on)
{
    if (!s_inited) return true;   /* init seeds the state from settings */

    if (!on) {
        _lock();
        if (s_monitor_active) { _unlock(); return false; }
        s_radio_on = false;
        s_try_valid = false;
        s_need_reconnect = false;
        s_scan_gen++;
        s_scan_count = -2;
        s_current_ssid[0] = '\0';
        s_current_ip[0]   = '\0';
        const bool worker = s_scan_task_active;
        if (worker) {
            /* Never stop the radio while the worker is inside scanNetworks. */
            s_pending_action = WIFI_ACTION_RADIO_OFF;
            s_status = WIFI_BRIDGE_IDLE;
        }
        _unlock();
        if (worker) {
            Serial.println("[wifi] radio off queued until scan worker finishes");
            return true;
        }
        s_status = WIFI_BRIDGE_IDLE;   /* before disconnecting: no FAILED */
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("[wifi] radio off");
        return true;
    }

    _lock();
    if (s_monitor_active) { _unlock(); return false; }
    s_radio_on = true;
    const bool worker = s_scan_task_active;
    _unlock();

    char sv_ssid[33] = {};
    persist_get_str(kKeySsid, sv_ssid, sizeof(sv_ssid), "");
    if (!worker) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
        s_status = WIFI_BRIDGE_IDLE;
    }
    if (sv_ssid[0] != '\0') {
        /* Queues behind the worker (replacing a pending RADIO_OFF) if busy. */
        ui_wifi_bridge_connect_saved(sv_ssid);
    } else if (worker) {
        /* The worker may have exited during the NVS read above: re-check. */
        _lock();
        const bool still_busy = s_scan_task_active;
        if (still_busy) s_pending_action = WIFI_ACTION_RADIO_ON;
        _unlock();
        if (!still_busy) {
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(false);
            s_status = WIFI_BRIDGE_IDLE;
        }
    }
    Serial.println("[wifi] radio on");
    return true;
}

/* ── State ───────────────────────────────────────────────────── */
wifi_bridge_status_t ui_wifi_bridge_get_status(void)
{
    return s_status;
}

void ui_wifi_bridge_get_status_text(char *buf, int len)
{
    if (!buf || len <= 0) return;
    if (!s_radio_on) { snprintf(buf, (size_t)len, "Wi-Fi off"); return; }
    switch (s_status) {
        case WIFI_BRIDGE_CONNECTED:
            snprintf(buf, (size_t)len, "Connected: %s", s_current_ssid);
            break;
        case WIFI_BRIDGE_CONNECTING:
            snprintf(buf, (size_t)len, "Connecting...");
            break;
        case WIFI_BRIDGE_FAILED:
            snprintf(buf, (size_t)len, "Connection failed");
            break;
        default:
            snprintf(buf, (size_t)len, "Not connected");
            break;
    }
}

bool ui_wifi_bridge_is_connected(void) { return (s_status == WIFI_BRIDGE_CONNECTED); }

void ui_wifi_bridge_get_ip(char *buf, int len)
{
    if (!buf || !len) return;
    strncpy(buf, s_current_ip, len - 1);
    buf[len - 1] = '\0';
}

const char *ui_wifi_bridge_get_current_ssid(void)
{
    /* If connected, return event-cached SSID; fall back to driver query. */
    if (s_current_ssid[0] == '\0' && s_status == WIFI_BRIDGE_CONNECTED) {
        String s = WiFi.SSID();
        strncpy(s_current_ssid, s.c_str(), sizeof(s_current_ssid) - 1);
        s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';
    }
    return s_current_ssid;
}

void ui_wifi_bridge_forget(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return;
    _lock();
    const bool monitoring = s_monitor_active;
    _unlock();
    if (monitoring) return;
    char saved[33] = {};
    persist_get_str(kKeySsid, saved, sizeof(saved), "");
    if (strcmp(ssid, saved) == 0) {
        /* Clear the secret first.  If that fails, keep the SSID record so the
         * user can retry the forget; log without any credential value. */
        if (!persist_set_str(kKeyPass, "") && !persist_set_str(kKeyPass, "")) {
            Serial.println("[wifi] WARNING: could not erase stored password; forget incomplete");
        } else if (!persist_set_str(kKeySsid, "")) {
            Serial.println("[wifi] WARNING: could not erase stored SSID");
        }
        _lock();
        s_try_valid = false;
        bool was_scanning = s_scan_task_active;
        if (was_scanning) {
            s_wipe_pending = true;   /* survives a RADIO_OFF overwriting FORGET */
            s_scan_gen++;
            s_scan_count = -2;
            s_need_reconnect = false;
            if (s_pending_action != WIFI_ACTION_RADIO_OFF)
                s_pending_action = WIFI_ACTION_FORGET;
            s_status = WIFI_BRIDGE_IDLE;
            s_current_ssid[0] = '\0';
            s_current_ip[0]   = '\0';
        }
        _unlock();
        if (was_scanning) {
            Serial.printf("[wifi] forget queued until scan worker finishes: %s\n", ssid);
            return;
        }
        ui_wifi_bridge_wipe_stored_config();
        WiFi.disconnect(false);
        s_status = WIFI_BRIDGE_IDLE;
        s_current_ssid[0] = '\0';
        s_current_ip[0]   = '\0';
        Serial.printf("[wifi] Forgot: %s\n", ssid);
    }
}

void ui_wifi_bridge_get_saved_pass(char *buf, int len)
{
    if (!buf || len <= 0) return;
    persist_get_str(kKeyPass, buf, len, "");
}

/* Erase any WiFi credentials the IDF itself may have persisted (e.g. by older
 * firmware that ran with persistent=true). Safe to call at any time. */
void ui_wifi_bridge_wipe_stored_config(void)
{
    if (WiFi.getMode() != WIFI_OFF) {
        esp_wifi_restore();   /* only valid once the driver is initialised */
    }
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

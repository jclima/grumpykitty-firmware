/**
 * @file launcher.cpp
 * @brief Launcher — persistent UI, two-state exclusive switching (GUI ↔ App)
 *
 * ═══════════════════════════════════════════════════════
 *  主干流程 (v2 — 取消 destroy/rebuild)
 * ═══════════════════════════════════════════════════════
 *
 *  onCreate() — 系统启动，执行一次：
 *    1. initSD()      挂载 SD 卡
 *    2. installApps() 注册 15 个原生应用到 Mooncake
 *    3. initLVGL()    lv_init + 显示/触摸/FS 驱动（一次性）
 *    4. buildUI()     创建 Home + AppsMenu 屏幕（一次性，常驻内存）
 *    5. lv_disp_load_scr(home)  首次启动显示 Home
 *
 *  onLoop() — 主循环，两态互斥：
 *
 *    ┌─ GUI 态（_app_running == false）──────────────────┐
 *    │  lv_timer_handler()    渲染 LVGL                  │
 *    │  handleAppSelection()  检测 icon 点击              │
 *    │  updateStatusBar()     1Hz 电池刷新                │
 *    └──────────────────────────────────────────────────┘
 *          │ 用户点击 icon
 *          ▼
 *    handleAppSelection():
 *      1. mooncake.openApp()  应用 onOpen() 接管 LCD
 *      2. _app_running = true
 *      （UI 屏幕保留在内存中，LVGL 暂停渲染）
 *
 *    ┌─ APP 态（_app_running == true）──────────────────┐
 *    │  button.tick()         长按B检测                  │
 *    │  mooncake.update()     驱动应用 onRunning()       │
 *    │  • 传统App: 直接 LovyanGFX 绘制（LVGL 空闲）     │
 *    │  • LvAppUI App: 自建 LVGL 屏幕 + lv_timer_handler│
 *    └──────────────────────────────────────────────────┘
 *          │ 用户长按 B ≥1s
 *          ▼
 *      1. mooncake.closeApp() 设置 StateGoClose
 *      2. mooncake.update()   驱动 onClose() 执行清理
 *      3. _app_running = false
 *      4. returnToUI()        切回 apps_menu + 强制 LVGL 全量重绘
 *
 * ═══════════════════════════════════════════════════════
 */
#include "launcher.h"
#include "../app.h"
#include "app_preferences.h"
#include "../app_03/vu_meter.h"
#include "../../bsp/porting/lv_port_disp.h"
#include "../../bsp/porting/lv_port_indev.h"
#include "../../bsp/porting/lv_port_fs.h"
#include "../../ui/ui_rtc_bridge.h"
#include "../../ui/ui_sd_bridge.h"
#include "../../ui/ui_wifi_bridge.h"
#include "../../ui/screens/ui_sd_card_files.h"
#include "../../system/usb_msc.h"
#include "../../system/usb_manager.h"
#include "../../system/settings_bridge.h"  /* includes persist internally */
#include "../../system/power_mgmt.h"
#include "../../system/ota_updater.h"
#include "../../system/mk_events.h"
#include "../../system/system_sound.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <lvgl.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <algorithm>

/* ────────────────────────────────────────────────────── */

/* Held for shutdown LED callback — plain C function can't capture 'this'. */
static DEVICES* s_shutdown_dev = nullptr;

/* Mirror of Launcher::_app_running for plain C callbacks (LVGL is paused in APP state). */
static volatile bool s_in_app = false;

static void _update_system_key_sound(DEVICES* dev)
{
    enum : uint8_t { A = 1u << 0, B = 1u << 1, U = 1u << 2,
                     D = 1u << 3, L = 1u << 4, R = 1u << 5 };
    static uint8_t previous = 0;
    uint8_t current = 0;
    if (dev->button.A.state() == Button_Class::PRESSED) current |= A;
    if (dev->button.B.state() == Button_Class::PRESSED) current |= B;
    if (dev->button.Up.state() == Button_Class::PRESSED) current |= U;
    if (dev->button.Down.state() == Button_Class::PRESSED) current |= D;
    if (dev->button.Left.state() == Button_Class::PRESSED) current |= L;
    if (dev->button.Right.state() == Button_Class::PRESSED) current |= R;
    if (current & (uint8_t)~previous) system_sound_play_button();
    previous = current;
}

/* True when the backlight has been turned off due to idle timeout. */
static bool s_screen_off = false;

/* Single place that changes the screen-off flag, so the touch indev always
 * knows about it: while off the pointer reports RELEASED and, after waking,
 * keeps doing so until the finger lifts (the wake touch must not also click). */
static void _set_screen_off(bool off)
{
    s_screen_off = off;
    lv_port_indev_set_screen_off(off);
}

static bool _screen_transition_active()
{
    lv_disp_t* disp = lv_disp_get_default();
    return disp != nullptr && lv_disp_get_scr_prev(disp) != nullptr;
}

static const char* _screen_name(const lv_obj_t* screen)
{
    if (screen == ui_home)          return "home";
    if (screen == ui_apps_menu)     return "apps";
    if (screen == ui_settings)      return "settings";
    if (screen == ui_sd_card_files) return "sd-files";
    if (screen == ui_clock)         return "clock";
    if (screen == ui_manual)        return "manual";
    if (screen == ui_date_picker)   return "date-picker";
    if (screen == ui_time_picker)   return "time-picker";
    if (screen == ui_usb_msc)       return "usb-msc";
    if (screen == ui_wifi)          return "wifi";
    if (screen == ui_tabview)       return "tabview";
    if (screen == ui_t9_keyboard)  return "wifi-keyboard";
    return "other";
}

static const char* _navigation_target_name(const lv_obj_t* screen, mk_event_t event)
{
    if (screen == ui_home) {
        if (event == MK_EVT_JOY_LEFT)  return "apps";
        if (event == MK_EVT_JOY_RIGHT) return "settings";
        if (event == MK_EVT_JOY_UP)    return "clock";
        if (event == MK_EVT_JOY_DOWN)  return "sd-files";
    }
    if ((screen == ui_apps_menu && event == MK_EVT_JOY_RIGHT) ||
        (screen == ui_settings && (event == MK_EVT_JOY_LEFT || event == MK_EVT_BTN_B)) ||
        (screen == ui_sd_card_files && event == MK_EVT_JOY_UP) ||
        (screen == ui_clock && event == MK_EVT_JOY_DOWN) ||
        (screen == ui_tabview && event == MK_EVT_BTN_A)) {
        return "home";
    }
    if (screen == ui_manual && event == MK_EVT_BTN_B) return "clock";
    if ((screen == ui_usb_msc || screen == ui_wifi || screen == ui_tabview) &&
        event == MK_EVT_BTN_B) {
        return "settings";
    }
    if (screen == ui_t9_keyboard && event == MK_EVT_BTN_B &&
        !ui_t9_keyboard_is_connecting()) {
        return "wifi";
    }
    return nullptr;
}

/* ── LED state machine ──────────────────────────────────────────────
 *
 *  Priority (high → low):
 *    low battery (≤ WARN%)   → BLINK_SLOW RED   (persistent while discharging)
 *    charging                → BREATHING GREEN  (while VBUS + charge current)
 *    normal / boot           → OFF
 *
 *  Call _updateLed() once per second from the 1 Hz status bar tick.
 *  Call _device->led.update() every main-loop iteration to drive animation.
 *  setEffect() is called only on state transitions to avoid resetting phase.
 * ─────────────────────────────────────────────────────────────────── */
static WS2812B_Class::Effect s_led_cur = WS2812B_Class::OFF;

static void _updateLed(DEVICES* dev, int pct, bool charging)
{
    WS2812B_Class::Effect want;
    WS2812B_Class::Color  clr;
    float                 spd;

    if (!power_is_ready()) {
        /* Boot grace period: ADC not settled — only show charging, suppress low-bat blink. */
        want = charging ? WS2812B_Class::BREATHING : WS2812B_Class::OFF;
        clr  = WS2812B_Class::GREEN;
        spd  = 0.7f;
    } else if (pct <= POWER_WARN_BAT_PCT && !charging) {
        want = WS2812B_Class::BLINK_SLOW;
        clr  = WS2812B_Class::RED;
        spd  = 1.0f;
    } else if (charging) {
        want = WS2812B_Class::BREATHING;
        clr  = WS2812B_Class::GREEN;
        spd  = 0.7f;
    } else {
        want = WS2812B_Class::OFF;
        clr  = WS2812B_Class::WHITE;
        spd  = 1.0f;
    }

    if (want != s_led_cur) {
        s_led_cur = want;
        if (want == WS2812B_Class::OFF)
            dev->led.off();
        else
            dev->led.setEffect(want, clr, spd);
    }
}

/* Launcher instance for plain C callbacks (set in onCreate). */
static Launcher* s_launcher = nullptr;

/* Shutdown / auto-off animation: 4 × red blink, then LED off before powerOFF.
 * A running app is closed first so its onClose() runs (e.g. the voice
 * recorder finalises its WAV header); the close is bounded to a few seconds. */
static void _shutdown_led_anim(void)
{
    if (s_launcher) s_launcher->closeRunningApp();
    settings_flush();
    if (!s_shutdown_dev) return;
    s_led_cur = WS2812B_Class::OFF;   /* sync tracker so next boot starts clean */
    for (int i = 0; i < 4; i++) {
        s_shutdown_dev->led.setColor(WS2812B_Class::RED);
        delay(180);
        s_shutdown_dev->led.off();
        delay(180);
    }
}

/* Auto-dismiss timer: deletes the toast overlay after 4 s. */
static void _bat_toast_dismiss(lv_timer_t * t)
{
    lv_obj_t * toast = (lv_obj_t *)t->user_data;
    if (toast && lv_obj_is_valid(toast)) lv_obj_del_async(toast);
    /* timer is one-shot (repeat_count=1) — auto-deleted by LVGL after this call */
}

/* Low-battery toast raised while an app owned the screen: shown on return to the UI. */
static bool s_toast_pending = false;
static int  s_toast_pct     = 0;

static void _show_toast(const char * msg, uint32_t bg)
{
    lv_obj_t * scr = lv_scr_act();
    if (!scr) return;

    /* Toast banner — fixed at top of whatever screen is currently active */
    lv_obj_t * toast = lv_obj_create(scr);
    lv_obj_set_size(toast, 286, 36);
    lv_obj_set_pos(toast, 17, 6);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(toast,     lv_color_hex(bg),       LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(toast,       LV_OPA_COVER,           LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(toast, 0,                      LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(toast,       8,                      LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(toast, 0,                      LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl = lv_label_create(toast);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl,  &ui_font_name_14,       0);
    lv_obj_center(lbl);

    lv_timer_t * t = lv_timer_create(_bat_toast_dismiss, 4000, toast);
    if (!t) { lv_obj_del(toast); return; }
    lv_timer_set_repeat_count(t, 1);
}

static void _show_low_bat_toast(int pct)
{
    char msg[48];
    lv_snprintf(msg, sizeof(msg), "Low battery: %d%%  — please charge", pct);
    _show_toast(msg, 0xCC3300);
}

/* Called once when battery first drops to POWER_WARN_BAT_PCT (while discharging).
 * LED blink is already driven by _updateLed(); this adds an on-screen toast banner. */
static void _low_bat_warn_cb(int pct)
{
    Serial.printf("[Launcher] Low battery: %d%% — please charge soon\n", pct);
    mk_event_push(MK_EVT_POWER_LOW);  /* notify any app that is listening */

    /* LVGL is paused while an app runs: don't create objects now, remember the
     * warning and show it when the user is back in the launcher UI. */
    if (s_in_app) { s_toast_pending = true; s_toast_pct = pct; return; }

    _show_low_bat_toast(pct);
}

Launcher::Launcher(DEVICES* device) : _device(device) {}

/* ── onCreate ──────────────────────────────────────── */

void Launcher::onCreate()
{
    Serial.println("\n[Launcher] === onCreate ===");
    Serial.printf("[Launcher] Free heap: %lu  PSRAM free: %lu\n",
                  (unsigned long)esp_get_free_heap_size(),
                  (unsigned long)ESP.getFreePsram());

    /* NVS init — wrapped in settings layer so we don't expose persist.h here */
    /* Event bus — must be ready before any button/power event can be pushed */
    mk_events_init();

    /* Power management — sets hardware cutoff voltage + PEK timing */
    power_init(&_device->pmu);
    s_shutdown_dev = _device;
    s_launcher = this;
    power_set_pre_shutdown_cb(_shutdown_led_anim);
    power_set_warn_cb(_low_bat_warn_cb);

    Serial.println("[Launcher] [1/5] initSD...");
    initSD();
/* Expose RTC to generated C UI screens (clock / pickers) */
    ui_rtc_bridge_register(&_device->rtc);

    
    Serial.println("[Launcher] [2/5] installApps...");
    installApps();

    Serial.println("[Launcher] [3/5] initLVGL...");
    initLVGL();

    Serial.println("[Launcher] [4/5] buildUI...");
    buildUI();

    /* First boot: show home screen */
    Serial.println("[Launcher] [5/5] Loading home screen...");
    if (ui_home) {
        lv_disp_load_scr(ui_home);
        updateStatusBar();   /* prime SD icon + battery label */
        lv_timer_handler();  /* flush first frame to framebuffer while still dark */

        /* Fade in: black → home screen. Brightness was zeroed by SplashScreen. */
        const uint8_t target_bright =
            (uint8_t)(sys_get_brightness() * 255 / 100);
        for (int b = 0; b <= (int)target_bright; b += 6) {
            _device->Lcd.setBrightness((uint8_t)b);
            delay(10);
        }
        _device->Lcd.setBrightness(target_bright);
    }

    /* Ensure LED is off when UI is ready.
     * devices.cpp leaves LED in OFF after boot blinks; this guards against
     * any future change to the boot sequence leaving an active effect. */
    s_led_cur = WS2812B_Class::OFF;
    _device->led.off();

    Serial.printf("[Launcher] Ready — %d apps, heap=%lu\n\n",
                  (int)_mooncake.getAppNum(),
                  (unsigned long)esp_get_free_heap_size());
}

/* ── onLoop ────────────────────────────────────────── */

void Launcher::onLoop()
{
    /* A timed voice-recorder close can outlive Mooncake's app state.  Service
       the persistent recorder instance before allowing any new app to claim
       the shared audio pins. */
    MOONCAKE::APPS::AppVoiceRecorder::serviceDeferredClose(_device);
    MOONCAKE::APPS::App03::serviceDeferredClose(_device);
    if (_personal_drain && personal_apps::cleanupPending()) {
        _mooncake.update();
    } else if (_personal_drain) {
        if (_personal_handle >= 0) _mooncake.uninstallApp(_personal_handle);
        _mooncake.update(); // finish destruction before any new install can reuse the ID
        _personal_handle = -1; _personal_id.clear(); _personal_drain = false;
    }

    /* Flush dirty settings to NVS after debounce — ~0 cost when clean */
    settings_tick();

    // onCreate renders the home screen before onLoop begins. Wait for a
    // few seconds of normal loop operation, even if an app is opened early.
    static uint32_t healthySince = 0;
    static bool otaChecked = false;
    if (!otaChecked && _ui_ready && ui_home && lv_disp_get_default() && lv_scr_act()) {
        const uint32_t now = millis();
        if (healthySince == 0) healthySince = now;
        if (now - healthySince >= 3000) {
            ota_updater::confirmHealthyBoot();
            otaChecked = true;
        }
    }

    if (!_app_running) {
        /* ── GUI state: LVGL active ── */
        /* Drive LED animation (BREATHING / BLINK) — must tick every loop. */
        _device->led.update();

        /* Poll physical buttons every loop so press edges aren't missed. */
        _device->button.update();
        _update_system_key_sound(_device);
        _device->button.tick();
        handlePhysicalNav();

        uint32_t next_ms = lv_timer_handler();
        handleAppSelection();
        if (ui_apps_manager && lv_scr_act() == ui_apps_manager) handleAppManager();

        /* Detect touchscreen activity → reset sleep timer + restore screen.
         * Uses the raw panel state: while the screen is off the LVGL pointer
         * reports RELEASED (see lv_port_indev.cpp), so the wake touch only
         * wakes the screen and never reaches a widget. */
        if (lv_port_indev_touch_active()) {
            power_reset_sleep_timer();
            if (s_screen_off) {
                _device->Lcd.setBrightness(
                    (uint8_t)(sys_get_brightness() * 255 / 100));
                _set_screen_off(false);
            }
        }

        /* Status bar + power tick ~1 Hz */
        static uint32_t lastSt = 0;
        if (millis() - lastSt > 1000) {
            lastSt = millis();
            power_tick();
            updateStatusBar();
            /* LED state: only active after boot grace; setEffect called on change only */
            _updateLed(_device, power_battery_pct(), power_is_charging());

            /* Screen off (user-configured display timeout)
             * Guard: don't dim while USB MSC is transferring files. */
            uint32_t idle_s   = power_idle_seconds();
            int      disp_to  = settings_get_disp_timeout();  /* 0 = never */
            if (!s_screen_off && disp_to > 0 && idle_s >= (uint32_t)disp_to
                && !usb_msc_is_active()) {
                _device->Lcd.setBrightness(0);
                _set_screen_off(true);
                Serial.printf("[Launcher] Screen off (idle %lus)\n",
                              (unsigned long)idle_s);
            }

            /* Auto power-off after extended idle (2.5× display timeout, min 5 min).
             * Guard: never power off while charging or while MSC session is live. */
            uint32_t off_threshold = (disp_to > 0)
                                     ? (uint32_t)disp_to * 5 / 2
                                     : 300u;
            if (off_threshold < 300u) off_threshold = 300u;
            if (idle_s >= off_threshold
                && !power_is_charging()
                && !power_vbus_present()
                && !usb_msc_is_active()) {
                Serial.printf("[Launcher] Idle %lus — auto power-off\n",
                              (unsigned long)idle_s);
                power_shutdown();
                /* does not return */
            }
        }

        /* Yield CPU for the time LVGL says it doesn't need us.
         * Minimum 1ms to let FreeRTOS IDLE task run (feeds watchdog). */
        if (next_ms > 5) next_ms = 5;
        if (next_ms < 1) next_ms = 1;
        delay(next_ms);
    } else {
        /* ── APP state: LVGL disabled, only mooncake + buttons ── */
        /* Drive LED animation while an app is running. */
        _device->led.update();
        /* Keep sleep timer alive while an app is open — user is actively using device. */
        power_reset_sleep_timer();
        _device->button.update(); /* read GPIO (apps use LVGL indev instead) */
        _update_system_key_sound(_device);
        _device->button.tick();   /* must tick for long-press B detection */

        /* A still-held B (e.g. the press that opened this app) must not close
         * it on the first loop: long-press B is accepted only after B has been
         * seen released since the app opened. */
        if (_device->button.B.state() == Button_Class::RELEASED) {
            _b_released_since_open = true;
        }

        /* Long-press B → exit app, return to persistent UI */
        if (_b_released_since_open && _device->button.B.isLongPress()) {
            Serial.printf("[Launcher] Long-press B — closing app %d\n",
                          _running_app_id);

            closeRunningApp();

            /* 3. Return to persistent UI (no rebuild needed) */
            returnToUI();
            return;
        }

        /* Power/battery handling must keep running inside apps (~1 Hz). */
        static uint32_t lastAppSt = 0;
        if (millis() - lastAppSt > 1000) {
            lastAppSt = millis();
            power_tick();
            _updateLed(_device, power_battery_pct(), power_is_charging());
        }

        _mooncake.update();
        if (_personal_handle >= 0 && _mooncake.getAppCurrentState(_personal_handle) == mooncake::AppAbility::StateSleeping) {
            _app_running = false; _running_app_id = -1; s_in_app = false;
            _personal_drain = true;
            returnToUI();
        } else if (_app_running && _running_app_id >= 0 && _running_app_id != _personal_handle) {
            /* A built-in app may close itself. openApp() sets StateGoOpen
             * synchronously, so a freshly opened app is never GoClose/Sleeping
             * here, and closeRunningApp() clears _app_running before it returns,
             * so this cannot fire mid-open or during shutdown. */
            const auto st = _mooncake.getAppCurrentState(_running_app_id);
            if (st == mooncake::AppAbility::StateGoClose) {
                /* onClose() runs on the next update and may delete the app's
                 * screen: make the persistent menu active first, as
                 * closeRunningApp() does. */
                if (ui_apps_menu && lv_scr_act() != ui_apps_menu) {
                    lv_disp_load_scr(ui_apps_menu);
                    lv_obj_invalidate(ui_apps_menu);
                    lv_timer_handler();
                }
            } else if (st == mooncake::AppAbility::StateSleeping) {
                /* onClose() has run: leave without calling closeApp() again. */
                Serial.printf("[Launcher] App %d closed itself — returning to UI\n", _running_app_id);
                _app_running = false; _running_app_id = -1; s_in_app = false;
                returnToUI();
            }
        }
    }
}

/* ── SD card ───────────────────────────────────────── */

void Launcher::initSD()
{
    Serial.println("[Launcher] Mounting SD (SDMMC 1-bit)...");

    /* Pin assignment */
    if (!SD_MMC.setPins(HAL_PIN_SD_CLK, HAL_PIN_SD_CMD, HAL_PIN_SD_D0)) {
        Serial.println("[Launcher] SD pin config failed");
        _sd_ready = false;
        return;
    }

    /* Try mounting — single attempt at safe speed, max ~5s timeout */
    Serial.printf("[Launcher] SD attempt @ 10MHz (1-bit)...\n");
    bool mounted = SD_MMC.begin("/sdcard", true, false, 10000);

    if (!mounted || SD_MMC.cardType() == CARD_NONE) {
        ui_sd_classify_failed_mount();   /* SD_MMC.end() + Missing/Unsupported */
        Serial.println("[Launcher] SD mount failed — running without SD");
        _sd_ready = false;
        return;
    }

    uint64_t sizeMB = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("[Launcher] SD OK — %lluMB  Total: %lluMB  Used: %lluMB\n",
                  (unsigned long long)sizeMB,
                  (unsigned long long)(SD_MMC.totalBytes() / (1024 * 1024)),
                  (unsigned long long)(SD_MMC.usedBytes()  / (1024 * 1024)));
    _sd_ready = true;
}

/* ── Install apps ──────────────────────────────────── */

void Launcher::installApps()
{
    Serial.println("[Launcher] Installing native apps...");
    _native_handles.clear();
    _native_handles.reserve(APP_REGISTRY_COUNT);
    for (const AppReg &r : APP_REGISTRY) {
        const int handle = _mooncake.installApp(r.make(_device));
        _native_handles.push_back(handle); /* never infer handles from display names */
    }
    auto scan = personal_apps::scan();
    _packages = std::move(scan.packages);
    _packages.erase(std::remove_if(_packages.begin(), _packages.end(), [](const auto &p) {
        for (int i=0;i<APP_REGISTRY_COUNT;i++) if (p.id == APP_REGISTRY[i].id) return true;
        return false;
    }), _packages.end());
}

/* ── LVGL one-time init ────────────────────────────── */

void Launcher::initLVGL()
{
    if (_lvgl_inited) return;

    Serial.println("[Launcher] LVGL init (one-time)...");
    lv_init();
    lv_port_disp_init(&_device->Lcd);
    lv_port_indev_init(&_device->ctp);

    if (_sd_ready) {
        lv_fs_fatfs_init();
    }

    _lvgl_inited = true;
}

/* ── Build UI (create screens + menu + icons) ──────── */

void Launcher::buildUI()
{
    Serial.printf("[Launcher] Building UI... free heap=%lu, stack HWM=%u\n",
                  (unsigned long)esp_get_free_heap_size(),
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));

    /* ui_init() creates home + apps_menu screens (does NOT load any screen) */
    ui_init();

    /* WiFi bridge: STA mode + auto-reconnect of last credentials.
     * Safe to call once here; ui_wifi screen uses it for scan/connect. */
    ui_wifi_bridge_init();

    /* Populate menu grid */
    loadAppsMenu();

    _ui_ready = true;
}

std::vector<std::string> Launcher::catalogIds() const
{
    std::vector<std::string> ids;
    for (const auto& native : APP_REGISTRY) ids.emplace_back(native.id);
    for (const auto& package : _packages) {
        if (std::find(ids.begin(), ids.end(), package.id) == ids.end()) ids.push_back(package.id);
    }
    return ids;
}

void Launcher::renderAppManager()
{
    ui_apps_manager_screen_init();
    const auto catalog = app_preferences::load();
    static AppManagerEntry_t rows[APPS_MENU_MAX_APPS];
    memset(rows, 0, sizeof(rows));
    int count = 0;
    for (const auto& id : catalog.sorted(catalogIds())) {
        if (count >= APPS_MENU_MAX_APPS) break;
        auto& row = rows[count++];
        strncpy(row.id, id.c_str(), sizeof(row.id) - 1);
        row.installed = true;
        row.visible = catalog.visible(id);
        bool native = false;
        for (const auto& entry : APP_REGISTRY) if (id == entry.id) {
            strncpy(row.name, entry.name, sizeof(row.name) - 1);
            strncpy(row.version, entry.version, sizeof(row.version) - 1);
            strncpy(row.description, entry.description, sizeof(row.description) - 1);
            strncpy(row.prerequisites, entry.prerequisites, sizeof(row.prerequisites) - 1);
            row.compatible = true;
            native = true;
            break;
        }
        if (native) continue;
        for (const auto& package : _packages) if (id == package.id) {
            strncpy(row.name, package.name.empty() ? id.c_str() : package.name.c_str(), sizeof(row.name) - 1);
            strncpy(row.version, package.version.c_str(), sizeof(row.version) - 1);
            strncpy(row.description, package.error.empty() ? package.description.c_str() : package.error.c_str(), sizeof(row.description) - 1);
            strncpy(row.prerequisites, "SD card", sizeof(row.prerequisites) - 1);
            row.compatible = package.compatible;
            row.removable = true;
            break;
        }
    }
    ui_apps_manager_load(rows, count);
}

void Launcher::refreshAppManager()
{
    ui_apps_manager_screen_init();
    if (_app_running || _personal_drain || personal_apps::cleanupPending() ||
        MOONCAKE::APPS::AppVoiceRecorder::closePending() || MOONCAKE::APPS::App03::closePending()) {
        renderAppManager();
        ui_apps_manager_set_status("Previous app is still closing");
        return;
    }
    auto result = personal_apps::scan();
    // Keep known packages on transient card/USB errors; launch revalidates them.
    if (result.error.empty()) {
        _packages = std::move(result.packages);
        _packages.erase(std::remove_if(_packages.begin(), _packages.end(), [](const auto& p) {
            for (const auto& entry : APP_REGISTRY) if (p.id == entry.id) return true;
            return false;
        }), _packages.end());
    }
    renderAppManager();
    loadAppsMenu();
    ui_apps_manager_set_status(!result.error.empty() ? result.error.c_str() :
        (!result.warning.empty() ? result.warning.c_str() : "SD apps refreshed"));
}

void Launcher::handleAppManager()
{
    const std::string id = ui_apps_manager_focus_id();
    if (!_manager_confirm_id.empty() && _manager_confirm_id != id) {
        _manager_confirm_id.clear();
        _manager_confirm_action = -1;
        ui_apps_manager_set_status("");
    }
    const int action = ui_apps_manager_take_action();
    if (action < 0) return;
    const bool removing = action == UI_APPS_MANAGER_REMOVE || action == UI_APPS_MANAGER_REMOVE_DATA;
    if (!removing) { _manager_confirm_id.clear(); _manager_confirm_action = -1; }
    if (action == UI_APPS_MANAGER_BACK) {
        _ui_screen_change(&ui_apps_menu, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, &ui_apps_menu_screen_init);
        return;
    }
    if (_app_running || _personal_drain || personal_apps::cleanupPending() ||
        MOONCAKE::APPS::AppVoiceRecorder::closePending() || MOONCAKE::APPS::App03::closePending()) {
        ui_apps_manager_set_status("Previous app is still closing"); return;
    }
    if (action == UI_APPS_MANAGER_REFRESH) { refreshAppManager(); return; }
    if (id.empty()) return;
    auto catalog = app_preferences::load();
    const auto available = catalogIds();
    catalog.hidden.erase(std::remove_if(catalog.hidden.begin(), catalog.hidden.end(), [&](const auto& hidden) {
        return std::find(available.begin(), available.end(), hidden) == available.end();
    }), catalog.hidden.end());
    std::string error;
    if (action == UI_APPS_MANAGER_HIDE_SHOW) {
        if (id == "firmware-update") { ui_apps_manager_set_status("FW Update stays available"); return; }
        catalog.toggle(id);
        if (!app_preferences::save(catalog, error)) { ui_apps_manager_set_status(error.c_str()); return; }
        loadAppsMenu(); renderAppManager();
        ui_apps_manager_set_status(catalog.visible(id) ? "Shown in Apps" : "Hidden from Apps");
        return;
    }
    if (action == UI_APPS_MANAGER_MOVE_UP || action == UI_APPS_MANAGER_MOVE_DOWN) {
        catalog.order = catalog.sorted(catalogIds());
        auto current = std::find(catalog.order.begin(), catalog.order.end(), id);
        if (current == catalog.order.end()) return;
        if (action == UI_APPS_MANAGER_MOVE_UP && current != catalog.order.begin()) std::iter_swap(current, current - 1);
        if (action == UI_APPS_MANAGER_MOVE_DOWN && current + 1 != catalog.order.end()) std::iter_swap(current, current + 1);
        if (!app_preferences::save(catalog, error)) { ui_apps_manager_set_status(error.c_str()); return; }
        loadAppsMenu(); renderAppManager(); ui_apps_manager_set_status("Order saved"); return;
    }
    if (!removing) return;
    auto package = std::find_if(_packages.begin(), _packages.end(), [&](const auto& p) { return p.id == id; });
    if (package == _packages.end()) { ui_apps_manager_set_status("Built-in app: use Hide"); return; }
    if (_manager_confirm_id != id || _manager_confirm_action != action) {
        _manager_confirm_id = id; _manager_confirm_action = action;
        ui_apps_manager_set_status(action == UI_APPS_MANAGER_REMOVE_DATA ? "Again: remove app AND data" : "Again: remove app; keep data");
        return;
    }
    const bool removed = personal_apps::remove(*package, action == UI_APPS_MANAGER_REMOVE_DATA, error);
    _manager_confirm_id.clear(); _manager_confirm_action = -1;
    refreshAppManager(); // Partial removals must no longer appear runnable.
    ui_apps_manager_set_status(removed ? "Personal app removed" : error.c_str());
}

/* ── Close running app (long-press B and critical shutdown) ── */

void Launcher::closeRunningApp()
{
    if (!_app_running) return;
    const uint32_t t0 = millis();

    /* An LVGL app owns its root screen. Its onClose() may delete
     * it, so make the persistent menu active before cleanup.
     * Deleting the active LVGL root screen can corrupt LVGL state
     * and trigger a watchdog reset. */
    if (ui_apps_menu && lv_scr_act() != ui_apps_menu) {
        lv_disp_load_scr(ui_apps_menu);
        lv_obj_invalidate(ui_apps_menu);
        lv_timer_handler();
    }

    /* Request close (StateGoClose), then drive the state machine so
     * onClose() actually executes. onClose() paths are individually bounded
     * (capture-task waits <= ~1 s); the elapsed time is logged. */
    _mooncake.closeApp(_running_app_id);
    _mooncake.update();

    _running_app_id = -1;
    _app_running    = false;
    s_in_app        = false;
    if (_personal_handle >= 0) _personal_drain = true;
    Serial.printf("[Launcher] App closed in %lu ms\n", (unsigned long)(millis() - t0));
}

/* ── Return to UI after app exit ───────────────────── */

void Launcher::returnToUI()
{
    Serial.println("[Launcher] Returning to persistent UI...");

    /* Restore backlight if an app left the screen off */
    if (s_screen_off) {
        _device->Lcd.setBrightness((uint8_t)(sys_get_brightness() * 255 / 100));
        _set_screen_off(false);
    }

    /* Drop button edges that arrived while the app owned the input, so they
     * are not replayed as navigation on the UI screen. */
    _device->button.A.pressed();
    _device->button.B.pressed();
    _device->button.Up.pressed();
    _device->button.Down.pressed();
    _device->button.Left.pressed();
    _device->button.Right.pressed();

    /* Force LVGL full redraw — app may have written directly to LCD */
    if (ui_apps_menu) {
        lv_disp_load_scr(ui_apps_menu);
        lv_obj_invalidate(ui_apps_menu);
    }
    lv_timer_handler();

    /* Low-battery warning that fired while the app was open */
    if (s_toast_pending) {
        s_toast_pending = false;
        if (power_is_low_battery()) _show_low_bat_toast(s_toast_pct);
    }

    Serial.println("[Launcher] Back to apps_menu (no rebuild)");
}

/* ── Apps menu ─────────────────────────────────────── */

void Launcher::loadAppsMenu()
{
    const auto catalog = app_preferences::load();
    static AppMenuEntry_t entries[APPS_MENU_MAX_APPS];
    memset(entries, 0, sizeof(entries));
    int count = 0;
    for (const auto& id : catalog.sorted(catalogIds())) {
        if (count >= APPS_MENU_MAX_APPS || !catalog.visible(id)) continue;
        const char* name = nullptr;
        const void* icon = nullptr;
        for (const auto& entry : APP_REGISTRY) if (id == entry.id) { name = entry.name; icon = entry.icon; break; }
        if (!name) for (const auto& package : _packages) if (id == package.id && package.compatible) { name = package.name.c_str(); break; }
        if (!name) continue;
        auto& entry = entries[count++];
        strncpy(entry.id, id.c_str(), sizeof(entry.id) - 1);
        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.icon = icon;
    }
    ui_apps_menu_load_apps(entries, count);
}

/* ── Status bar ────────────────────────────────────── */

void Launcher::updateStatusBar()
{
    if (!_ui_ready) return;

    /* ── Battery: label ("NN/100") + 5-cell unit indicator + color ── */
    {
        int pct = power_battery_pct();

        if (ui_soc) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d/100", pct);
            lv_label_set_text(ui_soc, buf);
            lv_obj_set_style_text_color(ui_soc, lv_color_white(), 0);
        }

        /* Each unit covers 20 pp; show a cell when the level meets its floor.
         * unit1 = 1-20%, unit2 = 21-40%, unit3 = 41-60%, unit4 = 61-80%, unit5 = 81-100% */
        auto setUnit = [](lv_obj_t* obj, bool show) {
            if (!obj) return;
            if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
            else      lv_obj_add_flag  (obj, LV_OBJ_FLAG_HIDDEN);
        };
        setUnit(ui_unit1, pct >= 1);
        setUnit(ui_unit2, pct >  20);
        setUnit(ui_unit3, pct >  40);
        setUnit(ui_unit4, pct >  60);
        setUnit(ui_unit5, pct >  80);
    }

    /* ── SD card hot-plug: detect changes and push events ── */
    {
        bool sd_present = (ui_sd_present() != 0);
        if (sd_present != _sd_ready) {
            mk_event_push(sd_present ? MK_EVT_SD_INSERTED : MK_EVT_SD_REMOVED);
        }
        _sd_ready = sd_present;
        if (ui_sd_on && ui_sd_null) {
            if (sd_present) {
                lv_obj_clear_flag(ui_sd_on,  LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(  ui_sd_null, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(  ui_sd_on,  LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui_sd_null, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* ── App selection (GUI → APP transition) ──────────── */

void Launcher::handleAppSelection()
{
    const char* selId = ui_apps_menu_get_selected_id();
    if (_personal_drain || personal_apps::cleanupPending() || MOONCAKE::APPS::AppVoiceRecorder::closePending() || MOONCAKE::APPS::App03::closePending()) {
        /* Selection is kept so the launch proceeds once the close finishes;
         * tell the user why nothing happens (at most every 3 s). */
        static uint32_t s_last_closing_toast = 0;
        const uint32_t now = millis();
        if (selId && selId[0] != '\0' && (s_last_closing_toast == 0 || now - s_last_closing_toast > 3000)) {
            s_last_closing_toast = now;
            _show_toast("Previous app still closing", 0x555555);
        }
        return;
    }
    if (!selId || selId[0] == '\0') return;

    Serial.printf("[Launcher] Selected: %s\n", selId);
    if (strcmp(selId, "__manager") == 0) {
        ui_apps_menu_clear_selected(); refreshAppManager();
        _ui_screen_change(&ui_apps_manager, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, &ui_apps_manager_screen_init);
        return;
    }

    /* Resolve by immutable catalog id, then use the captured install handle. */
    for (int i = 0; i < APP_REGISTRY_COUNT; i++) {
        if (strcmp(APP_REGISTRY[i].id, selId) == 0 && i < (int)_native_handles.size()) {
            /* Enter app — LVGL screens stay in memory, just pause rendering */
            _running_app_id = _native_handles[i];
            if (!_mooncake.openApp(_running_app_id)) {
                _running_app_id = -1;
                ui_apps_menu_clear_selected();
                _show_toast("Could not open app", 0x555555);
                return;
            }
            _app_running = true;
            s_in_app     = true;
            _b_released_since_open = false;   /* long-press B armed only after a release */

            ui_apps_menu_clear_selected();
            Serial.printf("[Launcher] Opened app id=%d (LVGL paused)\n", i);
            return;
        }
    }
    for (const auto &package : _packages) if (package.id == selId) {
        if (!package.compatible || _personal_drain) { ui_apps_menu_clear_selected(); return; }
        auto app = personal_apps::makeApp(_device, package);
        if (!app) { ui_apps_menu_clear_selected(); _show_toast("App unavailable. Refresh in Manage", 0x555555); return; }
        _personal_handle = _mooncake.installApp(std::move(app)); _personal_id = package.id;
        if (_personal_handle < 0 || !_mooncake.openApp(_personal_handle)) { if (_personal_handle >= 0) { _mooncake.uninstallApp(_personal_handle); _mooncake.update(); } _personal_handle=-1; _personal_id.clear(); ui_apps_menu_clear_selected(); return; }
        _running_app_id=_personal_handle; _app_running=true; s_in_app=true; _b_released_since_open=false; ui_apps_menu_clear_selected(); return;
    }

    /* Not found — clear selection */
    ui_apps_menu_clear_selected();
}

/* ── Physical button navigation (GUI state) ───────── */
/*
 *  Global rules:
 *    A button  → always returns to ui_home (one-touch Home).
 *    B button  → on screens that display the bottom-right key-prompts
 *                strip, returns to the parent level (capped at the
 *                2nd-level Home cross: apps_menu / clock / sd_card_files
 *                / settings). Specifically:
 *                    ui_wifi   / ui_tabview / ui_usb_msc → ui_settings
 *                    ui_t9_keyboard                      → ui_wifi
 *                    ui_date_picker / ui_time_picker     → ui_tabview
 *                On ui_sd_card_files B still pops one folder level
 *                (no prompts shown, kept for browsing convenience).
 *    Joystick  → drives the home-centred cross navigation; direction
 *                matches the destination (joystick points to where you go).
 *
 *                Cross layout (HOME at centre):
 *                              [Clock]  ← UP
 *                  [Apps] ← [HOME] → [Settings]
 *                             [SD]  ← DOWN
 *
 *                Joystick on HOME (animation mirrors touch-swipe, 500 ms):
 *                  LEFT  → ui_apps_menu     (MOVE_RIGHT)
 *                  RIGHT → ui_settings      (MOVE_LEFT)
 *                  UP    → ui_clock         (MOVE_BOTTOM)
 *                  DOWN  → ui_sd_card_files (MOVE_TOP)
 *
 *                Return to Home (inverse key, inverse animation):
 *                  apps_menu  + RIGHT → Home (MOVE_LEFT)
 *                  settings   + LEFT  → Home (MOVE_RIGHT)
 *                  clock      + DOWN  → Home (MOVE_TOP)
 *                  sd_files   + UP    → Home (MOVE_BOTTOM)
 *
 *    Pickers (ui_date_picker / ui_time_picker)
 *                A → commit Save then return to Home.
 */
/* ── handlePhysicalNav: detect button edges → drive navigation ──────────
 *
 * Physical inputs are dispatched immediately. They must not share the
 * system event queue because delayed inputs can be applied to a later screen.
 * ──────────────────────────────────────────────────────────────────────── */
void Launcher::handlePhysicalNav()
{
    const bool a     = _device->button.A.pressed();
    const bool b     = _device->button.B.pressed();
    const bool up    = _device->button.Up.pressed();
    const bool down  = _device->button.Down.pressed();
    const bool left  = _device->button.Left.pressed();
    const bool right = _device->button.Right.pressed();

    if (!a && !b && !up && !down && !left && !right) {
        if (!_screen_transition_active()) {
            processNavEvents();
        }
        return;
    }

    /* Any physical press resets idle timer and wakes the screen. The press
     * that wakes a dark screen only wakes it: it must not also act. */
    power_reset_sleep_timer();
    if (s_screen_off) {
        _device->Lcd.setBrightness((uint8_t)(sys_get_brightness() * 255 / 100));
        _set_screen_off(false);
        return;
    }

    if (_screen_transition_active()) {
        Serial.println("[Launcher] Navigation input ignored during screen transition");
        return;
    }

    /* A single physical action per loop prevents simultaneous inputs from
     * starting competing transitions. */
    if (a)          handleNavigationEvent(MK_EVT_BTN_A);
    else if (b)     handleNavigationEvent(MK_EVT_BTN_B);
    else if (up)    handleNavigationEvent(MK_EVT_JOY_UP);
    else if (down)  handleNavigationEvent(MK_EVT_JOY_DOWN);
    else if (left)  handleNavigationEvent(MK_EVT_JOY_LEFT);
    else if (right) handleNavigationEvent(MK_EVT_JOY_RIGHT);
}

/* ── processNavEvents: consume one external event ──────────────────────── */
void Launcher::processNavEvents()
{
    if (_screen_transition_active()) {
        return;
    }

    mk_event_t evt;
    if (!mk_event_pop(&evt)) {
        return;
    }

    handleNavigationEvent(evt);
}

/* ── handleNavigationEvent: drive screen transitions ───────────────────── */
void Launcher::handleNavigationEvent(mk_event_t evt)
{
    lv_obj_t* cur = lv_scr_act();
    if (!cur) {
        return;
    }

    const bool a     = (evt == MK_EVT_BTN_A);
    const bool b     = (evt == MK_EVT_BTN_B);
    const bool up    = (evt == MK_EVT_JOY_UP);
    const bool down  = (evt == MK_EVT_JOY_DOWN);
    const bool left  = (evt == MK_EVT_JOY_LEFT);
    const bool right = (evt == MK_EVT_JOY_RIGHT);

    const char* target = _navigation_target_name(cur, evt);
    if (target) {
        Serial.printf("[Launcher] Navigation %s -> %s (event=0x%02X, heap=%lu)\n",
                      _screen_name(cur), target, (unsigned)evt,
                      (unsigned long)esp_get_free_heap_size());
    }

    /* ── Home: joystick → cross navigation ── */
    if (cur == ui_home) {
        if (left)       _ui_screen_change(&ui_apps_menu,     LV_SCR_LOAD_ANIM_MOVE_RIGHT,  500, 0, &ui_apps_menu_screen_init);
        else if (right) _ui_screen_change(&ui_settings,      LV_SCR_LOAD_ANIM_MOVE_LEFT,   500, 0, &ui_settings_screen_init);
        else if (up)    _ui_screen_change(&ui_clock,         LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, &ui_clock_screen_init);
        else if (down)  _ui_screen_change(&ui_sd_card_files, LV_SCR_LOAD_ANIM_MOVE_TOP,    500, 0, &ui_sd_card_files_screen_init);
        return;
    }

    /* ── Cross sub-screens: reverse joystick → Home ── */
    if (cur == ui_apps_menu) {
        if (a)    { ui_apps_menu_activate_focused(); return; }
        if (left) { refreshAppManager(); _ui_screen_change(&ui_apps_manager, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, &ui_apps_manager_screen_init); return; }
        if (right || b) { _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_MOVE_LEFT,   500, 0, &ui_home_screen_init); return; }
        if (up)    { ui_apps_menu_focus_step(-1); return; }
        if (down)  { ui_apps_menu_focus_step(1);  return; }
    } else if (cur == ui_apps_manager) {
        if (b) { _manager_confirm_id.clear(); _manager_confirm_action = -1; _ui_screen_change(&ui_apps_menu, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, &ui_apps_menu_screen_init); return; }
        if (up) { ui_apps_manager_focus_step(-1); return; }
        if (down) { ui_apps_manager_focus_step(1); return; }
        if (left) { ui_apps_manager_action_step(-1); return; }
        if (right) { ui_apps_manager_action_step(1); return; }
        if (a) { ui_apps_manager_activate_focused(); return; }
    } else if (cur == ui_settings) {
        if (ui_settings_info_dialog_open()) {
            /* Info dialog is up: A or B closes it; nothing acts on the screen behind. */
            if (a || b) ui_settings_info_dialog_close();
            return;
        }
        if (b)     { _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT,  500, 0, &ui_home_screen_init); return; }
        if (a)     { ui_settings_activate_focused(); return; }
        if (left)  {
            if (!ui_settings_focus_step(-1, 0))
                _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT,  500, 0, &ui_home_screen_init);
            return;
        }
        if (right) { ui_settings_focus_step(1, 0);  return; }
        if (up)    { ui_settings_focus_step(0, -1); return; }
        if (down)  { ui_settings_focus_step(0, 1);  return; }
    } else if (cur == ui_sd_card_files) {
        if (up)    { _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, &ui_home_screen_init); return; }
        if (b)     { ui_sd_card_files_action_back(); return; }
        if (a)     { ui_sd_card_files_action_refresh(); return; }
        if (left)  { ui_sd_card_files_action_previous_page(); return; }
        if (right) { ui_sd_card_files_action_next_page(); return; }
    } else if (cur == ui_clock) {
        if (a)     { ui_clock_open_editor(); return; }
        if (down)  { _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_MOVE_TOP,    500, 0, &ui_home_screen_init); return; }
    } else if (cur == ui_manual) {
        if (a)     { ui_manual_enter_focused(); return; }
        if (b)     { _ui_screen_change(&ui_clock, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_clock_screen_init); return; }
        if (left)  { ui_manual_set_focus(0); return; }
        if (right) { ui_manual_set_focus(1); return; }
        return;
    }

    /* ── Pickers ── */
    if (cur == ui_date_picker) {
        if (a) ui_date_picker_action_save();
        if (b) ui_date_picker_action_cancel();
        return;
    }
    if (cur == ui_time_picker) {
        if (a) ui_time_picker_action_save();
        if (b) ui_time_picker_action_cancel();
        return;
    }

    /* ── USB MSC screen ── */
    if (cur == ui_usb_msc) {
        if (a || b) {
            ui_usb_msc_set_notice(NULL);   /* new press: drop any sticky message */
            if (!usb_msc_is_quiescent()) {
                /* Host is still writing: releasing now could corrupt the card. */
                ui_usb_msc_set_notice("Eject on computer first");
                return;
            }
        }
        if (a) {
            if (usb_msc_is_active()) {
                int ok = usb_manager_release(USB_MODE_MSC);
                if (ok) {
                    if (ui_usb_msc_label) lv_label_set_text(ui_usb_msc_label, "USB storage detached - eject host first");
                    Serial.println("[Launcher] MSC ejected via A");
                } else if (ui_usb_msc_label) {
                    lv_label_set_text(ui_usb_msc_label, "disconnecting - press A/B to retry");
                }
            } else {
                if (ui_usb_msc_label) lv_label_set_text(ui_usb_msc_label, "Already ejected");
            }
            return;
        }
        if (b) {
            int ok = usb_manager_release(USB_MODE_MSC);
            if (ok) {
                _ui_screen_change(&ui_settings, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_settings_screen_init);
            } else if (ui_usb_msc_label) {
                lv_label_set_text(ui_usb_msc_label, "disconnecting - press A/B to retry");
            }
            return;
        }
    }
    if (cur == ui_wifi) {
        if (a) { ui_wifi_connect_focused(); return; }
        if (b) { if (ui_wifi_handle_back()) return; ui_wifi_go_back(); return; }
        if (up) { ui_wifi_focus_step(-1); return; }
        if (down) { ui_wifi_focus_step(1); return; }
        if (right) { ui_wifi_open_options(); return; }
        return;
    }
    if (cur == ui_tabview) {
        if (ui_tabview_reset_dialog_open()) {
            /* Factory Reset confirmation is up: B cancels it; A and the joystick
             * must not navigate away (or change tabs) behind an armed dialog. */
            if (b) ui_tabview_reset_dialog_cancel();
            return;
        }
        if (a)    { _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, &ui_home_screen_init); return; }
        if (b)    { _ui_screen_change(&ui_settings, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_settings_screen_init); return; }
        if (up)   { uint16_t t = lv_tabview_get_tab_act(ui_tabview_settings); if (t > 0) lv_tabview_set_act(ui_tabview_settings, t-1, LV_ANIM_ON); return; }
        if (down) { uint16_t t = lv_tabview_get_tab_act(ui_tabview_settings); if (t < 4) lv_tabview_set_act(ui_tabview_settings, t+1, LV_ANIM_ON); return; }
        return;
    }
    if (cur == ui_t9_keyboard) {
        if (a || up || down || left || right) {
            if (ui_t9_keyboard_is_connecting()) return;
            if (a)          ui_t9_keyboard_press_focused();
            else if (up)    ui_t9_keyboard_move_focus(0, -1);
            else if (down)  ui_t9_keyboard_move_focus(0, 1);
            else if (left)  ui_t9_keyboard_move_focus(-1, 0);
            else            ui_t9_keyboard_move_focus(1, 0);
            return;
        }
        if (b) {
            if (ui_t9_keyboard_is_connecting()) {
                ui_t9_keyboard_cancel_connect();
            } else {
                ui_t9_keyboard_cleanup();
                _ui_screen_change(&ui_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_wifi_screen_init);
            }
            return;
        }
        if (!a) {
            return;
        }
    }

    /* ── Fallback: A → Home ── */
    if (a && cur != ui_apps_menu && cur != ui_settings &&
             cur != ui_sd_card_files && cur != ui_clock) {
        Serial.printf("[Launcher] Navigation %s -> home (event=0x%02X, heap=%lu)\n",
                      _screen_name(cur), (unsigned)evt,
                      (unsigned long)esp_get_free_heap_size());
        _ui_screen_change(&ui_home, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_home_screen_init);
    }
}

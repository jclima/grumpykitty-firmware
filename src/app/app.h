/**
 * @file app.h
 * @brief Native app registry — one table drives installation and the menu icon.
 *
 * ADDING AN APP:
 *   1. Create src/app/app_NN/ (see app_09/infrared.{h,cpp} as template).
 *      Set a unique name (<=31 chars) in the constructor; it is the menu id
 *      and the label shown in the apps menu.
 *   2. #include the header below and add ONE line to APP_REGISTRY[]:
 *          { &make<MOONCAKE::APPS::YourApp>, &ui_img_your_png },
 *      Order in the table = menu slot order.
 *   3. If it has a new icon: LV_IMG_DECLARE it in src/ui/ui.h and add the
 *      image .c file (see tools/gen_app_icon.py).
 *   The menu grid scrolls and holds up to APPS_MENU_MAX_APPS (24) apps; a
 *   static_assert below fails the build if the table grows past that.
 *   Notes: onCreate runs on first open only; onClose is the only teardown, so
 *   onOpen must re-initialise state. onRunning must return quickly (long-press
 *   B is only checked between updates).
 */
#pragma once

/* ── BSP ── */
#include "../bsp/devices.h"

/* ── LVGL icon declarations ── */
#include "../ui/ui.h"

/* ── Active native app headers (app_01 ~ app_14) ── */
#include "app_01/dino.h"        /* Dino         */
#include "app_02/matrix_rain.h" /* Matrix Rain  */
#include "app_03/vu_meter.h"    /* VU Meter     */
#include "app_04/retro_tv.h"    /* Retro TV     */
#include "app_05/pc_monitor.h"  /* PC Monitor   */
#include "app_06/air_mouse.h"   /* Air Mouse    */
#include "app_07/ble_spam.h"    /* BLE Spam     */
#include "app_08/badusb.h"      /* Bad USB      */
#include "app_09/infrared.h"    /* Infrared     */
#include "app_10/media_remote.h"  /* Media Remote */
#include "app_11/slide_clicker.h" /* Slide Clicker */
#include "app_12/radio_scanner.h"  /* Radio Scanner */
#include "app_13/voice_recorder.h" /* Voice Recorder */
#include "app_14/boat_dash.h"      /* Boat Dash    */

#include <mooncake.h>
#include <memory>

/** One registry row: how to construct the app and which icon the menu shows. */
struct AppReg {
    std::unique_ptr<mooncake::AppAbility> (*make)(DEVICES*);
    const void* icon;
};

template <class T>
static std::unique_ptr<mooncake::AppAbility> make(DEVICES* dev)
{
    return std::make_unique<T>(dev);
}

/**
 * @brief App registry — order = menu slot index. Names live in each app's
 *        constructor (read back by the launcher via Mooncake).
 */
static const AppReg APP_REGISTRY[] = {
    { &make<MOONCAKE::APPS::App01>,      &ui_img_dino_png        }, /* app_01  Dino         */
    { &make<MOONCAKE::APPS::App02>,      &ui_img_matrix_rain_png }, /* app_02  Matrix Rain  */
    { &make<MOONCAKE::APPS::App03>,      &ui_img_vu_meter_png    }, /* app_03  VU Meter     */
    { &make<MOONCAKE::APPS::App04>,      &ui_img_retro_tv_png    }, /* app_04  Retro TV     */
    { &make<MOONCAKE::APPS::PCMonitor>,  &ui_img_pc_montior_png  }, /* app_05  PC Monitor   */
    { &make<MOONCAKE::APPS::App06>,      &ui_img_air_mouse_png   }, /* app_06  Air Mouse    */
    { &make<MOONCAKE::APPS::App07>,      &ui_img_ble_spam_png    }, /* app_07  BLE Spam     */
    { &make<MOONCAKE::APPS::AppBadUSB>,  &ui_img_badusb_png      }, /* app_08  Bad USB      */
    { &make<MOONCAKE::APPS::App09>,      &ui_img_infrared_png    }, /* app_09  Infrared     */
    { &make<MOONCAKE::APPS::AppMediaRemote>,  &ui_img_mediaremote_png  }, /* app_10  Media Remote */
    { &make<MOONCAKE::APPS::AppSlideClicker>, &ui_img_slideclicker_png }, /* app_11  Slide Clicker */
    { &make<MOONCAKE::APPS::AppRadioScanner>, &ui_img_radarscan_png    }, /* app_12  Radio Scanner */
    { &make<MOONCAKE::APPS::AppVoiceRecorder>, &ui_img_voicerec_png    }, /* app_13  Voice Recorder */
    { &make<MOONCAKE::APPS::AppBoatDash>,      &ui_img_boatdash_png    }, /* app_14  Boat Dash    */
};
static const int APP_REGISTRY_COUNT =
    (int)(sizeof(APP_REGISTRY) / sizeof(APP_REGISTRY[0]));
static_assert(sizeof(APP_REGISTRY) / sizeof(APP_REGISTRY[0]) <= APPS_MENU_MAX_APPS,
              "APP_REGISTRY has more apps than the apps menu can show");

/** @brief Register all apps into Mooncake (table order = menu slot index). */
inline void registerAllApps(mooncake::Mooncake& mc, DEVICES* dev)
{
    for (const AppReg& r : APP_REGISTRY) {
        mc.installApp(r.make(dev));
    }
}

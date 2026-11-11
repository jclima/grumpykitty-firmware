/** Stable native application catalog. Display names are deliberately not ids. */
#pragma once
#include "../bsp/devices.h"
#include "../ui/ui.h"
#include "app_02/matrix_rain.h"
#include "app_03/vu_meter.h"
#include "app_04/retro_tv.h"
#include "app_05/pc_monitor.h"
#include "app_06/air_mouse.h"
#include "app_07/ble_spam.h"
#include "app_08/badusb.h"
#include "app_09/infrared.h"
#include "app_10/media_remote.h"
#include "app_11/slide_clicker.h"
#include "app_12/radio_scanner.h"
#include "app_13/voice_recorder.h"
#include "app_15/firmware_update.h"
#include <mooncake.h>
#include <memory>

struct AppReg {
    const char *id; const char *name; const char *version;
    const char *description; const char *prerequisites;
    std::unique_ptr<mooncake::AppAbility> (*make)(DEVICES*);
    const void *icon; bool visibleByDefault;
};
template <class T> static std::unique_ptr<mooncake::AppAbility> make(DEVICES *dev) { return std::make_unique<T>(dev); }

/* Dino and BoatDash are intentionally absent. IDs are immutable storage keys. */
static const AppReg APP_REGISTRY[] = {
 {"matrix-rain","Matrix Rain","1.0","Digital rain animation","Display",&make<MOONCAKE::APPS::App02>,&ui_img_matrix_rain_png,true},
 {"vu-meter","VU Meter","1.0","Audio-reactive level meter","Microphone",&make<MOONCAKE::APPS::App03>,&ui_img_vu_meter_png,true},
 {"retro-tv","Retro TV","1.0","Receive a computer screen over Wi-Fi","Wi-Fi + sender",&make<MOONCAKE::APPS::App04>,&ui_img_retro_tv_png,true},
 {"pc-monitor","PC Monitor","1.0","Monitor a connected computer","USB",&make<MOONCAKE::APPS::PCMonitor>,&ui_img_pc_montior_png,true},
 {"air-mouse","Air Mouse","1.0","Motion-controlled HID mouse","IMU + Bluetooth",&make<MOONCAKE::APPS::App06>,&ui_img_air_mouse_png,true},
 {"ble-spam","BLE Spam","1.0","Bluetooth testing utility","Bluetooth",&make<MOONCAKE::APPS::App07>,&ui_img_ble_spam_png,true},
 {"bad-usb","Bad USB","1.0","USB/BLE HID scripts","SD + USB or Bluetooth",&make<MOONCAKE::APPS::AppBadUSB>,&ui_img_badusb_png,true},
 {"infrared","Infrared","1.0","Infrared remote control","IR hardware",&make<MOONCAKE::APPS::App09>,&ui_img_infrared_png,true},
 {"media-remote","Media Remote","1.0","Bluetooth media controls","Bluetooth",&make<MOONCAKE::APPS::AppMediaRemote>,&ui_img_mediaremote_png,true},
 {"slide-clicker","Slide Clicker","1.0","Presentation remote","Bluetooth",&make<MOONCAKE::APPS::AppSlideClicker>,&ui_img_slideclicker_png,true},
 {"radio-scanner","Radio Scanner","1.0","Scan nearby Wi-Fi and BLE devices","Wi-Fi + Bluetooth",&make<MOONCAKE::APPS::AppRadioScanner>,&ui_img_radarscan_png,true},
 {"voice-recorder","Voice Recorder","1.0","Record audio to the SD card","Microphone + SD",&make<MOONCAKE::APPS::AppVoiceRecorder>,&ui_img_voicerec_png,true},
 {"firmware-update","FW Update","1.0","Check and install firmware updates","Wi-Fi + power",&make<MOONCAKE::APPS::AppFirmwareUpdate>,&ui_img_detailed_settings_png,true},
};
static constexpr int APP_REGISTRY_COUNT = (int)(sizeof(APP_REGISTRY)/sizeof(APP_REGISTRY[0]));
inline void registerAllApps(mooncake::Mooncake &mc, DEVICES *dev) { for (const AppReg &r : APP_REGISTRY) mc.installApp(r.make(dev)); }

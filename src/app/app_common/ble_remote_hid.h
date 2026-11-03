// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#pragma once
#include <cstdint>

// Shared BLE HID transport for Media Remote and Slide Clicker. Static GATT
// attributes (keyboard report ID 1 + consumer control report ID 2), one
// Bluetooth lease, non-blocking 16-entry send queue drained by tick().
// Both apps present the same identity so the host pairs only once.
namespace ble_remote_hid {
// Consumer control bits (report ID 2).
constexpr uint16_t MEDIA_NEXT = 0x01, MEDIA_PREV = 0x02, MEDIA_STOP = 0x04,
                   MEDIA_PLAY_PAUSE = 0x08, MEDIA_MUTE = 0x10,
                   MEDIA_VOL_UP = 0x20, MEDIA_VOL_DOWN = 0x40;
// Keyboard usages (HID usage page 0x07).
constexpr uint8_t KEY_B = 0x05, KEY_ESC = 0x29, KEY_RIGHT = 0x4F,
                  KEY_LEFT = 0x50, KEY_F5 = 0x3E;

bool begin();               // acquire lease and start advertising; false on failure
void end();                 // neutral report, release lease; safe to call repeatedly
void tick();                // drain the send queue; call every onRunning
bool ready();               // encrypted link with subscribed reports
bool connected();           // link exists (possibly still pairing)
bool failed();              // BLE error or Bluetooth disabled/unavailable
bool tapKey(uint8_t usage, uint8_t mods = 0);  // press+release; false if dropped
bool tapMedia(uint16_t bits);                  // press+release; false if dropped
}

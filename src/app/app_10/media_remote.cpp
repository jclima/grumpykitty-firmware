// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Media Remote: joystick/buttons -> BLE consumer-control keys. */
#include "media_remote.h"
#include "../app_common/ble_remote_hid.h"
#include "../../system/bluetooth_manager.h"
#include <Arduino.h>
#include <cstring>

namespace {
enum Idx { I_UP, I_DOWN, I_LEFT, I_RIGHT, I_A, I_B };
enum Status { S_WAITING, S_PAIRING, S_CONNECTED, S_ERROR };
constexpr uint16_t COL_OK = 0x07E0;
constexpr uint32_t REPEAT_DELAY = 400, REPEAT_EVERY = 120, MSG_MS = 900;

int statusNow() {
    if (ble_remote_hid::failed()) return S_ERROR;
    if (ble_remote_hid::mediaReady()) return S_CONNECTED;
    if (ble_remote_hid::connected()) return S_PAIRING;
    return S_WAITING;
}
void put(lgfx::LGFXBase& lcd, int x, int y, const char* s, uint16_t fg) {
    lcd.setFont(&fonts::efontCN_16); lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(fg, hp::COL_BG); lcd.setCursor(x, y); lcd.print(s);
}
}

namespace MOONCAKE::APPS {
AppMediaRemote::AppMediaRemote(DEVICES* device) : _device(device) {
    setAppInfo().name = "Media Remote";
}
bool AppMediaRemote::_edge(int idx, bool down) {
    const bool fire = down && !_prev[idx];
    _prev[idx] = down;
    return fire;
}
void AppMediaRemote::onOpen() {
    memset(_prev, 0, sizeof(_prev));
    _a_armed = _b_armed = false; _b_since = 0;
    _hold_since[0] = _hold_since[1] = _rep_at[0] = _rep_at[1] = 0;
    _last_ui = 0; _msg_at = 0; _status = -1;
    _msg[0] = 0; _drawn_msg[0] = 1; _msg_dirty = true;
    auto& b = _device->button;
    b.update(); b.A.hasChanged(); b.B.hasChanged();
    b.Up.hasChanged(); b.Down.hasChanged(); b.Left.hasChanged(); b.Right.hasChanged();
    _drawStatic();
    ble_remote_hid::begin();
    _status_ui();
}
void AppMediaRemote::_drawStatic() {
    auto& lcd = _device->Lcd;
    lcd.fillScreen(hp::COL_BG);
    hp::drawChrome(lcd);
    hp::drawFooter(lcd, "A:Play B:Mute", "Hold B:Exit");
    put(lcd, 16, 40, "UP / DOWN     Volume", hp::COL_FG);
    put(lcd, 16, 64, "LEFT / RIGHT  Prev / Next", hp::COL_FG);
    put(lcd, 16, 88, "A             Play / Pause", hp::COL_FG);
    put(lcd, 16, 112, "B (tap)       Mute", hp::COL_FG);
}
void AppMediaRemote::_say(const char* text) {
    strncpy(_msg, text, sizeof(_msg) - 1); _msg[sizeof(_msg) - 1] = 0;
    _msg_at = millis(); _msg_dirty = true;
}
void AppMediaRemote::_media(uint16_t bits, const char* label) {
    _say(ble_remote_hid::tapMedia(bits) ? label : "NOT CONNECTED");
}
void AppMediaRemote::_status_ui() {
    auto& lcd = _device->Lcd;
    const int st = statusNow();
    if (st != _status) {
        _status = st; _msg_dirty = true;
        static const char* const names[] = {"WAITING", "PAIRING", "CONNECTED", "BLE ERROR"};
        static const uint16_t cols[] = {hp::COL_INFO, hp::COL_WARN, COL_OK, hp::COL_ERR};
        hp::drawHeader(lcd, "MEDIA REMOTE", names[st], cols[st]);
    }
    if (!_msg_dirty) return;
    // Bottom message row: persistent state text unless a recent action label is active.
    char line[28];
    uint16_t color = hp::COL_ACCENT;
    if (_msg[0] && millis() - _msg_at < MSG_MS) snprintf(line, sizeof(line), "%s", _msg);
    else {
        _msg[0] = 0;
        if (st == S_ERROR) {
            snprintf(line, sizeof(line), "%s", bluetooth_manager::enabled() ? "Bluetooth error" : "Enable Bluetooth in Settings");
            color = hp::COL_ERR;
        } else if (st == S_CONNECTED) { snprintf(line, sizeof(line), "Ready"); color = COL_OK; }
        else if (st == S_PAIRING) { snprintf(line, sizeof(line), "Pairing..."); color = hp::COL_WARN; }
        else { snprintf(line, sizeof(line), "Pair 'GrumpyKitty Remote'"); color = hp::COL_INFO; }
    }
    if (strcmp(line, _drawn_msg) != 0 || _msg_dirty) {
        lcd.fillRect(1, 150, 318, 30, hp::COL_BG);
        lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(color, hp::COL_BG);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.drawString(line, 160, 165);
        strncpy(_drawn_msg, line, sizeof(_drawn_msg) - 1); _drawn_msg[sizeof(_drawn_msg) - 1] = 0;
    }
    _msg_dirty = false;
}
void AppMediaRemote::onRunning() {
    const uint32_t now = millis();
    auto& b = _device->button;
    b.update(); b.tick();
    ble_remote_hid::tick();
    if (b.B.isLongPress()) { close(); return; }

    const bool a = b.A.state() == Button_Class::PRESSED;
    const bool bb = b.B.state() == Button_Class::PRESSED;
    const bool dirs[4] = {b.Up.state() == Button_Class::PRESSED, b.Down.state() == Button_Class::PRESSED,
                          b.Left.state() == Button_Class::PRESSED, b.Right.state() == Button_Class::PRESSED};
    // Volume repeats while held; track transport keys are single shot.
    for (int i = 0; i < 2; ++i) {
        const uint16_t bits = i == I_UP ? ble_remote_hid::MEDIA_VOL_UP : ble_remote_hid::MEDIA_VOL_DOWN;
        const char* label = i == I_UP ? "VOLUME +" : "VOLUME -";
        if (_edge(i, dirs[i])) { _hold_since[i] = now; _rep_at[i] = now + REPEAT_DELAY; _media(bits, label); }
        else if (dirs[i] && static_cast<int32_t>(now - _rep_at[i]) >= 0) {
            _rep_at[i] = now + REPEAT_EVERY; _media(bits, label);
        }
    }
    if (_edge(I_LEFT, dirs[2])) _media(ble_remote_hid::MEDIA_PREV, "PREVIOUS");
    if (_edge(I_RIGHT, dirs[3])) _media(ble_remote_hid::MEDIA_NEXT, "NEXT");

    if (!a) _a_armed = true;
    if (_edge(I_A, a) && _a_armed) _media(ble_remote_hid::MEDIA_PLAY_PAUSE, "PLAY / PAUSE");

    // B: fire on release if held < 1 s; long press is handled above (exit).
    const bool b_was = _prev[I_B];
    if (!bb) _b_armed = true;
    if (bb && !b_was) _b_since = now;
    if (!bb && b_was && _b_armed && now - _b_since < 1000) _media(ble_remote_hid::MEDIA_MUTE, "MUTE");
    _prev[I_B] = bb;

    if (_msg[0] && now - _msg_at >= MSG_MS) _msg_dirty = true;
    if (now - _last_ui >= 100 || _msg_dirty) { _last_ui = now; _status_ui(); }
    delay(1);
}
void AppMediaRemote::onClose() {
    ble_remote_hid::end();
    _device->Lcd.fillScreen(TFT_BLACK);
}
}

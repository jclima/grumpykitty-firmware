// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Slide Clicker: joystick/buttons/touch -> BLE HID keyboard presentation keys. */
#include "slide_clicker.h"
#include "../app_common/ble_remote_hid.h"
#include "../../system/bluetooth_manager.h"
#include <Arduino.h>
#include <cstring>

namespace {
enum Idx { I_UP, I_DOWN, I_LEFT, I_RIGHT, I_A, I_B };
enum Status { S_WAITING, S_PAIRING, S_CONNECTED, S_ERROR };
constexpr uint16_t COL_OK = 0x07E0;
constexpr uint32_t MSG_MS = 900;
// Touch buttons: Start (F5) left, End (Esc) right.
constexpr int BTN_Y = 146, BTN_H = 54, START_X = 12, END_X = 168, BTN_W = 140;

int statusNow() {
    if (ble_remote_hid::failed()) return S_ERROR;
    if (ble_remote_hid::ready()) return S_CONNECTED;
    if (ble_remote_hid::connected()) return S_PAIRING;
    return S_WAITING;
}
void button(lgfx::LGFXBase& lcd, int x, const char* label) {
    lcd.drawRoundRect(x, BTN_Y, BTN_W, BTN_H, 6, hp::COL_FG);
    lcd.drawRoundRect(x + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, 5, hp::COL_FG);
    lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(hp::COL_ACCENT, hp::COL_BG);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.drawString(label, x + BTN_W / 2, BTN_Y + BTN_H / 2);
}
}

namespace MOONCAKE::APPS {
AppSlideClicker::AppSlideClicker(DEVICES* device) : _device(device) {
    setAppInfo().name = "Slide Clicker";
}
bool AppSlideClicker::_edge(int idx, bool down) {
    const bool fire = down && !_prev[idx];
    _prev[idx] = down;
    return fire;
}
uint32_t AppSlideClicker::_elapsedSecs(uint32_t now) const {
    return (_timer_running ? now - _timer_start : _timer_frozen) / 1000;
}
void AppSlideClicker::onOpen() {
    memset(_prev, 0, sizeof(_prev));
    _a_armed = _b_armed = _touch_down = false; _b_since = 0;
    _timer_running = true; _timer_start = millis(); _timer_frozen = 0;
    _last_ui = 0; _msg_at = 0; _status = -1; _drawn_secs = -1;
    _msg[0] = 0; _drawn_msg[0] = 1; _msg_dirty = true;
    auto& b = _device->button;
    b.update(); b.A.hasChanged(); b.B.hasChanged();
    b.Up.hasChanged(); b.Down.hasChanged(); b.Left.hasChanged(); b.Right.hasChanged();
    _drawStatic();
    ble_remote_hid::begin();
    _ui();
}
void AppSlideClicker::_drawStatic() {
    auto& lcd = _device->Lcd;
    lcd.fillScreen(hp::COL_BG);
    hp::drawChrome(lcd);
    hp::drawFooter(lcd, "A:Next B:Blank", "Hold B:Exit");
    button(lcd, START_X, "START  F5");
    button(lcd, END_X, "END  Esc");
}
void AppSlideClicker::_say(const char* text) {
    strncpy(_msg, text, sizeof(_msg) - 1); _msg[sizeof(_msg) - 1] = 0;
    _msg_at = millis(); _msg_dirty = true;
}
void AppSlideClicker::_key(uint8_t usage, const char* label) {
    _say(ble_remote_hid::tapKey(usage) ? label : "NOT CONNECTED");
}
void AppSlideClicker::_ui() {
    auto& lcd = _device->Lcd;
    const uint32_t now = millis();
    const int st = statusNow();
    if (st != _status) {
        _status = st; _msg_dirty = true;
        static const char* const names[] = {"WAITING", "PAIRING", "CONNECTED", "BLE ERROR"};
        static const uint16_t cols[] = {hp::COL_INFO, hp::COL_WARN, COL_OK, hp::COL_ERR};
        hp::drawHeader(lcd, "SLIDE CLICKER", names[st], cols[st]);
    }
    const int32_t secs = static_cast<int32_t>(_elapsedSecs(now));
    if (secs != _drawn_secs) {
        _drawn_secs = secs;
        char t[12];
        snprintf(t, sizeof(t), "%02ld:%02ld", static_cast<long>((secs / 60) % 100), static_cast<long>(secs % 60));
        lcd.fillRect(1, 34, 318, 56, hp::COL_BG);
        lcd.setFont(&fonts::efontCN_16); lcd.setTextSize(4);
        lcd.setTextColor(_timer_running ? hp::COL_FG : hp::COL_DIM, hp::COL_BG);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.drawString(t, 160, 62);
        lcd.setTextSize(1);
    }
    if (!_msg_dirty) return;
    char line[28];
    uint16_t color = hp::COL_ACCENT;
    if (_msg[0] && now - _msg_at < MSG_MS) snprintf(line, sizeof(line), "%s", _msg);
    else {
        _msg[0] = 0;
        if (st == S_ERROR) {
            snprintf(line, sizeof(line), "%s", bluetooth_manager::enabled() ? "Bluetooth error" : "Enable Bluetooth in Settings");
            color = hp::COL_ERR;
        } else if (st == S_CONNECTED) { snprintf(line, sizeof(line), "Ready"); color = COL_OK; }
        else if (st == S_PAIRING) { snprintf(line, sizeof(line), "Pairing..."); color = hp::COL_WARN; }
        else { snprintf(line, sizeof(line), "Pair 'GrumpyKitty Remote'"); color = hp::COL_INFO; }
    }
    lcd.fillRect(1, 104, 318, 30, hp::COL_BG);
    lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(color, hp::COL_BG);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.drawString(line, 160, 119);
    strncpy(_drawn_msg, line, sizeof(_drawn_msg) - 1); _drawn_msg[sizeof(_drawn_msg) - 1] = 0;
    _msg_dirty = false;
}
void AppSlideClicker::_touch(uint32_t now) {
    const bool down = _device->ctp.isTouched();
    if (down && !_touch_down) {
        int x = -1, y = -1; _device->ctp.getPos(x, y);
        if (y >= BTN_Y && y < BTN_Y + BTN_H) {
            if (x >= START_X && x < START_X + BTN_W) {
                _timer_running = true; _timer_start = now; _drawn_secs = -1;
                _key(ble_remote_hid::KEY_F5, "START (F5)");
            } else if (x >= END_X && x < END_X + BTN_W) {
                if (_timer_running) { _timer_frozen = now - _timer_start; _timer_running = false; _drawn_secs = -1; }
                _key(ble_remote_hid::KEY_ESC, "END (Esc)");
            }
        }
    }
    _touch_down = down;
}
void AppSlideClicker::onRunning() {
    const uint32_t now = millis();
    auto& b = _device->button;
    b.update(); b.tick();
    ble_remote_hid::tick();
    if (b.B.isLongPress()) { close(); return; }

    const bool a = b.A.state() == Button_Class::PRESSED;
    const bool bb = b.B.state() == Button_Class::PRESSED;
    const bool up = b.Up.state() == Button_Class::PRESSED, down = b.Down.state() == Button_Class::PRESSED;
    const bool left = b.Left.state() == Button_Class::PRESSED, right = b.Right.state() == Button_Class::PRESSED;
    if (!a) _a_armed = true;
    const bool a_edge = _edge(I_A, a) && _a_armed;
    const bool r_edge = _edge(I_RIGHT, right), d_edge = _edge(I_DOWN, down);
    const bool l_edge = _edge(I_LEFT, left), u_edge = _edge(I_UP, up);
    if (r_edge || d_edge || a_edge) _key(ble_remote_hid::KEY_RIGHT, "NEXT");
    else if (l_edge || u_edge) _key(ble_remote_hid::KEY_LEFT, "PREVIOUS");

    // B: blank screen on release if held < 1 s; long press exits (handled above).
    const bool b_was = _prev[I_B];
    if (!bb) _b_armed = true;
    if (bb && !b_was) _b_since = now;
    if (!bb && b_was && _b_armed && now - _b_since < 1000) _key(ble_remote_hid::KEY_B, "BLANK (B)");
    _prev[I_B] = bb;

    _touch(now);
    if (_msg[0] && now - _msg_at >= MSG_MS) _msg_dirty = true;
    if (now - _last_ui >= 100 || _msg_dirty) { _last_ui = now; _ui(); }
    delay(1);
}
void AppSlideClicker::onClose() {
    ble_remote_hid::end();
    _device->Lcd.fillScreen(TFT_BLACK);
}
}

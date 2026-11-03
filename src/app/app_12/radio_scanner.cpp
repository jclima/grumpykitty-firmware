// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Radio Scanner: passive WiFi + BLE observation. Never transmits, never connects. */
#include "radio_scanner.h"
#include "../app_09/infrared_file_utils.h"
#include "../../system/bluetooth_manager.h"
#include "../../system/settings_bridge.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_gap_ble_api.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

namespace {
constexpr int MAX_WIFI = 40, BLE_SLOTS = 64, NAME_LEN = 20;
constexpr uint32_t WIFI_RESCAN_MS = 5000, WIFI_BUSY_RETRY_MS = 1000;
constexpr uint32_t BLE_SNAP_MS = 500, BLE_EXPIRE_MS = 30000, DRAW_MIN_MS = 100;
constexpr uint32_t REPEAT_DELAY = 400, REPEAT_EVERY = 120;
constexpr int VIS = hp::LIST2_VIS;
enum Idx { I_UP, I_DOWN, I_LEFT, I_RIGHT, I_A, I_B };
enum State { ST_SCAN, ST_BUSY, ST_WIFI_OFF, ST_BT_OFF, ST_BT_ERR };

struct WifiRow { char ssid[33]; uint8_t bssid[6]; uint8_t ch; int8_t rssi; uint8_t auth; };
struct BleRow { uint8_t addr[6]; uint8_t type; int8_t rssi; char name[NAME_LEN]; uint32_t seen; };

// Live BLE table: written only by the Bluetooth task callback, read by the UI snapshot.
struct BleSlot { bool used; BleRow row; };
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
BleSlot g_table[BLE_SLOTS];
std::atomic<bool> g_ble_paused{false};
std::atomic<bool> g_ble_running{false};
esp_ble_scan_params_t g_scan_params;

void tableClear() {
    portENTER_CRITICAL(&g_mux);
    memset(g_table, 0, sizeof(g_table));
    portEXIT_CRITICAL(&g_mux);
}

// Bluetooth task: fixed table only, no allocation, no LCD.
void onScanResult(const esp_ble_gap_cb_param_t::ble_scan_result_evt_param& r) {
    if (r.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT || g_ble_paused.load()) return;
    const uint32_t now = millis();
    const unsigned total = unsigned(r.adv_data_len) + unsigned(r.scan_rsp_len);
    char name[NAME_LEN] = {};
    uint8_t len = 0;
    uint8_t* p = esp_ble_resolve_adv_data(const_cast<uint8_t*>(r.ble_adv), ESP_BLE_AD_TYPE_NAME_CMPL, &len);
    if (!p) p = esp_ble_resolve_adv_data(const_cast<uint8_t*>(r.ble_adv), ESP_BLE_AD_TYPE_NAME_SHORT, &len);
    if (p && len && p >= r.ble_adv && unsigned(p - r.ble_adv) + len <= total && total <= sizeof(r.ble_adv)) {
        if (len > NAME_LEN - 1) len = NAME_LEN - 1;
        for (uint8_t i = 0; i < len; ++i) name[i] = (p[i] >= 0x20 && p[i] < 0x7F) ? char(p[i]) : '?';
    }
    portENTER_CRITICAL(&g_mux);
    int slot = -1, oldest = 0;
    for (int i = 0; i < BLE_SLOTS; ++i) {
        if (!g_table[i].used) { if (slot < 0) slot = i; continue; }
        if (memcmp(g_table[i].row.addr, r.bda, 6) == 0 && g_table[i].row.type == r.ble_addr_type) { slot = i; oldest = -1; break; }
        if (g_table[i].row.seen < g_table[oldest].row.seen || !g_table[oldest].used) oldest = i;
    }
    if (slot < 0) slot = oldest;  // table full: evict the stalest entry
    BleSlot& s = g_table[slot];
    const bool fresh = !s.used || memcmp(s.row.addr, r.bda, 6) != 0 || s.row.type != r.ble_addr_type;
    if (fresh) { memset(&s, 0, sizeof(s)); memcpy(s.row.addr, r.bda, 6); s.row.type = r.ble_addr_type; s.used = true; }
    s.row.rssi = r.rssi; s.row.seen = now;
    if (name[0]) memcpy(s.row.name, name, NAME_LEN);
    portEXIT_CRITICAL(&g_mux);
}

void gapCb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (p->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) esp_ble_gap_start_scanning(0);
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        g_ble_running = (p->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: onScanResult(p->scan_rst); break;
    default: break;
    }
}

void put(lgfx::LGFXBase& lcd, int x, int y, const char* s, uint16_t fg) {
    lcd.setFont(&fonts::efontCN_16); lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(fg, hp::COL_BG); lcd.setCursor(x, y); lcd.print(s);
}
const char* authName(uint8_t a) {
    switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA12";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "EAP";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WP23";
    default: return "?";
    }
}
void macStr(char* out, const uint8_t* m) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}
}

namespace MOONCAKE::APPS {
struct AppRadioScanner::Rows {
    WifiRow wifi[MAX_WIFI];
    BleRow ble[BLE_SLOTS];
};

AppRadioScanner::AppRadioScanner(DEVICES* device) : _device(device) {
    setAppInfo().name = "Radio Scanner";
}
bool AppRadioScanner::_edge(int idx, bool down) {
    const bool fire = down && !_prev[idx];
    _prev[idx] = down;
    return fire;
}

void AppRadioScanner::onOpen() {
    delete _r;
    _r = new (std::nothrow) Rows();
    if (!_r) { close(); return; }
    memset(_prev, 0, sizeof(_prev));
    _hold_at[0] = _hold_at[1] = _rep_at[0] = _rep_at[1] = 0;
    _paused = false; _dirty = true; _count = _sel = _off = 0; _state = ST_SCAN;
    _next_scan = _next_snap = _last_draw = 0;
    _wifi_scanning = false; _lease = 0; g_ble_paused = false; g_ble_running = false;
    _saved_mode = static_cast<int>(WiFi.getMode());
    auto& b = _device->button;
    b.update(); b.A.hasChanged(); b.B.hasChanged();
    b.Up.hasChanged(); b.Down.hasChanged(); b.Left.hasChanged(); b.Right.hasChanged();
    _device->Lcd.fillScreen(hp::COL_BG);
    hp::drawChrome(_device->Lcd);
    _enterTab(0);
}

void AppRadioScanner::_stopWifiScan() {
    if (_wifi_scanning) esp_wifi_scan_stop();
    _wifi_scanning = false;
    WiFi.scanDelete();
}
void AppRadioScanner::_stopBle() {
    if (_lease) bluetooth_manager::release(_lease);  // also stops scanning
    _lease = 0; g_ble_running = false;
}
void AppRadioScanner::_leaveTab() {
    if (_tab == 0) _stopWifiScan(); else _stopBle();
}
void AppRadioScanner::_enterTab(int tab) {
    _tab = tab; _count = _sel = _off = 0; _dirty = true; _paused = false;
    g_ble_paused = false; _next_scan = _next_snap = 0;
    if (tab == 0) {
        _state = settings_get_wifi_en() ? ST_SCAN : ST_WIFI_OFF;
        return;
    }
    tableClear();
    if (!bluetooth_manager::enabled()) { _state = ST_BT_OFF; return; }
    _lease = bluetooth_manager::acquire(bluetooth_manager::Owner::Scanner, "MeowKit Scanner", gapCb, nullptr, nullptr);
    if (!_lease) { _state = ST_BT_ERR; return; }
    memset(&g_scan_params, 0, sizeof(g_scan_params));
    g_scan_params.scan_type = BLE_SCAN_TYPE_PASSIVE;
    g_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    g_scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    g_scan_params.scan_interval = 0x50;
    g_scan_params.scan_window = 0x30;
    g_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    if (esp_ble_gap_set_scan_params(&g_scan_params) != ESP_OK) { _stopBle(); _state = ST_BT_ERR; return; }
    _state = ST_SCAN;
}

void AppRadioScanner::_sortClamp() {
    infrared_file::listWindow(_count, VIS, _sel, _off);
}

void AppRadioScanner::_wifiTick(uint32_t now) {
    if (!settings_get_wifi_en()) {
        if (_state != ST_WIFI_OFF) { _stopWifiScan(); _state = ST_WIFI_OFF; _count = 0; _dirty = true; }
        return;
    }
    if (_state == ST_WIFI_OFF) { _state = ST_SCAN; _next_scan = 0; _dirty = true; }
    if (_wifi_scanning) {
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        _wifi_scanning = false;
        if (n < 0 || _paused) {  // failed, or finished while paused: discard
            WiFi.scanDelete();
            if (n < 0) { _state = ST_BUSY; _next_scan = now + WIFI_BUSY_RETRY_MS; _dirty = true; }
            else _next_scan = now + WIFI_RESCAN_MS;
            return;
        }
        const int keep = std::min(n, MAX_WIFI);
        for (int i = 0; i < keep; ++i) {
            WifiRow& w = _r->wifi[i];
            const String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) strcpy(w.ssid, "<hidden>");
            else {
                strncpy(w.ssid, ssid.c_str(), sizeof(w.ssid) - 1); w.ssid[sizeof(w.ssid) - 1] = 0;
                for (char* c = w.ssid; *c; ++c) if (*c < 0x20 || *c >= 0x7F) *c = '?';
            }
            const uint8_t* bs = WiFi.BSSID(i);
            if (bs) memcpy(w.bssid, bs, 6); else memset(w.bssid, 0, 6);
            w.ch = uint8_t(WiFi.channel(i));
            w.rssi = int8_t(WiFi.RSSI(i));
            w.auth = uint8_t(WiFi.encryptionType(i));
        }
        WiFi.scanDelete();
        std::sort(_r->wifi, _r->wifi + keep, [](const WifiRow& a, const WifiRow& b) { return a.rssi > b.rssi; });
        _count = keep; _state = ST_SCAN; _next_scan = now + WIFI_RESCAN_MS; _dirty = true;
        return;
    }
    if (_paused || static_cast<int32_t>(now - _next_scan) < 0) return;
    // Async, show hidden, passive, 120 ms per channel.
    const int r = WiFi.scanNetworks(true, true, true, 120);
    if (r == WIFI_SCAN_RUNNING) {
        _wifi_scanning = true;
        if (_state != ST_SCAN) { _state = ST_SCAN; _dirty = true; }
    } else {
        WiFi.scanDelete();
        _state = ST_BUSY; _next_scan = now + WIFI_BUSY_RETRY_MS; _dirty = true;
    }
}

void AppRadioScanner::_bleTick(uint32_t now) {
    if (_state == ST_BT_OFF || _state == ST_BT_ERR) return;
    if (!bluetooth_manager::owns(_lease)) {  // revoked, e.g. Bluetooth turned off in Settings
        _lease = 0; g_ble_running = false;
        _state = bluetooth_manager::enabled() ? ST_BT_ERR : ST_BT_OFF; _count = 0; _dirty = true;
        return;
    }
    if (_paused || static_cast<int32_t>(now - _next_snap) < 0) return;
    _next_snap = now + BLE_SNAP_MS;
    int n = 0;
    portENTER_CRITICAL(&g_mux);
    for (int i = 0; i < BLE_SLOTS; ++i) {
        if (g_table[i].used && static_cast<int32_t>(now - g_table[i].row.seen) <= static_cast<int32_t>(BLE_EXPIRE_MS)) _r->ble[n++] = g_table[i].row;
        else g_table[i].used = false;
    }
    portEXIT_CRITICAL(&g_mux);
    std::sort(_r->ble, _r->ble + n, [](const BleRow& a, const BleRow& b) { return a.rssi > b.rssi; });
    _count = n; _dirty = true;
}

void AppRadioScanner::_draw() {
    auto& lcd = _device->Lcd;
    char badge[16];
    uint16_t bcol = hp::COL_INFO;
    if (_paused) { snprintf(badge, sizeof(badge), "PAUSED"); bcol = hp::COL_WARN; }
    else if (_state == ST_BUSY) { snprintf(badge, sizeof(badge), "BUSY"); bcol = hp::COL_WARN; }
    else if (_state == ST_WIFI_OFF || _state == ST_BT_OFF || _state == ST_BT_ERR) { snprintf(badge, sizeof(badge), "OFF"); bcol = hp::COL_ERR; }
    else snprintf(badge, sizeof(badge), "%d %s", _count, _tab == 0 ? "APs" : "DEV");
    hp::drawHeader(lcd, _tab == 0 ? "WIFI SCAN" : "BLE SCAN", badge, bcol);
    hp::drawFooter3(lcd, "<>Tab ^vMove", _paused ? "[A]Run" : "[A]Pause", "Hold B:Exit");
    _sortClamp();
    hp::clearContent(lcd);
    const char* msg = nullptr;
    uint16_t mcol = hp::COL_DIM;
    switch (_state) {
    case ST_WIFI_OFF: msg = "WiFi disabled in Settings"; mcol = hp::COL_ERR; break;
    case ST_BT_OFF: msg = "Enable Bluetooth in Settings"; mcol = hp::COL_ERR; break;
    case ST_BT_ERR: msg = "Bluetooth error"; mcol = hp::COL_ERR; break;
    default:
        if (_count == 0) msg = _state == ST_BUSY ? "Radio busy, retrying..." : "Scanning...";
    }
    if (msg && _count == 0) { put(lcd, 16, 90, msg, mcol); return; }
    const uint32_t now = millis();
    const int end = std::min(VIS, _count - _off);
    for (int i = 0; i < end; ++i) {
        char title[40], sub[48], mac[18];
        if (_tab == 0) {
            const WifiRow& w = _r->wifi[_off + i];
            macStr(mac, w.bssid);
            snprintf(title, sizeof(title), "%s", w.ssid);
            snprintf(sub, sizeof(sub), "ch%u %d %s %s", w.ch, w.rssi, authName(w.auth), mac);
        } else {
            const BleRow& e = _r->ble[_off + i];
            macStr(mac, e.addr);
            snprintf(title, sizeof(title), "%s", e.name[0] ? e.name : "<no name>");
            int32_t age = static_cast<int32_t>(now - e.seen);
            if (age < 0) age = 0;
            snprintf(sub, sizeof(sub), "%ddBm %s %lus", e.rssi, mac, (unsigned long)(age / 1000));
        }
        hp::drawListItemSub(lcd, i, title, sub, i == _sel);
    }
    hp::drawScrollbar2(lcd, _count, _off, VIS);
}

void AppRadioScanner::onRunning() {
    const uint32_t now = millis();
    auto& b = _device->button;
    b.update(); b.tick();
    if (!_r) { close(); return; }
    if (b.B.isLongPress()) { close(); return; }

    const bool up = b.Up.state() == Button_Class::PRESSED, down = b.Down.state() == Button_Class::PRESSED;
    const bool left = b.Left.state() == Button_Class::PRESSED, right = b.Right.state() == Button_Class::PRESSED;
    const bool a = b.A.state() == Button_Class::PRESSED;
    const bool dirs[2] = {up, down};
    for (int i = 0; i < 2; ++i) {
        bool step = false;
        if (_edge(i, dirs[i])) { _rep_at[i] = now + REPEAT_DELAY; step = true; }
        else if (dirs[i] && static_cast<int32_t>(now - _rep_at[i]) >= 0) { _rep_at[i] = now + REPEAT_EVERY; step = true; }
        if (!step || _count == 0) continue;
        const int abs = _off + _sel + (i == I_UP ? -1 : 1);
        if (abs < 0 || abs >= _count) continue;
        if (abs < _off) { _off = abs; _sel = 0; }
        else if (abs >= _off + VIS) { _off = abs - VIS + 1; _sel = VIS - 1; }
        else _sel = abs - _off;
        _dirty = true;
    }
    if (_edge(I_LEFT, left) && _tab != 0) { _leaveTab(); _enterTab(0); }
    else if (_edge(I_RIGHT, right) && _tab != 1) { _leaveTab(); _enterTab(1); }
    if (_edge(I_A, a)) {
        _paused = !_paused; g_ble_paused = _paused;
        if (!_paused && _tab == 1) { tableClear(); _next_snap = 0; }  // drop entries aged out while paused
        _dirty = true;
    }
    if (_tab == 0) _wifiTick(now); else _bleTick(now);

    if (_dirty && now - _last_draw >= DRAW_MIN_MS) { _dirty = false; _last_draw = now; _draw(); }
    delay(1);
}

void AppRadioScanner::onClose() {
    _leaveTab();
    // WiFi.scanNetworks() may have switched the radio into STA; put back what we found.
    if (static_cast<int>(WiFi.getMode()) != _saved_mode) WiFi.mode(static_cast<wifi_mode_t>(_saved_mode));
    delete _r; _r = nullptr;
    _device->Lcd.fillScreen(TFT_BLACK);
}
}

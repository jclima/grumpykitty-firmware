// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Radio Scanner: passive WiFi + BLE observation. Uses passive discovery without initiating a connection. */
#include "radio_scanner.h"
#include "../app_09/infrared_file_utils.h"
#include "../../system/bluetooth_manager.h"
#include "../../system/settings_bridge.h"
#include "../../ui/ui_wifi_bridge.h"
#include "../app_common/sd_guard.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <esp_wifi.h>
#include <esp_gap_ble_api.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

namespace {
constexpr int MAX_WIFI = 40, BLE_SLOTS = 64, NAME_LEN = 20, CHANNELS = 14;
constexpr uint32_t WIFI_RESCAN_MS = 5000, WIFI_BUSY_RETRY_MS = 1000;
constexpr uint32_t BLE_SNAP_MS = 500, BLE_EXPIRE_MS = 30000, DRAW_MIN_MS = 100;
constexpr uint32_t BLE_START_DEADLINE_MS = 5000;
constexpr uint32_t MONITOR_DRAW_MS = 250;
constexpr uint32_t REPEAT_DELAY = 400, REPEAT_EVERY = 120;
constexpr int VIS = hp::LIST2_VIS;
enum Idx { I_UP, I_DOWN, I_LEFT, I_RIGHT, I_A };
enum State { ST_SCAN, ST_BUSY, ST_WIFI_OFF, ST_BT_OFF, ST_BT_ERR };

struct WifiRow { char ssid[33]; uint8_t bssid[6]; uint8_t ch; int8_t rssi; uint8_t auth; };
struct BleRow { uint8_t addr[6]; uint8_t type; int8_t rssi; char name[NAME_LEN]; uint32_t seen; };

// Live BLE table: written only by the Bluetooth task callback, read by the UI snapshot.
struct BleSlot { bool used; BleRow row; };
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
BleSlot g_table[BLE_SLOTS];
std::atomic<bool> g_ble_paused{false};
std::atomic<bool> g_ble_running{false};
std::atomic<bool> g_ble_active{false};
std::atomic<bool> g_ble_starting{false};
std::atomic<bool> g_ble_start_failed{false};
std::atomic<uint32_t> g_ble_start_deadline{0};
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
    if (!g_ble_active.load()) return;
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (p->scan_param_cmpl.status != ESP_BT_STATUS_SUCCESS || esp_ble_gap_start_scanning(0) != ESP_OK)
            g_ble_start_failed = true;
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        g_ble_running = (p->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
        g_ble_starting = false;
        if (!g_ble_running) g_ble_start_failed = true;
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
// A Wi-Fi label has at most 32 bytes. Quotes may double, and a spreadsheet
// text prefix, two delimiters and the terminator need four more bytes.
void csvLabel(char (&out)[68], const char* label) {
    const char* first = label;
    while (*first == ' ') ++first;
    char* p = out;
    *p++ = '"';
    if (*first && strchr("=+-@", *first)) *p++ = '\'';
    for (; *label; ++label) {
        if (*label == '"') *p++ = '"';
        *p++ = *label;
    }
    *p++ = '"';
    *p = '\0';
}
}

namespace MOONCAKE::APPS {
struct AppRadioScanner::Rows {
    WifiRow wifi[MAX_WIFI];
    uint16_t channels[CHANNELS];
    int wifiTotal;
    uint32_t wifiSeen;
    bool wifiReady;
    BleRow ble[BLE_SLOTS];
};

AppRadioScanner::AppRadioScanner(DEVICES* device) : _device(device) {
    setAppInfo().name = "Radio Scan+";
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
    _rep_at[0] = _rep_at[1] = 0;
    _paused = false; _dirty = true; _count = _sel = _off = 0; _state = ST_SCAN;
    _screen = SURVEY; _b_down = false; _message[0] = 0;
    _monitor_option = 0; _monitor_channel = 0;
    _monitor_filter = wifi_packet_monitor::Filter::All;
    _monitor_save = false; _monitor_redraw = 0;
    _next_scan = _next_snap = millis(); _last_draw = 0;   // not 0: signed deadline compare stalls after ~24.8 d uptime
    _wifi_scanning = false; _lease = 0; g_ble_paused = false; g_ble_running = false;
    _saved_mode = static_cast<int>(WiFi.getMode());
    auto& b = _device->button;
    b.update(); b.A.hasChanged(); b.B.hasChanged();
    b.Up.hasChanged(); b.Down.hasChanged(); b.Left.hasChanged(); b.Right.hasChanged();
    _device->Lcd.fillScreen(hp::COL_BG);
    hp::drawChrome(_device->Lcd);
    _prev[I_A] = b.A.state() == Button_Class::PRESSED;
    _enterTab(WIFI);
}

void AppRadioScanner::_stopWifiScan() {
    if (ui_wifi_bridge_radio_busy()) {
        /* The bridge may have taken ownership since our last tick. Never stop
         * or delete a scan whose result buffer is not ours. */
        _wifi_scanning = false;
        return;
    }
    if (!_wifi_scanning) return;
    esp_wifi_scan_stop();
    /* The Arduino core clears its "scan running" bit only on the scan-done event
     * (or in scanComplete()'s timeout branch): wait for it, or later scans
     * elsewhere report "running". Bounded, so the loop is never blocked long. */
    const uint32_t t0 = millis();
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - t0 < 2600) delay(10);
    _wifi_scanning = false;
    WiFi.scanDelete();
}
void AppRadioScanner::_stopBle() {
    g_ble_active = false; g_ble_starting = false;
    if (_lease) bluetooth_manager::release(_lease);  // also stops scanning
    _lease = 0; g_ble_running = false;
}
void AppRadioScanner::_leaveTab() {
    if (_tab == BLE) _stopBle(); else _stopWifiScan();
}
void AppRadioScanner::_enterTab(Tab tab) {
    _tab = tab; _count = _sel = _off = 0; _dirty = true; _paused = false;
    g_ble_paused = false; _next_scan = _next_snap = millis();
    if (tab != BLE) {
        _r->wifiReady = false;
        _state = settings_get_wifi_en() ? ST_SCAN : ST_WIFI_OFF;
        return;
    }
    tableClear();
    if (!bluetooth_manager::enabled()) { _state = ST_BT_OFF; return; }
    _lease = bluetooth_manager::acquire(bluetooth_manager::Owner::Scanner, "GrumpyKitty Scanner", gapCb, nullptr, nullptr);
    if (!_lease) { _state = ST_BT_ERR; return; }
    memset(&g_scan_params, 0, sizeof(g_scan_params));
    g_scan_params.scan_type = BLE_SCAN_TYPE_PASSIVE;
    g_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    g_scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    g_scan_params.scan_interval = 0x50;
    g_scan_params.scan_window = 0x30;
    g_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    g_ble_running = false; g_ble_start_failed = false; g_ble_starting = true;
    g_ble_start_deadline = millis() + BLE_START_DEADLINE_MS;
    g_ble_active = true;
    if (esp_ble_gap_set_scan_params(&g_scan_params) != ESP_OK) { _stopBle(); _state = ST_BT_ERR; return; }
    _state = ST_SCAN;
}

void AppRadioScanner::_sortClamp() {
    infrared_file::listWindow(_count, VIS, _sel, _off);
}

void AppRadioScanner::_wifiTick(uint32_t now) {
    if (!settings_get_wifi_en()) {
        if (_state != ST_WIFI_OFF) { _stopWifiScan(); _state = ST_WIFI_OFF; _count = 0; _r->wifiReady = false; _target_present = false; _dirty = true; }
        return;
    }
    if (_state == ST_WIFI_OFF) { _state = ST_SCAN; _next_scan = now; _dirty = true; }
    if (_wifi_scanning) {
        if (ui_wifi_bridge_radio_busy()) {
            _wifi_scanning = false;
            _state = ST_BUSY;
            _next_scan = now + WIFI_BUSY_RETRY_MS;
            _dirty = true;
            return;
        }
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        _wifi_scanning = false;
        if (n < 0 || _paused) {  // failed, or finished while paused: discard
            WiFi.scanDelete();
            if (n < 0) { _state = ST_BUSY; _next_scan = now + WIFI_BUSY_RETRY_MS; _dirty = true; }
            else _next_scan = now + WIFI_RESCAN_MS;
            return;
        }
        // Arduino's accessors use an 8-bit scan index.
        const int scanned = std::min(n, 256);
        memset(_r->channels, 0, sizeof(_r->channels));
        _r->wifiTotal = scanned;
        _r->wifiSeen = now;
        _r->wifiReady = true;
        if (_screen == SIGNAL) _target_present = false;
        for (int i = 0; i < scanned; ++i) {
            const int channel = WiFi.channel(i);
            if (channel >= 1 && channel <= CHANNELS) ++_r->channels[channel - 1];
            const auto* bssid = WiFi.BSSID(i);
            if (_screen == SIGNAL && !_target_present && bssid && !memcmp(bssid, _target_key, 6)) {
                _addSample(int8_t(WiFi.RSSI(i)), now);
            }
        }
        /* Keep the MAX_WIFI strongest networks: once the table is full, a
         * stronger result replaces the weakest kept row. */
        int keep = 0;
        for (int i = 0; i < scanned; ++i) {
            const int8_t rssi = int8_t(WiFi.RSSI(i));
            int slot = keep;
            if (keep >= MAX_WIFI) {
                int weakest = 0;
                for (int k = 1; k < keep; ++k) if (_r->wifi[k].rssi < _r->wifi[weakest].rssi) weakest = k;
                if (rssi <= _r->wifi[weakest].rssi) continue;
                slot = weakest;
            } else {
                ++keep;
            }
            WifiRow& w = _r->wifi[slot];
            const String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) strcpy(w.ssid, "<hidden>");
            else {
                strncpy(w.ssid, ssid.c_str(), sizeof(w.ssid) - 1); w.ssid[sizeof(w.ssid) - 1] = 0;
                for (char* c = w.ssid; *c; ++c) if (*c < 0x20 || *c >= 0x7F) *c = '?';
            }
            const uint8_t* bs = WiFi.BSSID(i);
            if (bs) memcpy(w.bssid, bs, 6); else memset(w.bssid, 0, 6);
            w.ch = uint8_t(WiFi.channel(i));
            w.rssi = rssi;
            w.auth = uint8_t(WiFi.encryptionType(i));
        }
        WiFi.scanDelete();
        std::sort(_r->wifi, _r->wifi + keep, [](const WifiRow& a, const WifiRow& b) { return a.rssi > b.rssi; });
        _count = keep; _state = ST_SCAN; _next_scan = now + WIFI_RESCAN_MS; _dirty = true;
        return;
    }
    if (_paused || static_cast<int32_t>(now - _next_scan) < 0) return;
    /* The UI bridge owns scan buffers and radio actions while active. Wait for
     * it to finish rather than starting an unrelated Arduino scan. */
    if (ui_wifi_bridge_radio_busy()) {
        _state = ST_BUSY;
        _next_scan = now + WIFI_BUSY_RETRY_MS;
        _dirty = true;
        return;
    }
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
        _target_present = false;
        _state = bluetooth_manager::enabled() ? ST_BT_ERR : ST_BT_OFF; _count = 0; _dirty = true;
        return;
    }
    if (g_ble_start_failed.load() || (g_ble_starting.load() && static_cast<int32_t>(now - g_ble_start_deadline.load()) >= 0)) {
        _stopBle(); _state = ST_BT_ERR; _count = 0;
        _setMessage("BLE scan failed; press A to retry", hp::COL_ERR); _dirty = true;
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
    _count = n;
    if (_screen == SIGNAL) {
        _target_present = false;
        for (int i = 0; i < n; ++i) {
            const auto& row = _r->ble[i];
            if (row.type != _target_type || memcmp(row.addr, _target_key, 6)) continue;
            if (row.seen != _target_seen) _addSample(row.rssi, row.seen);
            _target_present = true;
            break;
        }
    }
    _dirty = true;
}

void AppRadioScanner::_addSample(int8_t rssi, uint32_t seen) {
    constexpr int capacity = sizeof(_history);
    _history[_history_head] = rssi;
    _history_head = (_history_head + 1) % capacity;
    _history_count = std::min(_history_count + 1, capacity);
    _target_seen = seen;
    _target_present = true;
}

void AppRadioScanner::_trackSelection() {
    const int index = _off + _sel;
    if (index >= _count) return;
    _history_head = _history_count = 0;
    if (_tab == BLE) {
        const auto& row = _r->ble[index];
        memcpy(_target_key, row.addr, sizeof(_target_key));
        _target_type = row.type;
        _addSample(row.rssi, row.seen);
    } else {
        const auto& row = _r->wifi[index];
        memcpy(_target_key, row.bssid, sizeof(_target_key));
        _addSample(row.rssi, _r->wifiSeen);
    }
    _screen = SIGNAL;
}

void AppRadioScanner::_setMessage(const char* text, uint16_t color) {
    strncpy(_message, text, sizeof(_message) - 1); _message[sizeof(_message) - 1] = 0; _message_color = color; _dirty = true;
}

bool AppRadioScanner::_exportCsv() {
    if (_count == 0) { _setMessage("No results to export", hp::COL_WARN); return false; }
    const auto status = sd_guard::check();
    if (status != sd_guard::Status::Ok) {
        _setMessage(sd_guard::message(status), hp::COL_ERR);
        return false;
    }
    constexpr char dir[] = "/surveys";
    if (!SD_MMC.exists(dir) && !SD_MMC.mkdir(dir)) {
        _setMessage("Cannot create /surveys", hp::COL_ERR);
        return false;
    }

    const bool ble = _tab == BLE;
    const unsigned long stamp = millis();
    char path[40];
    File file;
    for (unsigned i = 0; i < 32; ++i) {
        snprintf(path, sizeof(path), "%s/%s-%08lx-%02u.csv", dir, ble ? "ble" : "wifi", stamp, i);
        if (!SD_MMC.exists(path)) {
            file = SD_MMC.open(path, FILE_WRITE);
            break;
        }
    }
    if (!file) { _setMessage("Cannot open survey CSV", hp::COL_ERR); return false; }

    size_t expected = 0;
    auto write = [&](const char* data, size_t size) {
        expected += size;
        return file.write(reinterpret_cast<const uint8_t*>(data), size) == size;
    };
    constexpr char header[] = "radio,id,label,rssi_dbm,channel,security,address_type\r\n";
    bool ok = write(header, sizeof(header) - 1);
    for (int i = 0; ok && i < _count; ++i) {
        char id[18], label[68], line[128];
        int length;
        if (ble) {
            const auto& row = _r->ble[i];
            macStr(id, row.addr);
            csvLabel(label, row.name);
            length = snprintf(line, sizeof(line), "ble,%s,%s,%d,,,%u\r\n",
                              id, label, row.rssi, unsigned(row.type));
        } else {
            const auto& row = _r->wifi[i];
            macStr(id, row.bssid);
            csvLabel(label, row.ssid);
            length = snprintf(line, sizeof(line), "wifi,%s,%s,%d,%u,%s,\r\n",
                              id, label, row.rssi, unsigned(row.ch), authName(row.auth));
        }
        ok = length > 0 && size_t(length) < sizeof(line) && write(line, size_t(length));
    }
    file.flush();
    file.close();
    if (ok) {
        // Arduino File::flush and close do not return filesystem errors.
        File saved = SD_MMC.open(path, FILE_READ);
        ok = saved && saved.size() == expected;
        saved.close();
    }
    if (!ok) {
        SD_MMC.remove(path);
        _setMessage("CSV write failed", hp::COL_ERR);
        return false;
    }
    _setMessage(path, hp::COL_INFO);
    return true;
}

void AppRadioScanner::_drawChannels() {
    auto& lcd = _device->Lcd;
    if (!_r->wifiReady) {
        put(lcd, 12, 90, _state == ST_WIFI_OFF ? "Wi-Fi disabled in Settings" :
                         _state == ST_BUSY ? "Radio busy, retrying..." :
                         _paused ? "Survey paused" : "Waiting for first Wi-Fi scan", hp::COL_DIM);
        return;
    }
    put(lcd, 12, 34, "Access points per channel", hp::COL_FG);
    const auto* channels = _r->channels;
    const int peak = std::max<int>(1, *std::max_element(channels, channels + CHANNELS));
    constexpr int base = 173, maxBar = 83;
    lcd.drawFastHLine(15, base, 290, hp::COL_DIM);
    for (int i = 0; i < CHANNELS; ++i) {
        const int x = 16 + i * 21;
        const int count = channels[i];
        const int height = count ? std::max(2, count * maxBar / peak) : 0;
        if (height) lcd.fillRect(x + 2, base - height, 13, height, hp::COL_ACCENT);
        char label[6];
        snprintf(label, sizeof(label), "%d", count);
        // Three digit counts are possible on a crowded channel. Raise odd
        // labels so adjacent values have room.
        if (count) put(lcd, x, base - height - 17 - (i % 2) * 17, label, hp::COL_FG);
        snprintf(label, sizeof(label), "%d", i + 1);
        put(lcd, x, base + 4, label, hp::COL_DIM);
    }
}

void AppRadioScanner::_drawScout() {
    auto& lcd = _device->Lcd;
    const bool found = _r->wifiReady && _r->wifiTotal > 0;
    const char* status = _state == ST_WIFI_OFF ? "Wi-Fi is off." :
                         _paused ? "Scout is on a break." :
                         _state == ST_BUSY ? "The radio is busy." :
                         found ? "I hear something." :
                         _r->wifiReady ? "Quiet around here." : "Scout is listening...";
    const uint16_t ink = found && !_paused ? hp::COL_ACCENT : hp::COL_DIM;
    lcd.fillTriangle(111, 115, 112, 64, 147, 100, ink);
    lcd.fillTriangle(173, 100, 208, 64, 209, 115, ink);
    lcd.fillEllipse(160, 119, 52, 32, ink);
    const int eye = found && !_paused ? 9 : 2;
    lcd.fillEllipse(139, 115, 8, eye, hp::COL_BG);
    lcd.fillEllipse(181, 115, 8, eye, hp::COL_BG);
    lcd.drawLine(129, 105, 149, 113, hp::COL_BG);
    lcd.drawLine(171, 113, 191, 105, hp::COL_BG);
    lcd.fillTriangle(155, 128, 165, 128, 160, 133, hp::COL_BG);
    lcd.drawLine(154, 140, 160, 137, hp::COL_BG);
    lcd.drawLine(160, 137, 166, 140, hp::COL_BG);
    put(lcd, 72, 160, status, hp::COL_FG);
    if (_r->wifiReady) {
        char line[35];
        snprintf(line, sizeof(line), "%d APs in last scan", _r->wifiTotal);
        put(lcd, 72, 182, line, hp::COL_DIM);
    }
}

void AppRadioScanner::_drawHistory() {
    auto& lcd = _device->Lcd;
    char addr[18];
    macStr(addr, _target_key);
    put(lcd, 16, 36, addr, hp::COL_FG);
    char status[38];
    constexpr int capacity = sizeof(_history);
    const int newest = (_history_head + capacity - 1) % capacity;
    const auto age = static_cast<unsigned long>((millis() - _target_seen) / 1000);
    if (_paused) snprintf(status, sizeof(status), "Paused  |  Last: %d dBm", _history[newest]);
    else if (!_target_present) snprintf(status, sizeof(status), "Not seen  |  Last: %d dBm", _history[newest]);
    else snprintf(status, sizeof(status), "%d dBm  |  Seen %lus ago", _history[newest], age);
    put(lcd, 16, 58, status, _target_present ? hp::COL_INFO : hp::COL_WARN);

    constexpr int graphX = 65, graphTop = 88, base = 175;
    lcd.drawRect(graphX, graphTop, 238, base - graphTop + 1, hp::COL_DIM);
    put(lcd, 18, graphTop, "-20", hp::COL_DIM);
    put(lcd, 18, base - 15, "-100", hp::COL_DIM);
    for (int i = 0; i < _history_count; ++i) {
        const int sample = (_history_head + capacity - _history_count + i) % capacity;
        const int x = graphX + 8 + (capacity - _history_count + i) * 14;
        const int strength = std::clamp<int>(_history[sample], -100, -20);
        const int height = std::max(2, (strength + 100) * (base - graphTop - 1) / 80);
        lcd.fillRect(x, base - height, 9, height, hp::COL_ACCENT);
    }
    put(lcd, 68, 184, "Oldest", hp::COL_DIM);
    put(lcd, 253, 184, "Newest", hp::COL_DIM);
}

void AppRadioScanner::_drawActions() {
    auto& lcd = _device->Lcd;
    hp::drawListItem(lcd, 0, _paused ? "Resume survey" : "Pause survey", _action == 0);
    hp::drawListItem(lcd, 1, "Export CSV snapshot", _action == 1);
    if (_tab != BLE) hp::drawListItem(lcd, 2, "Wi-Fi packet monitor", _action == 2);
    if (_message[0]) put(lcd, 12, 120, _message, _message_color);
    put(lcd, 12, 147, _tab == BLE ? "CSV: current BLE results" : "CSV: current Wi-Fi results", hp::COL_DIM);
}

void AppRadioScanner::_drawMonitor() {
    namespace monitor = wifi_packet_monitor;
    auto& lcd = _device->Lcd;
    const bool live = monitor::running();
    const auto stats = monitor::stats();
    char line[40], badge[16];
    if (live) snprintf(badge, sizeof(badge), "LIVE CH%u", unsigned(stats.channel));
    else snprintf(badge, sizeof(badge), "STOPPED");
    hp::drawHeader(lcd, "PACKET MONITOR", badge, live ? hp::COL_ACCENT : hp::COL_FG);
    hp::drawFooter3(lcd, live ? "" : "^vPick <>Set", live ? "A Stop" : "A Start",
                    live ? "Hold B:Exit" : "B Back");

    if (_monitor_channel) snprintf(line, sizeof(line), "Channel: %u", unsigned(_monitor_channel));
    else snprintf(line, sizeof(line), "Channel: current");
    hp::drawListItem(lcd, 0, line, !live && _monitor_option == 0);
    constexpr const char* filters[] = {"All management", "Beacons", "Probes", "Disconnect frames"};
    snprintf(line, sizeof(line), "PCAP: %s", filters[static_cast<unsigned>(_monitor_filter)]);
    hp::drawListItem(lcd, 1, line, !live && _monitor_option == 1);
    hp::drawListItem(lcd, 2, _monitor_save ? "Save to microSD: on" : "Save to microSD: off",
                     !live && _monitor_option == 2);

    snprintf(line, sizeof(line), "Beacon %lu   Probe %lu",
             (unsigned long)stats.beacons, (unsigned long)stats.probes);
    put(lcd, 12, 109, line, hp::COL_FG);
    snprintf(line, sizeof(line), "Disconnect %lu   Other %lu",
             (unsigned long)stats.disconnects, (unsigned long)stats.other);
    put(lcd, 12, 130, line, hp::COL_FG);
    snprintf(line, sizeof(line), "Written %lu   Dropped %lu",
             (unsigned long)stats.saved, (unsigned long)stats.dropped);
    put(lcd, 12, 151, line, hp::COL_DIM);
    const char* msg = _message[0] ? _message : monitor::message();
    if (msg && msg[0]) put(lcd, 12, 174, msg, _message[0] ? _message_color : hp::COL_INFO);
    const char* path = monitor::savedPath();
    if (path[0]) put(lcd, 12, 193, path, hp::COL_DIM);
}

void AppRadioScanner::_monitorAdjust(int direction) {
    if (_monitor_option == 0) {
        _monitor_channel = (_monitor_channel + direction + CHANNELS + 1) % (CHANNELS + 1);
    } else if (_monitor_option == 1) {
        constexpr int count = static_cast<int>(wifi_packet_monitor::Filter::Disconnects) + 1;
        _monitor_filter = static_cast<wifi_packet_monitor::Filter>(
            (static_cast<int>(_monitor_filter) + direction + count) % count);
    } else {
        _monitor_save = !_monitor_save;
    }
    _message[0] = 0;
    _dirty = true;
}

void AppRadioScanner::_draw() {
    auto& lcd = _device->Lcd;
    hp::clearContent(lcd);
    if (_screen == MONITOR) { _drawMonitor(); return; }
    if (_screen == SIGNAL) {
        hp::drawHeader(lcd, "SIGNAL HISTORY");
        hp::drawFooter(lcd, "B Back", "Hold B:Exit");
        _drawHistory();
        return;
    }
    if (_screen == ACTIONS) {
        hp::drawHeader(lcd, "SURVEY ACTIONS");
        hp::drawFooter3(lcd, "^v Move", "A Select", "B Back");
        _drawActions();
        return;
    }
    char badge[16];
    uint16_t bcol = hp::COL_INFO;
    if (_paused) { snprintf(badge, sizeof(badge), "PAUSED"); bcol = hp::COL_WARN; }
    else if (_state == ST_BUSY) { snprintf(badge, sizeof(badge), "BUSY"); bcol = hp::COL_WARN; }
    else if (_state == ST_WIFI_OFF || _state == ST_BT_OFF || _state == ST_BT_ERR) { snprintf(badge, sizeof(badge), "OFF"); bcol = hp::COL_ERR; }
    else if (_tab == BLE) snprintf(badge, sizeof(badge), "%d DEV", _count);
    else if (_r->wifiReady) snprintf(badge, sizeof(badge), "%d APs", _r->wifiTotal);
    else snprintf(badge, sizeof(badge), "SCAN");
    const char* titles[] = {"WIFI SCAN", "CHANNELS", "GRUMPY SCOUT", "BLE SCAN"};
    hp::drawHeader(lcd, titles[_tab], badge, bcol);
    const bool list = _tab == WIFI || _tab == BLE;
    hp::drawFooter3(lcd, list ? "<>Tab ^vMove" : "<>Tab", list ? "A Track" : "", "B Actions");
    if (_tab == CHANNELS) { _drawChannels(); return; }
    if (_tab == SCOUT) { _drawScout(); return; }
    _sortClamp();
    const char* msg = nullptr;
    uint16_t mcol = hp::COL_DIM;
    switch (_state) {
    case ST_WIFI_OFF: msg = "WiFi disabled in Settings"; mcol = hp::COL_ERR; break;
    case ST_BT_OFF: msg = "Enable Bluetooth in Settings"; mcol = hp::COL_ERR; break;
    case ST_BT_ERR: msg = "Bluetooth error"; mcol = hp::COL_ERR; break;
    default:
        if (_count == 0) {
            msg = _state == ST_BUSY ? "Radio busy, retrying..." :
                  _paused ? "Survey paused" :
                  _tab == WIFI && _r->wifiReady ? "No access points in last scan" : "Scanning...";
        }
    }
    if (msg && _count == 0) { put(lcd, 16, 90, msg, mcol); return; }
    const uint32_t now = millis();
    const int end = std::min(VIS, _count - _off);
    for (int i = 0; i < end; ++i) {
        char title[40], sub[48], mac[18];
        if (_tab == WIFI) {
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
    if (_b_down && b.B.isLongPress()) { close(); return; }

    const bool pressed = b.B.state() == Button_Class::PRESSED;
    if (b.B.hasChanged() && pressed) {
        _b_down = true;
        _b_started = now;
    }
    if (!pressed && _b_down) {
        _b_down = false;
        if (now - _b_started >= 1000) { close(); return; }
        if (_screen == SURVEY) {
            _screen = ACTIONS;
            _action = 0;
            _message[0] = 0;
        } else if (_screen == MONITOR && wifi_packet_monitor::running()) {
            wifi_packet_monitor::stop();
        } else {
            _screen = SURVEY;
        }
        _dirty = true;
    }

    const bool dirs[] = {b.Up.state() == Button_Class::PRESSED, b.Down.state() == Button_Class::PRESSED};
    const bool left = _edge(I_LEFT, b.Left.state() == Button_Class::PRESSED);
    const bool right = _edge(I_RIGHT, b.Right.state() == Button_Class::PRESSED);
    const bool a = _edge(I_A, b.A.state() == Button_Class::PRESSED);
    for (int i = 0; i < 2; ++i) {
        const bool edge = _edge(i, dirs[i]);
        if (edge) _rep_at[i] = now + REPEAT_DELAY;
        const bool repeat = dirs[i] && !edge && static_cast<int32_t>(now - _rep_at[i]) >= 0;
        if (repeat) _rep_at[i] = now + REPEAT_EVERY;
        if (_screen == ACTIONS && edge) {
            _action = std::clamp(_action + (i == I_UP ? -1 : 1), 0, _tab == BLE ? 1 : 2);
            _dirty = true;
        }
        if (_screen == MONITOR && edge && !wifi_packet_monitor::running()) {
            _monitor_option = std::clamp(_monitor_option + (i == I_UP ? -1 : 1), 0, 2);
            _dirty = true;
        }
        if (_screen != SURVEY || (_tab != WIFI && _tab != BLE) || (!edge && !repeat)) continue;
        const int index = _off + _sel + (i == I_UP ? -1 : 1);
        if (index < 0 || index >= _count) continue;
        if (index < _off) _off = index;
        else if (index >= _off + VIS) _off = index - VIS + 1;
        _sel = index - _off;
        _dirty = true;
    }
    if (_screen == SURVEY && (left || right)) {
        const auto next = static_cast<Tab>((_tab + (left ? TAB_COUNT - 1 : 1)) % TAB_COUNT);
        if (_tab == BLE || next == BLE) {
            _leaveTab();
            _enterTab(next);
        } else {
            _tab = next;
            _dirty = true;
        }
    } else if (_screen == MONITOR && (left || right) && !wifi_packet_monitor::running()) {
        _monitorAdjust(left ? -1 : 1);
    }
    if (a) {
        if (_screen == SURVEY && _tab == BLE && _state == ST_BT_ERR) {
            /* Retry is an explicit reacquire: stale callbacks are fenced by _stopBle(). */
            _leaveTab(); _enterTab(BLE); _dirty = true;
        } else
        if (_screen == SURVEY && (_tab == WIFI || _tab == BLE)) {
            _sortClamp();
            _trackSelection();
        } else if (_screen == ACTIONS && _action == 0) {
            _paused = !_paused;
            g_ble_paused = _paused;
            if (!_paused && _tab == BLE) { tableClear(); _next_snap = millis(); }
            _message[0] = 0;
        } else if (_screen == ACTIONS && _action == 1) {
            _exportCsv();
        } else if (_screen == ACTIONS) {
            _screen = MONITOR;
            _monitor_option = 0;
            _message[0] = 0;
        } else if (_screen == MONITOR) {
            if (wifi_packet_monitor::running()) {
                wifi_packet_monitor::stop();
            } else if (_wifi_scanning) {
                _setMessage("Wait for the survey scan to finish", hp::COL_WARN);
            } else {
                _message[0] = 0;
                wifi_packet_monitor::start(_monitor_channel, _monitor_filter, _monitor_save);
            }
        }
        _dirty = true;
    }
    if (_screen == MONITOR) {
        if (wifi_packet_monitor::running()) {
            wifi_packet_monitor::tick();
            if (!wifi_packet_monitor::running() || now - _monitor_redraw >= MONITOR_DRAW_MS) {
                _monitor_redraw = now;
                _dirty = true;
            }
        } else if (_wifi_scanning) {
            // Let an existing passive survey finish. The monitor never starts one.
            _wifiTick(now);
        }
    } else if (_tab == BLE) {
        _bleTick(now);
    } else {
        _wifiTick(now);
    }

    if (_dirty && now - _last_draw >= DRAW_MIN_MS) { _dirty = false; _last_draw = now; _draw(); }
    delay(1);
}

void AppRadioScanner::onClose() {
    wifi_packet_monitor::stop();
    _leaveTab();
    // WiFi.scanNetworks() may have switched the radio into STA; put back what we found.
    if (!ui_wifi_bridge_radio_busy() && static_cast<int>(WiFi.getMode()) != _saved_mode)
        WiFi.mode(static_cast<wifi_mode_t>(_saved_mode));
    delete _r; _r = nullptr;
    _device->Lcd.fillScreen(TFT_BLACK);
}
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Radio Scanner: passive Wi-Fi and BLE site survey. */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "../app_common/hp_ui.h"
#include "wifi_packet_monitor.h"

namespace MOONCAKE::APPS {
class AppRadioScanner : public mooncake::AppAbility {
public:
    explicit AppRadioScanner(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    struct Rows;  // heap-owned only while the app is open
    DEVICES* _device;
    Rows* _r = nullptr;
    enum Tab { WIFI, CHANNELS, SCOUT, BLE, TAB_COUNT };
    enum Screen { SURVEY, SIGNAL, ACTIONS, MONITOR };
    Tab _tab = WIFI;
    Screen _screen = SURVEY;
    uint8_t _target_key[6] = {};
    uint8_t _target_type = 0;
    int8_t _history[16] = {};
    int _history_head = 0, _history_count = 0;
    uint32_t _target_seen = 0;
    bool _target_present = false;
    bool _b_down = false;
    uint32_t _b_started = 0;
    int _action = 0;
    int _monitor_option = 0;
    uint8_t _monitor_channel = 0;
    wifi_packet_monitor::Filter _monitor_filter = wifi_packet_monitor::Filter::All;
    bool _monitor_save = false;
    uint32_t _monitor_redraw = 0;
    char _message[48] = {};
    uint16_t _message_color = 0;
    bool _paused = false, _dirty = true;
    int _count = 0, _sel = 0, _off = 0;
    int _state = 0;               // per-tab status code, see cpp
    bool _prev[5] = {};
    uint32_t _rep_at[2] = {};
    uint32_t _next_scan = 0, _next_snap = 0, _last_draw = 0;
    int _saved_mode = 0;          // WiFi.getMode() at open
    bool _wifi_scanning = false;
    uint32_t _lease = 0;          // bluetooth_manager::Lease
    void _enterTab(Tab tab);
    void _leaveTab();
    void _wifiTick(uint32_t now);
    void _bleTick(uint32_t now);
    void _stopWifiScan();
    void _stopBle();
    void _sortClamp();
    void _draw();
    void _drawChannels();
    void _drawScout();
    void _drawHistory();
    void _drawActions();
    void _drawMonitor();
    void _monitorAdjust(int direction);
    void _addSample(int8_t rssi, uint32_t seen);
    void _trackSelection();
    bool _exportCsv();
    void _setMessage(const char* text, uint16_t color);
    bool _edge(int idx, bool down);
};
}

/** Radio Scanner: passive WiFi + BLE site-survey (observation only, never transmits). */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "../app_common/hp_ui.h"

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
    int _tab = 0;                 // 0 = WiFi, 1 = BLE
    bool _paused = false, _dirty = true;
    int _count = 0, _sel = 0, _off = 0;
    int _state = 0;               // per-tab status code, see cpp
    bool _prev[6] = {};
    uint32_t _hold_at[2] = {}, _rep_at[2] = {};
    uint32_t _next_scan = 0, _next_snap = 0, _last_draw = 0;
    int _saved_mode = 0;          // WiFi.getMode() at open
    bool _wifi_scanning = false;
    uint32_t _lease = 0;          // bluetooth_manager::Lease
    void _enterTab(int tab);
    void _leaveTab();
    void _wifiTick(uint32_t now);
    void _bleTick(uint32_t now);
    void _stopWifiScan();
    void _stopBle();
    void _sortClamp();
    void _draw();
    bool _edge(int idx, bool down);
};
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Media Remote: BLE HID consumer-control remote (shared ble_remote_hid transport). */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "../app_common/hp_ui.h"

namespace MOONCAKE::APPS {
class AppMediaRemote : public mooncake::AppAbility {
public:
    explicit AppMediaRemote(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    DEVICES* _device;
    bool _prev[6] = {};
    bool _a_armed = false, _b_armed = false;
    uint32_t _b_since = 0, _hold_since[2] = {}, _rep_at[2] = {};
    uint32_t _last_ui = 0, _msg_at = 0;
    int _status = -1;
    char _msg[28] = {};
    char _drawn_msg[28] = {};
    bool _msg_dirty = true;
    void _drawStatic();
    void _status_ui();
    void _say(const char* text);
    void _media(uint16_t bits, const char* label);
    bool _edge(int idx, bool down);
};
}

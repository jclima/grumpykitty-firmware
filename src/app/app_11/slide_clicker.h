// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Slide Clicker: BLE HID presentation remote (shared ble_remote_hid transport). */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "../app_common/hp_ui.h"

namespace MOONCAKE::APPS {
class AppSlideClicker : public mooncake::AppAbility {
public:
    explicit AppSlideClicker(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    DEVICES* _device;
    bool _prev[6] = {};
    bool _a_armed = false, _b_armed = false, _touch_down = false;
    bool _timer_running = true;
    uint32_t _b_since = 0, _timer_start = 0, _timer_frozen = 0;
    uint32_t _last_ui = 0, _msg_at = 0;
    int _status = -1;
    int32_t _drawn_secs = -1;
    char _msg[28] = {};
    char _drawn_msg[28] = {};
    bool _msg_dirty = true;
    void _drawStatic();
    void _ui();
    void _say(const char* text);
    void _key(uint8_t usage, const char* label);
    bool _edge(int idx, bool down);
    void _touch(uint32_t now);
    uint32_t _elapsedSecs(uint32_t now) const;
};
}

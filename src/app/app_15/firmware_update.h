// SPDX-License-Identifier: MIT
#pragma once

#include <mooncake.h>
#include "../../bsp/devices.h"
#include "../app_common/hp_ui.h"

namespace MOONCAKE::APPS {

/** User-facing wrapper around the asynchronous OTA updater. */
class AppFirmwareUpdate : public mooncake::AppAbility {
public:
    explicit AppFirmwareUpdate(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    DEVICES* _device;
    bool _prevA = false;
    const char* _hint = nullptr;
    uint32_t _lastDraw = 0;

    void _draw();
    bool _powerOk();
};

}  // namespace MOONCAKE::APPS

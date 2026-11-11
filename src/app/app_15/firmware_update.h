// SPDX-License-Identifier: MIT
#pragma once

#include <mooncake.h>
#include <LovyanGFX.hpp>
#include "../../bsp/devices.h"
#include "../../system/ota_updater.h"
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
    LGFX_Sprite _frame;
    bool _frameUsable = false;
    bool _frameReady = false;
    ota_updater::Status _lastStatus{};
    const char* _lastMessage = nullptr;

    void _draw();
    void _drawFull(lgfx::LGFXBase& lcd, const ota_updater::Status& st);
    bool _powerOk();
};

}  // namespace MOONCAKE::APPS

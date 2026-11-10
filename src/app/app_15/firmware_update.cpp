// SPDX-License-Identifier: MIT

#include "firmware_update.h"

#include "../../system/ota_updater.h"
#include "../../system/power_mgmt.h"
#include "../../system/settings_bridge.h"
#include "../../bsp/config.h"

#include <Arduino.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>

namespace {
void put(lgfx::LGFXBase& lcd, int x, int y, const char* text, uint16_t color) {
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(color, hp::COL_BG);
    lcd.setCursor(x, y);
    lcd.print(text ? text : "");
}

const char* stateName(ota_updater::State state) {
    using ota_updater::State;
    switch (state) {
        case State::Checking: return "CHECKING";
        case State::Available: return "AVAILABLE";
        case State::Downloading: return "DOWNLOADING";
        case State::Staged: return "STAGED";
        case State::UpToDate: return "UP TO DATE";
        case State::Failed: return "FAILED";
        default: return "READY";
    }
}

uint16_t stateColor(ota_updater::State state) {
    using ota_updater::State;
    if (state == State::Failed) return hp::COL_ERR;
    if (state == State::Available || state == State::Staged) return hp::COL_INFO;
    if (state == State::Downloading || state == State::Checking) return hp::COL_WARN;
    return hp::COL_FG;
}
}  // namespace

namespace MOONCAKE::APPS {

AppFirmwareUpdate::AppFirmwareUpdate(DEVICES* device) : _device(device) {
    setAppInfo().name = "FW Update";
}

void AppFirmwareUpdate::onOpen() {
    ota_updater::cancel();
    _hint = nullptr;
    _lastDraw = 0;

    auto& b = _device->button;
    b.update();
    _prevA = b.A.state() == Button_Class::PRESSED;
    hp::drawChrome(_device->Lcd);
    _draw();
}

bool AppFirmwareUpdate::_powerOk() {
    if (!power_is_ready()) return false;
    return power_vbus_present() || power_battery_pct() >= 30;
}

void AppFirmwareUpdate::onClose() {
    // This cancels an in-flight worker and discards an unactivated staged image.
    // It deliberately does not activate or otherwise change the boot slot.
    ota_updater::cancel();
}

void AppFirmwareUpdate::_draw() {
    const ota_updater::Status st = ota_updater::status();
    auto& lcd = _device->Lcd;
    hp::clearContent(lcd);
    hp::drawHeader(lcd, "FW UPDATE", stateName(st.state), stateColor(st.state));

    char line[64];
    snprintf(line, sizeof(line), "Installed: v%s", GK_VERSION);
    put(lcd, 10, 34, line, hp::COL_FG);

    if (st.version[0]) {
        snprintf(line, sizeof(line), "Target:    %s", st.version);
        put(lcd, 10, 56, line, hp::COL_ACCENT);
    } else {
        put(lcd, 10, 56, "Target:    --", hp::COL_DIM);
    }

    if (st.state == ota_updater::State::Downloading) {
        const float pct = st.total ? float(st.written) / float(st.total) : 0.0f;
        hp::drawProgress(lcd, 10, 92, 300, pct, hp::COL_INFO);
        snprintf(line, sizeof(line), "%lu / %lu KB", (unsigned long)(st.written / 1024),
                 (unsigned long)(st.total / 1024));
        put(lcd, 10, 116, line, hp::COL_FG);
    } else if (st.state == ota_updater::State::Staged) {
        put(lcd, 10, 92, "Ready to restart", hp::COL_INFO);
    }

    const char* message = _hint ? _hint : st.message;
    if (message) put(lcd, 10, 160, message, st.state == ota_updater::State::Failed ? hp::COL_ERR : hp::COL_DIM);

    const char* left = "A Check";
    if (st.state == ota_updater::State::Available) left = "A Download";
    else if (st.state == ota_updater::State::Staged) left = "A Restart";
    else if (st.state == ota_updater::State::Checking || st.state == ota_updater::State::Downloading) left = "A Wait";
    hp::drawFooter(lcd, left, "Hold B:Exit");
}

void AppFirmwareUpdate::onRunning() {
    const uint32_t now = millis();
    const bool a = _device->button.A.state() == Button_Class::PRESSED;
    const bool aEdge = a && !_prevA;
    _prevA = a;

    const ota_updater::Status st = ota_updater::status();
    if (aEdge) {
        _hint = nullptr;
        if (st.state == ota_updater::State::Idle || st.state == ota_updater::State::UpToDate ||
            st.state == ota_updater::State::Failed) {
            ota_updater::check();
        } else if (st.state == ota_updater::State::Available) {
            if (!_powerOk()) _hint = "Charge to 30% or plug in USB";
            else ota_updater::download();
        } else if (st.state == ota_updater::State::Staged) {
            if (!_powerOk()) {
                _hint = "Charge to 30% or plug in USB";
            } else {
                settings_flush();
                if (ota_updater::activate()) esp_restart();
            }
        }
    }

    if (now - _lastDraw >= 250 || aEdge) {
        _lastDraw = now;
        _draw();
    }
}

}  // namespace MOONCAKE::APPS

/**
 * @file app_04.h
 * @brief App04 — Retro TV (WiFi screen-cast receiver, TCP/JPEG stream)
 */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include "../../ui/ui_wifi_bridge.h"
#include "ScreenShotReceiver/TCPReceiver.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    class App04 : public AppAbility {
    public:
        enum class State { IDLE, CONNECTING, READY, RUNNING };

        App04(DEVICES* device);
        ~App04();
        void onOpen()    override;
        void onRunning() override;
        void onClose()   override;

    private:
        DEVICES*     _device       = nullptr;
        TCPReceiver* _recv         = nullptr;
        bool         _isClosing    = false;
        State        _state        = State::IDLE;
        uint32_t     _connectStart = 0;
        char         _savedSsid[33] = {};
        char         _ip[20]        = {};

        void _drawAll();
        void _startReceiver();
        void _stopReceiver();
        bool         _bLatched     = false;
        static bool _pollB(void* self);
    };
}

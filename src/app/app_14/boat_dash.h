// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Boat Dash: read-only Signal K dashboard (pages of 2x2 tiles, alarm takeover). */
#pragma once
#include <mooncake.h>

#include <vector>

#include "../../bsp/devices.h"
#include "../app_common/hp_ui.h"
#include "sk_client.h"
#include "sk_model.h"

namespace MOONCAKE::APPS {
class AppBoatDash : public mooncake::AppAbility {
public:
    explicit AppBoatDash(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum View : uint8_t { V_NONE, V_CFGERR, V_WIFIOFF, V_NOSSID, V_OFFLINE, V_JOINING, V_DASH };
    struct VPage {
        char title[28];
        int idx[sk::kMaxPageItems];  // snapshot indexes
        int n;
        bool status;
    };
    struct TileSig {
        char display[28];
        sk::Sev sev;
        bool stale, missing, blink;
        uint32_t age;
    };

    void _loadConfig();
    void _buildPages();
    View _view() const;
    void _drawMessage(View v);
    void _drawPage(bool full, uint32_t now);
    void _drawTile(int slot, const sk::SnapItem& it, const char* label, bool unackedAlarm, bool blinkOn, bool acked);
    void _drawStatus(const sk::SnapItem* items, uint32_t now);
    void _processAlarms(uint32_t now);
    int _pageOf(int item) const;
    const char* _labelOf(int item) const;

    DEVICES* _device;
    sk::Config _cfg;
    sk::Client _client;
    sk::Snapshot _snap;
    sk::ClientStatus _st;
    std::vector<VPage> _pages;
    char _cfgErr[200] = "";
    char _cfgSrc[16] = "defaults";
    char _ssid[34] = "";
    uint32_t _errUntil = 0;
    View _shown = V_NONE;
    int _page = 0;
    bool _prev[4] = {};
    bool _aArmed = false, _bArmed = false;
    uint32_t _bSince = 0;
    uint32_t _nextSnap = 0, _nextSound = 0, _lastStatus = 0;
    bool _needFull = true;
    bool _soundOn = false;
    sk::Sev _prevSev[sk::kMaxItems] = {};
    bool _acked[sk::kMaxItems] = {};
    TileSig _sig[4] = {};
    bool _sigValid[4] = {};
    char _hdrSig[40] = "";
    char _ftrSig[48] = "";
};
}  // namespace MOONCAKE::APPS

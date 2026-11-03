// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Boat Dash: read-only Signal K dashboard. All network work lives in sk_client (worker task). */
#include "boat_dash.h"

#include "sk_defaults.h"

#include "../../system/persist.h"
#include "../../system/settings_bridge.h"
#include "../../system/system_sound.h"
#include "../../ui/ui_theme.h"
#include "../../ui/ui_wifi_bridge.h"
#include "../app_common/sd_guard.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr char kConfigPath[] = "/grumpykitty/signalk.json";
constexpr size_t kMaxConfigBytes = 16384;
constexpr uint32_t kErrShowMs = 3000, kSnapEveryMs = 250, kFullEveryMs = 1000, kAlarmBeepMs = 350;
constexpr uint32_t kBlinkMs = 400;
enum Idx { I_LEFT, I_RIGHT, I_A, I_B };

constexpr int TILE_W = 148, TILE_H = 84, TILE_X0 = 8, TILE_Y0 = 30, TILE_GX = 156, TILE_GY = 90;
constexpr uint16_t COL_TILE = THEME_SURFACE_565;
constexpr uint16_t COL_TILE_DIM = THEME_BG_ALT_565;
constexpr uint16_t COL_TEXT = THEME_TEXT_565;
constexpr uint16_t COL_MUTED = THEME_TEXT_DIM_565;
constexpr uint16_t COL_NEUTRAL = THEME_BORDER_565;
constexpr uint16_t COL_GOOD = THEME_OK_565;
constexpr uint16_t COL_BAD = THEME_DANGER_565;
constexpr uint16_t COL_WHITE = 0xFFFF;

void formatAge(char* out, size_t n, bool have, uint32_t ms) {
    if (!have) { snprintf(out, n, "--"); return; }
    const uint32_t s = ms / 1000;
    if (s < 60) snprintf(out, n, "%us", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%um", (unsigned)(s / 60));
    else snprintf(out, n, "%uh", (unsigned)(s / 3600));
}

// Split `msg` into lines of at most `cols` chars (break at spaces when possible).
int wrapLines(const char* msg, int cols, char lines[][40], int maxLines) {
    int n = 0;
    const char* p = msg;
    while (*p && n < maxLines) {
        size_t len = strlen(p);
        size_t take = std::min<size_t>(len, size_t(cols));
        if (take < len) {
            size_t br = take;
            while (br > 0 && p[br] != ' ') --br;
            if (br > 0) take = br;
        }
        memcpy(lines[n], p, take);
        lines[n][take] = 0;
        ++n;
        p += take;
        while (*p == ' ') ++p;
    }
    return n;
}

bool sigEqual(const char* a, const char* b) { return strcmp(a, b) == 0; }
}  // namespace

namespace MOONCAKE::APPS {

AppBoatDash::AppBoatDash(DEVICES* device) : _device(device) {
    setAppInfo().name = "Boat Dash";
}

// ───────────────────────── config / pages ─────────────────────────

void AppBoatDash::_loadConfig() {
    _cfg = sk::Config();
    _cfgErr[0] = 0;
    snprintf(_cfgSrc, sizeof(_cfgSrc), "defaults");
    _errUntil = 0;

    if (sd_guard::check() == sd_guard::Status::Ok && SD_MMC.exists(kConfigPath)) {
        File f = SD_MMC.open(kConfigPath, FILE_READ);
        if (f && !f.isDirectory()) {
            const size_t sz = f.size();
            std::string text;
            bool readOk = false;
            if (sz > 0 && sz <= kMaxConfigBytes) {
                text.resize(sz);
                readOk = f.read(reinterpret_cast<uint8_t*>(&text[0]), sz) == int(sz);
            } else {
                snprintf(_cfgErr, sizeof(_cfgErr), "%s is %s", kConfigPath, sz ? "over 16 KB" : "empty");
            }
            f.close();
            if (readOk) {
                std::string err;
                if (sk::parseConfig(text.data(), text.size(), _cfg, err)) {
                    snprintf(_cfgSrc, sizeof(_cfgSrc), "SD file");
                    if (_cfg.instruments.empty() && _cfg.notifications.empty() && _cfg.pages.empty()) {
                        // A server-only file: keep its server, inherit the built-in tiles and pages.
                        sk::Config def;
                        std::string derr;
                        if (sk::parseConfig(sk::kDefaultConfigJson, sizeof(sk::kDefaultConfigJson) - 1, def, derr)) {
                            _cfg.instruments = def.instruments;
                            _cfg.notifications = def.notifications;
                            _cfg.pages = def.pages;
                        }
                    }
                    return;
                }
                snprintf(_cfgErr, sizeof(_cfgErr), "%s", err.c_str());
                _cfg = sk::Config();
            }
        } else if (f) {
            f.close();
        }
    }
    if (_cfgErr[0]) _errUntil = millis() + kErrShowMs;

    std::string err;
    if (!sk::parseConfig(sk::kDefaultConfigJson, sizeof(sk::kDefaultConfigJson) - 1, _cfg, err)) {
        snprintf(_cfgErr, sizeof(_cfgErr), "Built-in config: %s", err.c_str());  // should never happen (host tests)
        _cfg = sk::Config();
        _errUntil = millis() + kErrShowMs;
    }
}

void AppBoatDash::_buildPages() {
    _pages.clear();
    for (const sk::Page& p : _cfg.pages) {
        std::vector<int> ids;
        for (const std::string& id : p.items) {
            const int i = _cfg.findItem(id);
            if (i >= 0) ids.push_back(i);
        }
        if (p.status) {
            VPage v{};
            snprintf(v.title, sizeof(v.title), "%.27s", p.title.c_str());
            v.status = true;
            v.n = int(std::min<size_t>(ids.size(), 3));  // status page: first 3 items as rows
            for (int k = 0; k < v.n; ++k) v.idx[k] = ids[k];
            _pages.push_back(v);
            continue;
        }
        const size_t chunks = std::max<size_t>(1, (ids.size() + 3) / 4);  // 2x2 tiles per page
        for (size_t c = 0; c < chunks; ++c) {
            VPage v{};
            if (chunks > 1) snprintf(v.title, sizeof(v.title), "%.24s %u", p.title.c_str(), (unsigned)(c + 1));
            else snprintf(v.title, sizeof(v.title), "%.27s", p.title.c_str());
            v.n = int(std::min<size_t>(4, ids.size() - std::min(ids.size(), c * 4)));
            for (int k = 0; k < v.n; ++k) v.idx[k] = ids[c * 4 + size_t(k)];
            _pages.push_back(v);
        }
    }
}

int AppBoatDash::_pageOf(int item) const {
    for (size_t p = 0; p < _pages.size(); ++p)
        for (int k = 0; k < _pages[p].n; ++k)
            if (_pages[p].idx[k] == item) return int(p);
    return -1;
}

const char* AppBoatDash::_labelOf(int item) const {
    const size_t ni = _cfg.instruments.size();
    if (size_t(item) < ni) return _cfg.instruments[size_t(item)].label.c_str();
    const size_t j = size_t(item) - ni;
    return j < _cfg.notifications.size() ? _cfg.notifications[j].label.c_str() : "";
}

// ───────────────────────── lifecycle ─────────────────────────

void AppBoatDash::onOpen() {
    _client.stop();  // defensive: onClose is the normal path
    memset(_prev, 0, sizeof(_prev));
    _aArmed = _bArmed = false;
    _bSince = 0;
    _page = 0;
    _shown = V_NONE;
    _nextSnap = _nextSound = _lastStatus = 0;
    _needFull = true;
    _soundOn = false;
    _snap = sk::Snapshot();
    _st = sk::ClientStatus();
    for (int i = 0; i < sk::kMaxItems; ++i) { _prevSev[i] = sk::Sev::Unknown; _acked[i] = false; }
    memset(_sig, 0, sizeof(_sig));
    memset(_sigValid, 0, sizeof(_sigValid));
    _hdrSig[0] = _ftrSig[0] = 0;

    auto& b = _device->button;
    b.update(); b.A.hasChanged(); b.B.hasChanged();
    b.Up.hasChanged(); b.Down.hasChanged(); b.Left.hasChanged(); b.Right.hasChanged();

    persist_get_str(PKEY_WIFI_SSID, _ssid, sizeof(_ssid), "");  // saved by Settings; read once, not per frame
    _loadConfig();
    _buildPages();
    _device->Lcd.fillScreen(hp::COL_BG);
}

void AppBoatDash::onClose() {
    system_sound_stop();
    _soundOn = false;
    _client.stop();  // bounded (<= 3.5 s); a worker stuck in a request frees itself
    _pages.clear();
    _device->Lcd.fillScreen(TFT_BLACK);
}

// ───────────────────────── screens ─────────────────────────

AppBoatDash::View AppBoatDash::_view() const {
    if (_errUntil && int32_t(millis() - _errUntil) < 0) return V_CFGERR;
    if (!settings_get_wifi_en()) return V_WIFIOFF;
    if (ui_wifi_bridge_is_connected()) return V_DASH;
    if (ui_wifi_bridge_get_status() == WIFI_BRIDGE_CONNECTING) return V_JOINING;
    return _ssid[0] ? V_OFFLINE : V_NOSSID;
}

void AppBoatDash::_drawMessage(View v) {
    auto& lcd = _device->Lcd;
    const char* ssid = _ssid;
    char l1[48] = "";
    const char* title = "BOAT DASH";
    const char* foot = "Hold B: Exit";
    uint16_t col = hp::COL_FG;
    char wrapped[5][40];
    int nWrap = 0;
    switch (v) {
    case V_CFGERR:
        title = "CONFIG ERROR"; col = hp::COL_ERR; foot = "Using built-in defaults";
        nWrap = wrapLines(_cfgErr, 36, wrapped, 5);
        break;
    case V_WIFIOFF: snprintf(l1, sizeof(l1), "WiFi disabled in Settings"); col = hp::COL_ERR; break;
    case V_NOSSID: snprintf(l1, sizeof(l1), "Connect WiFi in Settings"); col = hp::COL_ERR; break;
    case V_OFFLINE: snprintf(l1, sizeof(l1), "WiFi offline - A to reconnect"); col = hp::COL_WARN; foot = "[A] Reconnect  Hold B: Exit"; break;
    case V_JOINING: {
        const char* cur = ui_wifi_bridge_get_current_ssid();
        snprintf(l1, sizeof(l1), "Joining %.24s...", (cur && cur[0]) ? cur : ssid);
        break;
    }
    default: break;
    }
    hp::drawChrome(lcd);
    hp::drawHeader(lcd, title);
    hp::drawFooter(lcd, foot, nullptr);
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(col, hp::COL_BG);
    if (nWrap) {
        for (int i = 0; i < nWrap; ++i) { lcd.setCursor(16, 60 + i * 24); lcd.print(wrapped[i]); }
    } else {
        lcd.setCursor(std::max(8, (320 - int(strlen(l1)) * 8) / 2), 100);
        lcd.print(l1);
    }
}

void AppBoatDash::_drawTile(int slot, const sk::SnapItem& it, const char* label, bool unackedAlarm, bool blinkOn, bool acked) {
    auto& lcd = _device->Lcd;
    const int x = TILE_X0 + (slot % 2) * TILE_GX, y = TILE_Y0 + (slot / 2) * TILE_GY;
    const bool live = !it.missing && !it.stale;
    const bool alarm = live && it.sev == sk::Sev::Alarm;

    uint16_t fill = COL_TILE, border = COL_NEUTRAL, fg = COL_TEXT, sub = COL_MUTED;
    if (alarm) {
        const bool lit = !unackedAlarm || blinkOn;
        fill = lit ? COL_BAD : hp::COL_BG;
        border = COL_BAD;
        fg = COL_WHITE; sub = lit ? COL_WHITE : COL_MUTED;
        (void)acked;
    } else if (live && it.sev == sk::Sev::Warn) {
        border = hp::COL_WARN;
    } else if (live && it.sev == sk::Sev::Ok && it.hasThresholds) {
        border = COL_GOOD;
    } else if (!live) {
        fill = COL_TILE_DIM; border = THEME_SURFACE_ALT_565; fg = COL_MUTED;
    }

    lcd.fillRoundRect(x, y, TILE_W, TILE_H, 5, fill);
    lcd.drawRoundRect(x, y, TILE_W, TILE_H, 5, border);
    lcd.drawRoundRect(x + 1, y + 1, TILE_W - 2, TILE_H - 2, 4, border);

    // Value: biggest font that fits, two lines for long strings (position).
    const char* text = it.display;
    const int maxW = TILE_W - 14;
    lcd.setTextColor(fg, fill);
    lcd.setTextDatum(textdatum_t::middle_left);
    if (strlen(text) > 16) {
        lcd.setFont(&fonts::efontCN_16);
        const char* split = nullptr;
        for (const char* p = text; *p; ++p)
            if ((*p == 'N' || *p == 'S') && p[1] == ' ') { split = p + 1; break; }
        char a[28], b[28];
        if (split) {
            snprintf(a, sizeof(a), "%.*s", int(split - text), text);
            snprintf(b, sizeof(b), "%s", split + 1);
        } else {
            snprintf(a, sizeof(a), "%.17s", text);
            snprintf(b, sizeof(b), "%s", strlen(text) > 17 ? text + 17 : "");
        }
        lcd.drawString(a, x + 7, y + 32);
        lcd.drawString(b, x + 7, y + 50);
    } else {
        lcd.setFont(&fonts::DejaVu40);
        if (lcd.textWidth(text) > maxW) lcd.setFont(&fonts::DejaVu24);
        if (lcd.textWidth(text) > maxW) lcd.setFont(&fonts::efontCN_16);
        lcd.drawString(text, x + 7, y + 41);
    }
    lcd.setTextDatum(textdatum_t::top_left);

    // Label (top-left), unit (bottom-right), STALE (bottom-left) drawn last over the value cell.
    lcd.setFont(&fonts::efontCN_16);
    lcd.setTextColor(sub, fill);
    lcd.setCursor(x + 7, y + 4);
    lcd.printf("%.16s", label);
    if (it.unit[0] && !it.notification) {
        const int uw = int(strlen(it.unit)) * 8;
        lcd.setCursor(x + TILE_W - 7 - uw, y + TILE_H - 20);
        lcd.print(it.unit);
    }
    if (!it.missing && it.stale && !it.notification) {
        lcd.setTextColor(hp::COL_WARN, fill);
        lcd.setCursor(x + 7, y + TILE_H - 20);
        lcd.printf("STALE %us", (unsigned)it.ageS);
    }
}

void AppBoatDash::_drawStatus(const sk::SnapItem* items, uint32_t now) {
    auto& lcd = _device->Lcd;
    const VPage& pg = _pages[size_t(_page)];
    hp::clearContent(lcd);
    lcd.setFont(&fonts::efontCN_16);
    int row = 0;
    char buf[48];
    auto put = [&](uint16_t col) {
        lcd.setTextColor(col, hp::COL_BG);
        lcd.setCursor(12, 30 + row * 24);
        lcd.print(buf);
        ++row;
    };
    for (int k = 0; k < pg.n; ++k) {
        const sk::SnapItem& it = items[pg.idx[k]];
        snprintf(buf, sizeof(buf), "%.10s: %.26s%s", _labelOf(pg.idx[k]), it.display, it.stale && !it.missing ? " (stale)" : "");
        uint16_t c = COL_TEXT;
        if (it.missing || it.stale) c = COL_MUTED;
        else if (it.sev == sk::Sev::Alarm) c = hp::COL_ERR;
        else if (it.sev == sk::Sev::Warn) c = hp::COL_WARN;
        else if (it.sev == sk::Sev::Ok && it.hasThresholds) c = COL_GOOD;
        put(c);
    }
    snprintf(buf, sizeof(buf), "Server: %.28s", _st.host[0] ? _st.host : "--");
    put(COL_TEXT);
    snprintf(buf, sizeof(buf), "%.37s", sk::linkText(_st.link));
    put(_st.link == sk::LinkState::Connected ? COL_GOOD : hp::COL_WARN);
    char age[12];
    formatAge(age, sizeof(age), _st.everOk, now - _st.lastOkMs);
    snprintf(buf, sizeof(buf), "Data: %s ago  Cfg: %s", age, _cfgSrc);
    put(COL_TEXT);
    snprintf(buf, sizeof(buf), "Err: %.32s", _st.error[0] ? _st.error : "none");
    put(_st.error[0] ? hp::COL_WARN : COL_MUTED);
}

void AppBoatDash::_drawPage(bool full, uint32_t now) {
    auto& lcd = _device->Lcd;
    const VPage& pg = _pages[size_t(_page)];

    if (full) {
        hp::drawChrome(lcd);
        memset(_sigValid, 0, sizeof(_sigValid));
        _hdrSig[0] = _ftrSig[0] = 0;
    }

    // Mood from every fresh item, not only this page.
    bool anyAlarm = false, anyWarn = false, anyData = false;
    for (size_t i = 0; i < _snap.count; ++i) {
        const sk::SnapItem& it = _snap.items[i];
        if (it.missing || it.stale) continue;
        anyData = true;
        if (it.sev == sk::Sev::Alarm) anyAlarm = true;
        else if (it.sev == sk::Sev::Warn) anyWarn = true;
    }
    const char* mood = anyAlarm ? "HISS!" : anyWarn ? "hmph" : anyData ? "purr" : "zzz";
    const uint16_t mcol = anyAlarm ? hp::COL_ERR : anyWarn ? hp::COL_WARN : anyData ? COL_GOOD : THEME_SURFACE_ALT_565;
    char hs[40];
    snprintf(hs, sizeof(hs), "%s|%s", pg.title, mood);
    if (!sigEqual(hs, _hdrSig)) {
        hp::drawHeader(lcd, pg.title, mood, mcol);
        snprintf(_hdrSig, sizeof(_hdrSig), "%s", hs);
    }

    if (pg.status) {
        // Status rows change slowly: redraw at 1 Hz (or on page entry).
        if (full || now - _lastStatus >= kFullEveryMs) {
            _drawStatus(_snap.items, now);
            _lastStatus = now;
        }
    } else {
        const bool blinkOn = ((now / kBlinkMs) & 1) == 0;
        for (int s = 0; s < 4; ++s) {
            if (s >= pg.n) {
                if (_sigValid[s]) {
                    lcd.fillRect(TILE_X0 + (s % 2) * TILE_GX, TILE_Y0 + (s / 2) * TILE_GY, TILE_W, TILE_H, hp::COL_BG);
                    _sigValid[s] = false;
                }
                continue;
            }
            const int item = pg.idx[s];
            const sk::SnapItem& it = _snap.items[item];
            const bool live = !it.missing && !it.stale;
            const bool unacked = live && it.sev == sk::Sev::Alarm && !_acked[item];
            TileSig sg{};
            snprintf(sg.display, sizeof(sg.display), "%s", it.display);
            sg.sev = it.sev; sg.stale = it.stale; sg.missing = it.missing;
            sg.blink = unacked && blinkOn;
            sg.age = it.stale ? it.ageS : 0;
            const TileSig& o = _sig[s];
            if (_sigValid[s] && sigEqual(o.display, sg.display) && o.sev == sg.sev && o.stale == sg.stale &&
                o.missing == sg.missing && o.blink == sg.blink && o.age == sg.age)
                continue;
            _drawTile(s, it, _labelOf(item), unacked, blinkOn, _acked[item]);
            _sig[s] = sg;
            _sigValid[s] = true;
        }
    }

    // Footer: "host | age" left, "< 2/5 >" right.
    char age[12], left[48], right[16];
    formatAge(age, sizeof(age), _st.everOk, now - _st.lastOkMs);
    const char* h = _st.host[0] ? _st.host : "no server";
    snprintf(left, sizeof(left), "%.20s | %s", h, age);
    snprintf(right, sizeof(right), "< %d/%d >", _page + 1, int(_pages.size()));
    char fs[64];
    snprintf(fs, sizeof(fs), "%s|%s", left, right);
    if (!sigEqual(fs, _ftrSig)) {
        hp::drawFooter(lcd, left, right);
        snprintf(_ftrSig, sizeof(_ftrSig), "%s", fs);
    }
}

// ───────────────────────── alarms ─────────────────────────

void AppBoatDash::_processAlarms(uint32_t now) {
    int active = -1;
    for (size_t i = 0; i < _snap.count; ++i) {
        const sk::SnapItem& it = _snap.items[i];
        const bool alarm = !it.missing && !it.stale && it.sev == sk::Sev::Alarm;
        if (alarm && _prevSev[i] != sk::Sev::Alarm) {  // rising edge
            _acked[i] = false;
            const int p = _pageOf(int(i));
            if (p >= 0 && p != _page) { _page = p; _needFull = true; }
        }
        if (!alarm) _acked[i] = false;
        _prevSev[i] = alarm ? sk::Sev::Alarm : sk::Sev::Unknown;
        if (alarm && !_acked[i] && active < 0) active = int(i);
    }
    if (active >= 0) {
        if (int32_t(now - _nextSound) >= 0) {
            system_sound_play_alert();  // respects the volume setting; ignores the key-click toggle
            _nextSound = now + kAlarmBeepMs;
        }
        _soundOn = true;
    } else if (_soundOn) {
        system_sound_stop();
        _soundOn = false;
    }
}

// ───────────────────────── main loop ─────────────────────────

void AppBoatDash::onRunning() {
    const uint32_t now = millis();
    auto& b = _device->button;
    b.update(); b.tick();
    if (b.B.isLongPress()) { close(); return; }

    const bool st[4] = {
        b.Left.state() == Button_Class::PRESSED, b.Right.state() == Button_Class::PRESSED,
        b.A.state() == Button_Class::PRESSED, b.B.state() == Button_Class::PRESSED };
    bool edge[4];
    for (int i = 0; i < 4; ++i) edge[i] = st[i] && !_prev[i];
    // B acts on a short release; keys held when the app opened are ignored until released once.
    const bool bTap = !st[I_B] && _prev[I_B] && _bArmed && now - _bSince < 1000;
    if (edge[I_B]) _bSince = now;
    if (!st[I_B]) _bArmed = true;
    if (!st[I_A]) _aArmed = true;
    const bool aEdge = edge[I_A] && _aArmed;
    memcpy(_prev, st, sizeof(st));

    const View v = _view();
    if (v != _shown) {
        _shown = v;
        _needFull = true;
        if (v == V_DASH) {
            if (_pages.empty()) {
                snprintf(_cfgErr, sizeof(_cfgErr), "No pages in config");
                _errUntil = now + kErrShowMs;
                _shown = V_NONE;
                return;
            }
            if (!_client.running() && !_client.start(_cfg)) {
                snprintf(_cfgErr, sizeof(_cfgErr), "Out of memory: cannot start Signal K client");
                _errUntil = now + kErrShowMs;
                _shown = V_NONE;
                return;
            }
            _nextSnap = 0;
        } else {
            _drawMessage(v);
        }
    }

    if (v == V_OFFLINE && aEdge) {
        if (_ssid[0]) ui_wifi_bridge_connect_saved(_ssid);
        return;
    }
    if (v != V_DASH) { delay(2); return; }

    // Navigation and actions.
    const int np = int(_pages.size());
    if (edge[I_LEFT]) { _page = (_page + np - 1) % np; _needFull = true; }
    if (edge[I_RIGHT]) { _page = (_page + 1) % np; _needFull = true; }
    if (bTap && _page != 0) { _page = 0; _needFull = true; }
    if (aEdge) {
        int active = -1;
        for (size_t i = 0; i < _snap.count && active < 0; ++i) {
            const sk::SnapItem& it = _snap.items[i];
            if (!it.missing && !it.stale && it.sev == sk::Sev::Alarm && !_acked[i]) active = int(i);
        }
        if (active >= 0) {
            _acked[active] = true;
            _nextSound = now + 2 * kAlarmBeepMs;
            _needFull = true;
        } else {
            _client.refreshNow();
        }
    }

    bool draw = _needFull;
    if (int32_t(now - _nextSnap) >= 0) {
        _nextSnap = now + kSnapEveryMs;
        _client.snapshot(now, _snap, _st);
        _processAlarms(now);
        draw = true;
    }
    if (draw) {
        _drawPage(_needFull, now);  // unchanged tiles/header/footer are skipped by signature
        _needFull = false;
    }
    delay(2);
}

}  // namespace MOONCAKE::APPS

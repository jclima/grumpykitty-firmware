#include "lua_app.h"
#include "lua_sandbox.h"
#include "../app_common/sd_guard.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <lua.hpp>
#include "../app_14/sk_client.h"
#include "../app_14/sk_model.h"
#include "../../ui/ui_wifi_bridge.h"
#include "../../system/settings_bridge.h"
#include "../../system/persist.h"
#include "../../system/system_sound.h"

#include <algorithm>
#include <cstring>

namespace personal_apps {
namespace {
constexpr size_t kMaxScript = 64 * 1024;
struct LuaState {
    bool exited = false;
    bool dirty = false;
    bool ownsWifiAttempt = false;
    sk::Client* signalK = nullptr;
    sk::Config* config = nullptr;
    LGFX_Sprite* frame = nullptr;
    bool allowWifi = false, allowSound = false, allowSignalK = false;
    bool sessionOpened = false;
    std::string configSource, configError;
    std::string packagePath, packageId;
};

LuaState* state(lua_State* L) { return *reinterpret_cast<LuaState**>(lua_getextraspace(L)); }
constexpr uint16_t background = 0x2104, surface = 0x3186;
uint16_t toneColor(const char* tone) {
    if (!strcmp(tone, "alarm")) return 0xF9C7;
    if (!strcmp(tone, "warn")) return 0xFD80;
    if (!strcmp(tone, "ok")) return 0x5E88;
    if (!strcmp(tone, "dim")) return 0x8C71;
    return 0xFFFF;
}
void text(LuaState* s, const char* value, int x, int y, int width, int height,
          uint16_t color = 0xFFFF, bool large = false) {
    char bounded[97];
    snprintf(bounded, sizeof(bounded), "%s", value ? value : "");
    auto& canvas = *s->frame;
    canvas.setClipRect(x, y, width, height);
    canvas.setTextColor(color);
    canvas.setTextSize(1);
    canvas.setFont(large ? &fonts::Font4 : &fonts::Font2);
    if (large && canvas.textWidth(bounded) > width) canvas.setFont(&fonts::Font2);
    canvas.drawString(bounded, x, y);
    canvas.clearClipRect();
    s->dirty = true;
}
void tileValue(LuaState* s, const char* value, int x, int y, uint16_t color) {
    s->frame->setFont(&fonts::Font2); s->frame->setTextSize(1);
    if (s->frame->textWidth(value) <= 142) { text(s, value, x, y, 142, 30, color, true); return; }
    // Position strings and long readings need two lines rather than losing their suffix.
    char first[48], second[48];
    const size_t length = strlen(value);
    size_t split = length / 2;
    for (size_t i = split; i > 0; --i) if (value[i] == ' ') { split = i; break; }
    snprintf(first, sizeof(first), "%.*s", (int)split, value);
    const char* rest = value + split; if (*rest == ' ') ++rest;
    snprintf(second, sizeof(second), "%s", rest);
    text(s, first, x, y, 142, 16, color);
    text(s, second, x, y + 16, 142, 16, color);
}
void stringField(lua_State* L, int table, const char* key, char* output, size_t capacity) {
    table = lua_absindex(L, table);
    lua_pushstring(L, key);
    lua_rawget(L, table);
    const char* value = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
    snprintf(output, capacity, "%s", value);
    lua_pop(L, 1);
}
bool booleanField(lua_State* L, int table, const char* key) {
    table = lua_absindex(L, table);
    lua_pushstring(L, key); lua_rawget(L, table);
    bool result = lua_toboolean(L, -1); lua_pop(L, 1);
    return result;
}
int uiClear(lua_State* L) {
    auto* s = state(L); s->frame->fillScreen(background); s->dirty = true; return 0;
}
int uiHeader(lua_State* L) {
    auto* s = state(L);
    const char* title = luaL_optstring(L, 1, "Personal app");
    const char* status = luaL_optstring(L, 2, "");
    const uint16_t color = toneColor(luaL_optstring(L, 3, "normal"));
    s->frame->fillRect(0, 0, 320, 28, surface);
    text(s, title, 6, 5, 210, 22);
    text(s, status, 220, 5, 94, 22, color);
    return 0;
}
int uiTiles(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* s = state(L);
    s->frame->fillRect(0, 28, 320, 194, background); s->dirty = true;
    const size_t count = std::min<size_t>(4, lua_rawlen(L, 1));
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 1, i + 1);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
        char label[48], value[48], unit[16], tone[16];
        stringField(L, -1, "label", label, sizeof(label));
        stringField(L, -1, "value", value, sizeof(value));
        stringField(L, -1, "unit", unit, sizeof(unit));
        stringField(L, -1, "tone", tone, sizeof(tone));
        const bool stale = booleanField(L, -1, "stale"), blink = booleanField(L, -1, "blink");
        lua_pop(L, 1);
        const uint16_t color = toneColor(stale ? "dim" : tone);
        const int x = (i % 2) * 156 + 6, y = (i / 2) * 94 + 32;
        s->frame->drawRoundRect(x, y, 152, 88, 5, color);
        text(s, label, x + 5, y + 4, 142, 20, color);
        tileValue(s, value[0] ? value : "--", x + 5, y + 27, blink ? background : color);
        text(s, stale ? "Stale" : unit, x + 5, y + 62, 142, 20, color);
    }
    return 0;
}
int uiRows(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* s = state(L); s->frame->fillRect(0, 28, 320, 194, background); s->dirty = true;
    const size_t count = std::min<size_t>(7, lua_rawlen(L, 1));
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 1, i + 1);
        if (lua_istable(L, -1)) {
            char value[97], tone[16];
            stringField(L, -1, "text", value, sizeof(value));
            stringField(L, -1, "tone", tone, sizeof(tone));
            text(s, value, 6, 33 + i * 26, 308, 24, toneColor(tone));
        }
        lua_pop(L, 1);
    }
    return 0;
}
int uiFooter(lua_State* L) {
    auto* s = state(L);
    const char* left = luaL_optstring(L, 1, ""), *right = luaL_optstring(L, 2, "");
    s->frame->fillRect(0, 222, 320, 18, surface);
    // Compact footer uses the built-in 6x8 font for both control hints.
    s->frame->setFont(&fonts::Font0); s->frame->setTextSize(1); s->frame->setTextColor(0xFFFF);
    char bounded[27]; snprintf(bounded, sizeof(bounded), "%s", left);
    s->frame->setClipRect(5, 225, 150, 14); s->frame->drawString(bounded, 5, 227);
    snprintf(bounded, sizeof(bounded), "%s", right);
    s->frame->setClipRect(160, 225, 155, 14); s->frame->drawRightString(bounded, 315, 227);
    s->frame->clearClipRect(); s->dirty = true; return 0;
}
int uiMessage(lua_State* L) {
    auto* s = state(L);
    const char* title = luaL_optstring(L, 1, "Personal app");
    luaL_checktype(L, 2, LUA_TTABLE);
    const uint16_t color = toneColor(luaL_optstring(L, 3, "normal"));
    s->frame->fillScreen(background);
    text(s, title, 6, 5, 308, 24, color);
    const size_t count = std::min<size_t>(7, lua_rawlen(L, 2));
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 2, i + 1);
        if (lua_type(L, -1) == LUA_TSTRING) text(s, lua_tostring(L, -1), 6, 38 + i * 25, 308, 24);
        lua_pop(L, 1);
    }
    return 0;
}
int exitApp(lua_State* L) { state(L)->exited = true; return 0; }
int wifiStatus(lua_State* L) {
    auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L));
    lua_newtable(L);
    if (!s->allowWifi) { lua_pushboolean(L, false); lua_setfield(L, -2, "enabled"); return 1; }
    bool connected = ui_wifi_bridge_is_connected(); auto st = ui_wifi_bridge_get_status();
    lua_pushboolean(L, settings_get_wifi_en()); lua_setfield(L, -2, "enabled"); lua_pushboolean(L, connected); lua_setfield(L, -2, "connected"); lua_pushboolean(L, st == WIFI_BRIDGE_CONNECTING); lua_setfield(L, -2, "connecting");
    char saved[34] = ""; persist_get_str(PKEY_WIFI_SSID, saved, sizeof(saved), ""); lua_pushstring(L, saved); lua_setfield(L, -2, "saved_ssid"); lua_pushstring(L, ui_wifi_bridge_get_current_ssid()); lua_setfield(L, -2, "current_ssid");
    return 1;
}
int wifiReconnect(lua_State* L) {
    auto* s = state(L);
    if (!s->allowWifi || !settings_get_wifi_en() || ui_wifi_bridge_is_connected() ||
        ui_wifi_bridge_get_status() == WIFI_BRIDGE_CONNECTING) return 0;
    char ssid[34] = ""; persist_get_str(PKEY_WIFI_SSID, ssid, sizeof(ssid), "");
    if (ssid[0]) { ui_wifi_bridge_connect_saved(ssid); s->ownsWifiAttempt = true; }
    return 0;
}
int soundAlert(lua_State* L) { auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L)); if (s->allowSound) system_sound_play_alert(); return 0; }
int soundStop(lua_State* L) { auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L)); if (s->allowSound) system_sound_stop(); return 0; }
int sessionClose(lua_State* L) {
    auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L));
    if (!s->allowSignalK) { lua_pushnil(L); lua_pushliteral(L, "signalk capability not declared"); return 2; }
    if (s->signalK) s->signalK->stop(0);
    return 0;
}
int sessionRefresh(lua_State* L) { auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L)); if (s->signalK) s->signalK->refreshNow(); return 0; }
int sessionSnapshot(lua_State* L) {
    auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L));
    if (!s->signalK || !s->config) { lua_pushnil(L); return 1; }
    sk::Snapshot snap; sk::ClientStatus st; s->signalK->snapshot(millis(), snap, st); lua_newtable(L);
    lua_newtable(L); for (size_t i = 0; i < snap.count; ++i) {
        const char* id = i < s->config->instruments.size() ? s->config->instruments[i].id.c_str() : s->config->notifications[i - s->config->instruments.size()].id.c_str();
        lua_newtable(L); lua_pushstring(L, id); lua_setfield(L, -2, "id"); lua_pushstring(L, snap.items[i].display); lua_setfield(L, -2, "display"); lua_pushstring(L, snap.items[i].unit); lua_setfield(L, -2, "unit"); lua_pushstring(L, sk::sevName(snap.items[i].sev)); lua_setfield(L, -2, "severity");
        lua_pushboolean(L, snap.items[i].missing); lua_setfield(L, -2, "missing"); lua_pushboolean(L, snap.items[i].stale); lua_setfield(L, -2, "stale"); lua_pushboolean(L, snap.items[i].notification); lua_setfield(L, -2, "notification"); lua_pushboolean(L, snap.items[i].hasThresholds); lua_setfield(L, -2, "has_thresholds"); lua_pushinteger(L, snap.items[i].ageS); lua_setfield(L, -2, "age_s"); lua_rawseti(L, -2, i + 1);
    } lua_setfield(L, -2, "items"); lua_pushboolean(L, snap.connected); lua_setfield(L, -2, "connected");
    lua_newtable(L); lua_pushstring(L, st.host); lua_setfield(L, -2, "host"); lua_pushstring(L, sk::linkText(st.link)); lua_setfield(L, -2, "link"); lua_pushstring(L, st.error); lua_setfield(L, -2, "error"); lua_pushboolean(L, st.everOk); lua_setfield(L, -2, "ever_ok"); lua_pushinteger(L, st.everOk ? millis() - st.lastOkMs : 0); lua_setfield(L, -2, "age_ms"); lua_setfield(L, -2, "status"); return 1;
}
bool loadConfig(const std::string&, const std::string&, sk::Config&, LuaState&);
int signalkOpen(lua_State* L) {
    auto* s = *reinterpret_cast<LuaState**>(lua_getextraspace(L));
    if (!s->allowSignalK) { lua_pushnil(L); lua_pushliteral(L, "signalk capability required"); return 2; }
    if (s->sessionOpened || sk::workerCleanupPending()) { lua_pushnil(L); lua_pushliteral(L, "Only one session per app launch"); return 2; }
    const char* defaultsName = luaL_optstring(L, 1, "defaults.json");
    const char* overrideName = luaL_optstring(L, 2, "signalk.json");
    if (strcmp(defaultsName, "defaults.json") != 0 || strcmp(overrideName, "signalk.json") != 0) { lua_pushnil(L); lua_pushliteral(L, "configuration filenames must be defaults.json/signalk.json"); return 2; }
    if (!loadConfig(s->packagePath, s->packageId, *s->config, *s)) {
        lua_pushnil(L); lua_pushstring(L, s->configError.c_str()); return 2;
    }
    if (!s->signalK->start(*s->config)) { lua_pushnil(L); lua_pushliteral(L, "Signal K session already running"); return 2; }
    s->sessionOpened = true;
    lua_newtable(L); lua_pushlightuserdata(L, s); lua_pushcclosure(L, sessionClose, 1); lua_setfield(L, -2, "close"); lua_pushlightuserdata(L, s); lua_pushcclosure(L, sessionRefresh, 1); lua_setfield(L, -2, "refresh"); lua_pushlightuserdata(L, s); lua_pushcclosure(L, sessionSnapshot, 1); lua_setfield(L, -2, "snapshot");
    lua_newtable(L); lua_newtable(L); for (size_t i = 0; i < s->config->instruments.size(); ++i) { lua_newtable(L); lua_pushstring(L, s->config->instruments[i].id.c_str()); lua_setfield(L, -2, "id"); lua_pushstring(L, s->config->instruments[i].label.c_str()); lua_setfield(L, -2, "label"); lua_rawseti(L, -2, i + 1); } for (size_t i = 0; i < s->config->notifications.size(); ++i) { auto& n = s->config->notifications[i]; lua_newtable(L); lua_pushstring(L, n.id.c_str()); lua_setfield(L, -2, "id"); lua_pushstring(L, n.label.c_str()); lua_setfield(L, -2, "label"); lua_rawseti(L, -2, s->config->instruments.size() + i + 1); } lua_setfield(L, -2, "items"); lua_newtable(L); for (size_t i = 0; i < s->config->pages.size(); ++i) { auto& p = s->config->pages[i]; lua_newtable(L); lua_pushstring(L, p.title.c_str()); lua_setfield(L, -2, "title"); lua_pushboolean(L, p.status); lua_setfield(L, -2, "status"); lua_newtable(L); for (size_t j = 0; j < p.items.size(); ++j) { lua_pushstring(L, p.items[j].c_str()); lua_rawseti(L, -2, j + 1); } lua_setfield(L, -2, "items"); lua_rawseti(L, -2, i + 1); } lua_setfield(L, -2, "pages"); lua_pushstring(L, s->configSource.c_str()); lua_setfield(L, -2, "config_source"); lua_pushstring(L, s->configError.c_str()); lua_setfield(L, -2, "config_error");
    return 2;
}
void fn(lua_State* L, const char* name, lua_CFunction f) { lua_pushcfunction(L, f); lua_setfield(L, -2, name); }

void installApi(lua_State* L) {
    lua_newtable(L); // mk
    lua_newtable(L); // ui
    fn(L, "clear", uiClear); fn(L, "header", uiHeader); fn(L, "footer", uiFooter);
    fn(L, "message", uiMessage); fn(L, "tiles", uiTiles); fn(L, "rows", uiRows);
    lua_setfield(L, -2, "ui");
    lua_newtable(L); fn(L, "status", wifiStatus); fn(L, "reconnect_saved", wifiReconnect); lua_setfield(L, -2, "wifi");
    lua_newtable(L); fn(L, "alert", soundAlert); fn(L, "stop", soundStop); lua_setfield(L, -2, "sound");
    lua_newtable(L); fn(L, "open", signalkOpen); lua_setfield(L, -2, "signalk");
    fn(L, "exit", exitApp);
    lua_setglobal(L, "mk");
    // The base library is useful for utility apps, but these are deliberate escape hatches.
    lua_pushnil(L); lua_setglobal(L, "dofile"); lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load"); lua_pushnil(L); lua_setglobal(L, "require"); lua_pushnil(L); lua_setglobal(L, "pcall"); lua_pushnil(L); lua_setglobal(L, "xpcall"); lua_pushnil(L); lua_setglobal(L, "coroutine");
    lua_pushnil(L); lua_setglobal(L, "collectgarbage");
    lua_pushnil(L); lua_setglobal(L, "setmetatable");
    lua_pushnil(L); lua_setglobal(L, "getmetatable");
    lua_getglobal(L, LUA_STRLIBNAME); lua_pushnil(L); lua_setfield(L, -2, "find"); lua_pushnil(L); lua_setfield(L, -2, "match"); lua_pushnil(L); lua_setfield(L, -2, "gmatch"); lua_pushnil(L); lua_setfield(L, -2, "gsub"); lua_pop(L, 1);
    lua_pushnil(L); lua_setglobal(L, "print");
}

bool readText(const std::string& file, std::string& out, size_t max = kMaxScript) {
    File f = SD_MMC.open(file.c_str(), FILE_READ);
    if (!f || f.isDirectory() || f.size() == 0 || f.size() > max) { if (f) f.close(); return false; }
    out.resize(f.size()); bool ok = f.read(reinterpret_cast<uint8_t*>(&out[0]), out.size()) == static_cast<int>(out.size()); f.close(); return ok;
}
bool readScript(const std::string& path, std::string& out) { return readText(path + "/main.lua", out); }

bool loadConfig(const std::string& packagePath, const std::string& id, sk::Config& cfg, LuaState& state) {
    state.configError.clear();
    state.configSource = "package";
    if (sd_guard::check() != sd_guard::Status::Ok) { state.configError = "SD unavailable"; return false; }
    std::string text, error;
    if (!readText(packagePath + "/defaults.json", text) || !sk::parseConfig(text.data(), text.size(), cfg, error)) {
        state.configError = "defaults.json missing or invalid";
        return false;
    }
    JsonDocument merged;
    if (deserializeJson(merged, text)) { state.configError = "Config memory unavailable"; return false; }
    std::string path = "/appdata/" + id + "/signalk.json";
    bool legacy = false;
    if (!SD_MMC.exists(path.c_str())) {
        if (id != "boat-dash" || !SD_MMC.exists("/grumpykitty/signalk.json")) return true;
        path = "/grumpykitty/signalk.json"; legacy = true;
    }
    if (!readText(path, text)) { state.configError = "Override unreadable; using defaults"; return true; }
    JsonDocument overrideDoc;
    if (deserializeJson(overrideDoc, text) || !overrideDoc.is<JsonObject>()) {
        state.configError = "Override invalid JSON; using defaults"; return true;
    }
    for (JsonPair field : overrideDoc.as<JsonObject>()) merged[field.key()] = field.value();
    text.clear(); serializeJson(merged, text);
    sk::Config candidate;
    if (!sk::parseConfig(text.data(), text.size(), candidate, error)) {
        state.configError = "Override invalid; using defaults"; return true;
    }
    cfg = std::move(candidate);
    state.configSource = legacy ? "legacy" : "appdata";
    return true;
}
} // namespace

struct LuaApp::Impl {
    DEVICES* device;
    Package package;
    std::unique_ptr<LuaSandbox> vm;
    LuaState state;
    sk::Config config;
    sk::Client client;
    std::string source;
    int updateRef = LUA_NOREF, closeRef = LUA_NOREF;
    bool faulted = false, faultDrawn = false, closed = false;
    const char* faultReason = "Script error; check main.lua";
    bool armed[6] = {}, previous[6] = {}, input[6] = {};
    uint32_t now = 0, lastSdCheck = 0;

    explicit Impl(DEVICES* d, const Package& p) : device(d), package(p) {
        state.signalK = &client; state.config = &config;
        state.packagePath = p.path; state.packageId = p.id;
        state.allowWifi = p.wifi; state.allowSound = p.sound; state.allowSignalK = p.signalk;
    }
    void stopResources() {
        client.stop(0);
        if (state.allowSound) system_sound_stop();
        if (state.ownsWifiAttempt && ui_wifi_bridge_get_status() == WIFI_BRIDGE_CONNECTING)
            ui_wifi_bridge_disconnect();
        state.ownsWifiAttempt = false;
    }
    void fail(const char* reason = nullptr) {
        faulted = true;
        if (reason) faultReason = reason;
        else if (vm && vm->error().find("instruction") != std::string::npos) faultReason = "Script exceeded its time budget";
        else if (vm && vm->error().find("memory") != std::string::npos) faultReason = "Script exceeded available memory";
        stopResources();
    }
    void present() {
        if (state.frame && state.dirty) { state.frame->pushSprite(0, 0); state.dirty = false; }
    }
    static int initialize(lua_State* L) {
        auto* self = static_cast<Impl*>(lua_touserdata(L, 1));
        luaL_requiref(L, LUA_GNAME, luaopen_base, 1); lua_pop(L, 1);
        luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
        luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
        luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
        luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
        installApi(L);
        if (luaL_loadbufferx(L, self->source.data(), self->source.size(), self->package.id.c_str(), "t") != LUA_OK)
            return lua_error(L);
        lua_call(L, 0, 1);
        luaL_checktype(L, -1, LUA_TTABLE);
        lua_pushliteral(L, "update"); lua_rawget(L, -2);
        if (!lua_isfunction(L, -1)) return luaL_error(L, "App needs update(now_ms,input)");
        self->updateRef = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_pushliteral(L, "close"); lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) self->closeRef = luaL_ref(L, LUA_REGISTRYINDEX);
        else lua_pop(L, 1);
        lua_pushliteral(L, "open"); lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) lua_call(L, 0, 0);
        return 0;
    }
    static int update(lua_State* L) {
        auto* self = static_cast<Impl*>(lua_touserdata(L, 1));
        lua_rawgeti(L, LUA_REGISTRYINDEX, self->updateRef);
        lua_pushinteger(L, self->now);
        lua_createtable(L, 0, 6);
        const char* names[] = {"left", "right", "up", "down", "a", "b"};
        for (int i = 0; i < 6; ++i) { lua_pushboolean(L, self->input[i]); lua_setfield(L, -2, names[i]); }
        lua_call(L, 2, 0);
        return 0;
    }
    static int finish(lua_State* L) {
        auto* self = static_cast<Impl*>(lua_touserdata(L, 1));
        if (self->closeRef != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, self->closeRef); lua_call(L, 0, 0);
        }
        return 0;
    }
    void sample(bool initialize = false) {
        auto& b = device->button;
        const bool down[] = {b.Left.state() == Button_Class::PRESSED, b.Right.state() == Button_Class::PRESSED,
            b.Up.state() == Button_Class::PRESSED, b.Down.state() == Button_Class::PRESSED,
            b.A.state() == Button_Class::PRESSED, b.B.state() == Button_Class::PRESSED};
        for (int i = 0; i < 6; ++i) {
            input[i] = false;
            if (initialize) { armed[i] = !down[i]; previous[i] = down[i]; continue; }
            if (i == 5) input[i] = armed[i] && previous[i] && !down[i];
            else input[i] = armed[i] && !previous[i] && down[i];
            if (!down[i]) armed[i] = true;
            previous[i] = down[i];
        }
    }
};

LuaApp::LuaApp(DEVICES* d, const Package& p) : impl_(std::make_unique<Impl>(d, p)) { setAppInfo().name = p.name.c_str(); }
LuaApp::~LuaApp() { onClose(); }

void LuaApp::onOpen() {
    auto& self = *impl_;
    self.sample(true);
    if (sd_guard::check() != sd_guard::Status::Ok || !readScript(self.package.path, self.source)) { self.fail("SD app files unavailable"); return; }
    self.state.frame = new (std::nothrow) LGFX_Sprite(&self.device->Lcd);
    if (!self.state.frame) { self.fail("Not enough display memory"); return; }
    self.state.frame->setPsram(true);
    self.state.frame->setColorDepth(16);
    if (!self.state.frame->createSprite(320, 240)) { self.fail("Not enough display memory"); return; }
    self.state.frame->fillScreen(background); self.state.dirty = true;
    self.vm.reset(new (std::nothrow) LuaSandbox());
    if (!self.vm || !self.vm->state()) { self.fail("Not enough script memory"); return; }
    *reinterpret_cast<LuaState**>(lua_getextraspace(self.vm->state())) = &self.state;
    if (!self.vm->call(Impl::initialize, &self)) self.fail();
    self.source.clear(); self.source.shrink_to_fit();
    if (!self.faulted) self.present();
}

void LuaApp::onRunning() {
    auto& self = *impl_;
    if (self.state.exited) { close(); return; }
    if (self.faulted) {
        if (!self.faultDrawn) {
            auto& lcd = self.device->Lcd;
            lcd.fillScreen(background); lcd.setFont(&fonts::Font2); lcd.setTextSize(1);
            lcd.setTextColor(0xFD80); lcd.drawString("Personal app stopped", 8, 30);
            lcd.setTextColor(0xFFFF); lcd.drawString(self.faultReason, 8, 62);
            lcd.drawString("Hold B to exit", 8, 88);
            self.faultDrawn = true;
        }
        return;
    }
    self.now = millis();
    if (self.now - self.lastSdCheck >= 1000) {
        self.lastSdCheck = self.now;
        if (sd_guard::check() != sd_guard::Status::Ok) { self.fail("SD card unavailable"); return; }
    }
    self.sample();
    if (!self.vm->call(Impl::update, &self)) { self.fail(); return; }
    if (self.state.exited) close();
    else self.present();
}

void LuaApp::onClose() {
    if (!impl_ || impl_->closed) return;
    auto& self = *impl_; self.closed = true;
    if (self.vm && self.vm->state() && !self.faulted) self.vm->call(Impl::finish, &self);
    self.stopResources();
    self.vm.reset();
    if (self.state.frame) { self.state.frame->deleteSprite(); delete self.state.frame; self.state.frame = nullptr; }
}
} // namespace personal_apps

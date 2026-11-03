// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

// Shared BLE HID remote transport (keyboard report ID 1 + consumer control
// report ID 2). GATT layout and report map are derived from
// app_08/badusb_ble.cpp; queueing follows app_06/air_mouse_hid.cpp. Everything
// private lives in the anonymous namespace (the link uses -zmuldefs).
#include "ble_remote_hid.h"
#include "../../system/bluetooth_manager.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_bt_device.h>
#include <HIDTypes.h>
#include <atomic>
#include <cstring>

namespace {
constexpr uint16_t APP_ID = 0xB00;
constexpr unsigned QUEUE_LEN = 16, HID_ATTRS = 25;
struct Item { uint8_t kind; uint8_t mods; uint16_t value; };  // kind 0=key, 1=media
std::atomic<bool> active{false}, linked{false}, secure{false}, subscribed{false}, error{false};
std::atomic<bool> bootSubscribed{false}, mediaSubscribed{false};
std::atomic<bool> congested{false}, suspended{false};
std::atomic<unsigned> epoch{0}, services{0};
esp_gatt_if_t iface = ESP_GATT_IF_NONE;
uint16_t conn = 0, handles[2][HID_ATTRS] = {};
uint8_t peer[6] = {};
bluetooth_manager::Lease lease = 0;
unsigned advReady = 0;
uint32_t lastSend = 0, startedAt = 0;
unsigned seenEpoch = 0;
Item queue[QUEUE_LEN] = {};
unsigned head = 0, tail = 0;

uint16_t serviceUuid[] = {0x1812, 0x180A};
uint16_t primary = 0x2800, declaration = 0x2803;
uint16_t infoUuid = 0x2A4A, mapUuid = 0x2A4B, controlUuid = 0x2A4C;
uint16_t protocolUuid = 0x2A4E, reportUuid = 0x2A4D, cccUuid = 0x2902, refUuid = 0x2908;
uint16_t bootUuid = 0x2A22, bootOutUuid = 0x2A32;
uint8_t outputProp = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t keyboardOut = 0, outputRef[] = {1, 2}, mediaRef[] = {2, 1}, media[2] = {}, mediaCcc[2] = {};
uint16_t manufacturerUuid = 0x2A29, pnpUuid = 0x2A50;
uint8_t readProp = ESP_GATT_CHAR_PROP_BIT_READ;
uint8_t writeProp = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t protocolProp = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t reportProp = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
uint8_t info[] = {0x11, 0x01, 0, 2}, control = 0, protocol = 1;
std::atomic<uint8_t> protocolMode{1};
uint8_t report[8] = {}, ccc[2] = {}, ref[] = {1, 1};
uint8_t bootReport[8] = {}, bootCcc[2] = {};
uint8_t manufacturer[] = "MeowKit", pnp[] = {2, 0xE5, 0x11, 0xB0, 0x10, 0x00, 0x01};
uint8_t reportMap[] = {
  USAGE_PAGE(1),      0x01,          // USAGE_PAGE (Generic Desktop Ctrls)
  USAGE(1),           0x06,          // USAGE (Keyboard)
  COLLECTION(1),      0x01,          // COLLECTION (Application)
  // ------------------------------------------------- Keyboard
  REPORT_ID(1),       1,   //   REPORT_ID (1)
  USAGE_PAGE(1),      0x07,          //   USAGE_PAGE (Kbrd/Keypad)
  USAGE_MINIMUM(1),   0xE0,          //   USAGE_MINIMUM (0xE0)
  USAGE_MAXIMUM(1),   0xE7,          //   USAGE_MAXIMUM (0xE7)
  LOGICAL_MINIMUM(1), 0x00,          //   LOGICAL_MINIMUM (0)
  LOGICAL_MAXIMUM(1), 0x01,          //   Logical Maximum (1)
  REPORT_SIZE(1),     0x01,          //   REPORT_SIZE (1)
  REPORT_COUNT(1),    0x08,          //   REPORT_COUNT (8)
  HIDINPUT(1),        0x02,          //   INPUT (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  REPORT_COUNT(1),    0x01,          //   REPORT_COUNT (1) ; 1 byte (Reserved)
  REPORT_SIZE(1),     0x08,          //   REPORT_SIZE (8)
  HIDINPUT(1),        0x01,          //   INPUT (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
  REPORT_COUNT(1),    0x05,          //   REPORT_COUNT (5) ; 5 bits (Num lock, Caps lock, Scroll lock, Compose, Kana)
  REPORT_SIZE(1),     0x01,          //   REPORT_SIZE (1)
  USAGE_PAGE(1),      0x08,          //   USAGE_PAGE (LEDs)
  USAGE_MINIMUM(1),   0x01,          //   USAGE_MINIMUM (0x01) ; Num Lock
  USAGE_MAXIMUM(1),   0x05,          //   USAGE_MAXIMUM (0x05) ; Kana
  HIDOUTPUT(1),       0x02,          //   OUTPUT (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
  REPORT_COUNT(1),    0x01,          //   REPORT_COUNT (1) ; 3 bits (Padding)
  REPORT_SIZE(1),     0x03,          //   REPORT_SIZE (3)
  HIDOUTPUT(1),       0x01,          //   OUTPUT (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
  REPORT_COUNT(1),    0x06,          //   REPORT_COUNT (6) ; 6 bytes (Keys)
  REPORT_SIZE(1),     0x08,          //   REPORT_SIZE(8)
  LOGICAL_MINIMUM(1), 0x00,          //   LOGICAL_MINIMUM(0)
  LOGICAL_MAXIMUM(1), 0x65,          //   LOGICAL_MAXIMUM(0x65) ; 101 keys
  USAGE_PAGE(1),      0x07,          //   USAGE_PAGE (Kbrd/Keypad)
  USAGE_MINIMUM(1),   0x00,          //   USAGE_MINIMUM (0)
  USAGE_MAXIMUM(1),   0x65,          //   USAGE_MAXIMUM (0x65)
  HIDINPUT(1),        0x00,          //   INPUT (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
  END_COLLECTION(0),                 // END_COLLECTION
  // ------------------------------------------------- Media Keys
  USAGE_PAGE(1),      0x0C,          // USAGE_PAGE (Consumer)
  USAGE(1),           0x01,          // USAGE (Consumer Control)
  COLLECTION(1),      0x01,          // COLLECTION (Application)
  REPORT_ID(1),       2, //   REPORT_ID (2)
  USAGE_PAGE(1),      0x0C,          //   USAGE_PAGE (Consumer)
  LOGICAL_MINIMUM(1), 0x00,          //   LOGICAL_MINIMUM (0)
  LOGICAL_MAXIMUM(1), 0x01,          //   LOGICAL_MAXIMUM (1)
  REPORT_SIZE(1),     0x01,          //   REPORT_SIZE (1)
  REPORT_COUNT(1),    0x10,          //   REPORT_COUNT (16)
  USAGE(1),           0xB5,          //   USAGE (Scan Next Track)     ; bit 0: 1
  USAGE(1),           0xB6,          //   USAGE (Scan Previous Track) ; bit 1: 2
  USAGE(1),           0xB7,          //   USAGE (Stop)                ; bit 2: 4
  USAGE(1),           0xCD,          //   USAGE (Play/Pause)          ; bit 3: 8
  USAGE(1),           0xE2,          //   USAGE (Mute)                ; bit 4: 16
  USAGE(1),           0xE9,          //   USAGE (Volume Increment)    ; bit 5: 32
  USAGE(1),           0xEA,          //   USAGE (Volume Decrement)    ; bit 6: 64
  USAGE(2),           0x23, 0x02,    //   Usage (WWW Home)            ; bit 7: 128
  USAGE(2),           0x94, 0x01,    //   Usage (My Computer) ; bit 0: 1
  USAGE(2),           0x92, 0x01,    //   Usage (Calculator)  ; bit 1: 2
  USAGE(2),           0x2A, 0x02,    //   Usage (WWW fav)     ; bit 2: 4
  USAGE(2),           0x21, 0x02,    //   Usage (WWW search)  ; bit 3: 8
  USAGE(2),           0x26, 0x02,    //   Usage (WWW stop)    ; bit 4: 16
  USAGE(2),           0x24, 0x02,    //   Usage (WWW back)    ; bit 5: 32
  USAGE(2),           0x83, 0x01,    //   Usage (Media sel)   ; bit 6: 64
  USAGE(2),           0x8A, 0x01,    //   Usage (Mail)        ; bit 7: 128
  HIDINPUT(1),        0x02,          //   INPUT (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  END_COLLECTION(0)                  // END_COLLECTION
};
esp_gatts_attr_db_t hidDb[HID_ATTRS], disDb[5];
esp_ble_adv_params_t adv = {};

void attribute(esp_gatts_attr_db_t& a, uint16_t& uuid, uint16_t perm,
               uint16_t len, uint8_t* value) {
    a = {};
    a.attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    a.att_desc = {ESP_UUID_LEN_16, reinterpret_cast<uint8_t*>(&uuid), perm, len, len, value};
}
void databases() {
    attribute(hidDb[0], primary, ESP_GATT_PERM_READ, 2, reinterpret_cast<uint8_t*>(&serviceUuid[0]));
    attribute(hidDb[1], declaration, ESP_GATT_PERM_READ, 1, &readProp);
    attribute(hidDb[2], infoUuid, ESP_GATT_PERM_READ, sizeof(info), info);
    attribute(hidDb[3], declaration, ESP_GATT_PERM_READ, 1, &readProp);
    attribute(hidDb[4], mapUuid, ESP_GATT_PERM_READ, sizeof(reportMap), reportMap);
    attribute(hidDb[5], declaration, ESP_GATT_PERM_READ, 1, &writeProp);
    attribute(hidDb[6], controlUuid, ESP_GATT_PERM_WRITE_ENCRYPTED, 1, &control);
    attribute(hidDb[7], declaration, ESP_GATT_PERM_READ, 1, &protocolProp);
    attribute(hidDb[8], protocolUuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED, 1, &protocol);
    attribute(hidDb[9], declaration, ESP_GATT_PERM_READ, 1, &reportProp);
    attribute(hidDb[10], reportUuid, ESP_GATT_PERM_READ_ENCRYPTED, sizeof(report), report);
    attribute(hidDb[11], cccUuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED, 2, ccc);
    attribute(hidDb[12], refUuid, ESP_GATT_PERM_READ, 2, ref);
    attribute(hidDb[13], declaration, ESP_GATT_PERM_READ, 1, &reportProp);
    attribute(hidDb[14], bootUuid, ESP_GATT_PERM_READ_ENCRYPTED, 8, bootReport);
    attribute(hidDb[15], cccUuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED, 2, bootCcc);
    attribute(hidDb[16], declaration, ESP_GATT_PERM_READ, 1, &outputProp);
    attribute(hidDb[17], reportUuid, ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED, 1, &keyboardOut);
    attribute(hidDb[18], refUuid, ESP_GATT_PERM_READ, 2, outputRef);
    attribute(hidDb[19], declaration, ESP_GATT_PERM_READ, 1, &reportProp);
    attribute(hidDb[20], reportUuid, ESP_GATT_PERM_READ_ENCRYPTED, 2, media);
    attribute(hidDb[21], cccUuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED, 2, mediaCcc);
    attribute(hidDb[22], refUuid, ESP_GATT_PERM_READ, 2, mediaRef);
    attribute(hidDb[23], declaration, ESP_GATT_PERM_READ, 1, &outputProp);
    attribute(hidDb[24], bootOutUuid, ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED, 1, &keyboardOut);
    attribute(disDb[0], primary, ESP_GATT_PERM_READ, 2, reinterpret_cast<uint8_t*>(&serviceUuid[1]));
    attribute(disDb[1], declaration, ESP_GATT_PERM_READ, 1, &readProp);
    attribute(disDb[2], manufacturerUuid, ESP_GATT_PERM_READ, sizeof(manufacturer) - 1, manufacturer);
    attribute(disDb[3], declaration, ESP_GATT_PERM_READ, 1, &readProp);
    attribute(disDb[4], pnpUuid, ESP_GATT_PERM_READ, sizeof(pnp), pnp);
}
void advertise() {
    if (active && services == 2 && advReady == 3 && !linked) {
        if (esp_ble_gap_start_advertising(&adv) != ESP_OK) error = true;
    }
}
void gap(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t* p) {
    if (!active) return;
    switch (e) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (p->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) { error = true; break; }
        advReady |= 1; advertise(); break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        if (p->scan_rsp_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) { error = true; break; }
        advReady |= 2; advertise(); break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (p->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) error = true;
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT: esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true); break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        secure = p->ble_security.auth_cmpl.success;
        if (!secure) esp_ble_gap_disconnect(p->ble_security.auth_cmpl.bd_addr);
        break;
    default: break;
    }
}
void gatts(esp_gatts_cb_event_t e, esp_gatt_if_t i, esp_ble_gatts_cb_param_t* p) {
    if (!active) return;
    if (e == ESP_GATTS_REG_EVT) {
        if (p->reg.app_id != APP_ID) return;
        if (p->reg.status != ESP_GATT_OK) { error = true; return; }
        iface = i;
        if (esp_ble_gatts_create_attr_tab(hidDb, i, HID_ATTRS, 0) != ESP_OK ||
            esp_ble_gatts_create_attr_tab(disDb, i, 5, 1) != ESP_OK) error = true;
        return;
    }
    if (i != iface && i != ESP_GATT_IF_NONE) return;
    switch (e) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
        unsigned id = p->add_attr_tab.svc_inst_id;
        if (p->add_attr_tab.status != ESP_GATT_OK || id > 1 ||
            p->add_attr_tab.num_handle != (id ? 5 : HID_ATTRS)) { error = true; break; }
        memcpy(handles[id], p->add_attr_tab.handles, p->add_attr_tab.num_handle * sizeof(uint16_t));
        if (esp_ble_gatts_start_service(handles[id][0]) != ESP_OK) error = true;
        break;
    }
    case ESP_GATTS_START_EVT:
        if (p->start.status != ESP_GATT_OK) { error = true; break; }
        ++services; advertise(); break;
    case ESP_GATTS_CONNECT_EVT: {
        conn = p->connect.conn_id; memcpy(peer, p->connect.remote_bda, 6);
        linked = true; secure = false; subscribed = false; bootSubscribed = false; mediaSubscribed = false;
        suspended = false; congested = false; ++epoch;
        memset(report, 0, sizeof(report)); memset(ccc, 0, sizeof(ccc)); memset(mediaCcc, 0, sizeof(mediaCcc));
        protocol = 1; protocolMode = 1;
        esp_ble_gatts_set_attr_value(handles[0][11], 2, ccc);
        esp_ble_gatts_set_attr_value(handles[0][15], 2, ccc);
        esp_ble_gatts_set_attr_value(handles[0][21], 2, mediaCcc);
        esp_ble_gatts_set_attr_value(handles[0][8], 1, &protocol);
        esp_ble_set_encryption(peer, ESP_BLE_SEC_ENCRYPT);
        esp_ble_conn_update_params_t params = {};
        memcpy(params.bda, peer, 6); params.min_int = 12; params.max_int = 24;
        params.latency = 0; params.timeout = 400; esp_ble_gap_update_conn_params(&params);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        linked = false; secure = false; subscribed = false; bootSubscribed = false; mediaSubscribed = false;
        ++epoch; advertise(); break;
    case ESP_GATTS_CONGEST_EVT: congested = p->congest.congested; break;
    case ESP_GATTS_WRITE_EVT:
        if (p->write.is_prep) break;
        if (p->write.handle == handles[0][11] && p->write.len == 2) subscribed = (p->write.value[0] & 1) != 0;
        if (p->write.handle == handles[0][15] && p->write.len == 2) bootSubscribed = (p->write.value[0] & 1) != 0;
        if (p->write.handle == handles[0][21] && p->write.len == 2) mediaSubscribed = (p->write.value[0] & 1) != 0;
        if (p->write.handle == handles[0][6] && p->write.len == 1) suspended = p->write.value[0] == 0;
        if (p->write.handle == handles[0][8] && p->write.len == 1) protocolMode = p->write.value[0];
        break;
    default: break;
    }
}
bool linkReady() {
    return active && linked && secure && !suspended && !error &&
           (protocolMode == 0 ? bootSubscribed.load() : (subscribed.load() && mediaSubscribed.load()));
}
bool send(const Item& it, bool force = false) {
    if (!linkReady() || (congested && !force)) return false;
    if (it.kind == 0) {
        uint8_t data[8] = {it.mods, 0, static_cast<uint8_t>(it.value), 0, 0, 0, 0, 0};
        const uint16_t handle = handles[0][protocolMode == 0 ? 14 : 10];
        return esp_ble_gatts_send_indicate(iface, conn, handle, 8, data, false) == ESP_OK;
    }
    if (protocolMode == 0) return true;  // Boot protocol has no consumer control; drop.
    uint8_t data[2] = {static_cast<uint8_t>(it.value), static_cast<uint8_t>(it.value >> 8)};
    return esp_ble_gatts_send_indicate(iface, conn, handles[0][20], 2, data, false) == ESP_OK;
}
bool enqueueTap(uint8_t kind, uint8_t mods, uint16_t value) {
    if (!linkReady() || !value) return false;
    const unsigned used = (tail + QUEUE_LEN - head) % QUEUE_LEN;
    if (used + 2 >= QUEUE_LEN) return false;  // Keep one slot free; never split a tap.
    queue[tail] = {kind, mods, value}; tail = (tail + 1) % QUEUE_LEN;
    queue[tail] = {kind, 0, 0};        tail = (tail + 1) % QUEUE_LEN;
    return true;
}
void stopped() {
    // Called by bluetooth_manager while the stack is still valid: release
    // every key and media bit so nothing stays held on the host.
    if (!active || !linked || !secure || iface == ESP_GATT_IF_NONE) return;
    send({0, 0, 0}, true);
    send({1, 0, 0}, true);
}
}
namespace ble_remote_hid {
bool begin() {
    if (bluetooth_manager::owns(lease)) return !error;
    // Both Media Remote and Slide Clicker share this name/address so the host pairs once.
    lease = bluetooth_manager::acquire(bluetooth_manager::Owner::HidRemote, "MeowKit Remote", gap, gatts, stopped);
    if (!lease) { active = false; error = true; return false; }
    active = true; linked = false; secure = false; subscribed = false; bootSubscribed = false;
    mediaSubscribed = false; suspended = false; congested = false; error = false;
    services = 0; advReady = 0; iface = ESP_GATT_IF_NONE; head = tail = 0;
    startedAt = millis(); lastSend = startedAt; seenEpoch = epoch.load();
    databases();
    adv = {}; adv.adv_int_min = 0x30; adv.adv_int_max = 0x60;
    adv.adv_type = ADV_TYPE_IND; adv.own_addr_type = BLE_ADDR_TYPE_RANDOM;
    adv.channel_map = ADV_CHNL_ALL; adv.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    uint8_t auth = ESP_LE_AUTH_BOND, io = ESP_IO_CAP_NONE, keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK, size = 16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &size, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1);
    static uint8_t ad[] = {2, 1, 6, 3, 3, 0x12, 0x18, 3, 0x19, 0xC1, 3};  // appearance: keyboard
    static uint8_t name[] = {15, 9, 'M','e','o','w','K','i','t',' ','R','e','m','o','t','e'};
    // Static random identity, derived like Air Mouse but with a distinct tweak,
    // so host caches never mix this GATT layout with another profile.
    const uint8_t* baseAddress = esp_bt_dev_get_address();
    if (!baseAddress) { end(); error = true; return false; }
    uint8_t address[6]; memcpy(address, baseAddress, 6);
    address[0] |= 0xC0; address[5] ^= 0x0B;
    if (esp_ble_gap_set_rand_addr(address) != ESP_OK ||
        esp_ble_gap_config_adv_data_raw(ad, sizeof(ad)) != ESP_OK ||
        esp_ble_gap_config_scan_rsp_data_raw(name, sizeof(name)) != ESP_OK ||
        esp_ble_gatts_app_register(APP_ID) != ESP_OK) error = true;
    if (error) { end(); error = true; return false; }
    return true;
}
void end() {
    if (!active && !lease) return;
    // The manager runs stopped() (neutral keyboard + media reports) while the
    // stack is valid, then stops advertising, disconnects and deinitialises.
    bluetooth_manager::release(lease); lease = 0;
    active = false; linked = false; secure = false; subscribed = false;
    bootSubscribed = false; mediaSubscribed = false; iface = ESP_GATT_IF_NONE;
    head = tail = 0;
}
bool connected() { return active && linked; }
bool ready() { return linkReady(); }
bool failed() {
    return error || (active && !bluetooth_manager::owns(lease)) ||
           (active && services != 2 && millis() - startedAt > 5000);
}
bool tapKey(uint8_t usage, uint8_t mods) { return enqueueTap(0, mods, usage); }
bool tapMedia(uint16_t bits) { return enqueueTap(1, 0, bits); }
void tick() {
    if (seenEpoch != epoch.load()) { seenEpoch = epoch.load(); head = tail = 0; }
    if (!linkReady()) { head = tail = 0; return; }
    if (head != tail && millis() - lastSend >= 10 && send(queue[head])) {
        head = (head + 1) % QUEUE_LEN; lastSend = millis();
    }
}
}

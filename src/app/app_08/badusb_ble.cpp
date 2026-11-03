// Static keyboard GATT lifecycle; ASCII map/report descriptor adapted from the
// existing vendored ESP32-BLE-Keyboard/BleKeyboard.cpp. No BLEServer objects retained.
#include "badusb_ble.h"
#ifdef BADUSB_BLE_HOST_TEST
#include "../../../test/air_mouse_hid_stubs.h"
#include "../../../test/bluetooth_lease_stubs.h"
#else
#include "../../system/bluetooth_manager.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_bt_device.h>
#endif
#include <HIDTypes.h>
namespace keyboard_transport { bool ready(); }
#include <atomic>
#include <cstring>

namespace {
constexpr uint16_t APP_ID = 0x800;
std::atomic<bool> active{false}, linked{false}, secure{false}, subscribed{false}, error{false};
std::atomic<bool> bootSubscribed{false};
std::atomic<bool> congested{false}, suspended{false};
std::atomic<unsigned> epoch{0};
std::atomic<bool> sendFault{false};
esp_gatt_if_t iface = ESP_GATT_IF_NONE;
bluetooth_manager::Lease lease=0;
uint16_t conn = 0, handles[2][25] = {};
uint8_t peer[6] = {};
std::atomic<unsigned> services{0};
unsigned advReady = 0;
uint32_t lastSend = 0, startedAt = 0;

uint16_t serviceUuid[] = {0x1812, 0x180A};
uint16_t primary = 0x2800, declaration = 0x2803;
uint16_t infoUuid = 0x2A4A, mapUuid = 0x2A4B, controlUuid = 0x2A4C;
uint16_t protocolUuid = 0x2A4E, reportUuid = 0x2A4D, cccUuid = 0x2902, refUuid = 0x2908;
uint16_t bootUuid = 0x2A22, bootOutUuid=0x2A32;
uint8_t outputProp=ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t keyboardOut=0, outputRef[]={1,2}, mediaRef[]={2,1}, media[2]={}, mediaCcc[2]={};
uint16_t manufacturerUuid = 0x2A29, pnpUuid = 0x2A50;
uint8_t readProp = ESP_GATT_CHAR_PROP_BIT_READ;
uint8_t writeProp = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t protocolProp = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
uint8_t reportProp = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
uint8_t info[] = {0x11, 0x01, 0, 2}, control = 0, protocol = 1;
std::atomic<uint8_t> protocolMode{1};
uint8_t report[8] = {}, ccc[2] = {}, ref[] = {1, 1};
uint8_t bootReport[8] = {}, bootCcc[2] = {};
uint8_t manufacturer[] = "FancyKitty", pnp[] = {2, 0xAC, 0x05, 0x0A, 0x82, 0x10, 0x02};
// Battery level is not exposed: the board has no validated percentage source.
// Report protocol matches the previous five-byte mouse descriptor. Boot
// protocol exposes three-byte pointer reports for hosts that request it.
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
  REPORT_ID(1),       2, //   REPORT_ID (3)
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
esp_gatts_attr_db_t hidDb[25], disDb[5];
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
    attribute(hidDb[17], reportUuid, ESP_GATT_PERM_READ_ENCRYPTED|ESP_GATT_PERM_WRITE_ENCRYPTED, 1, &keyboardOut);
    attribute(hidDb[18], refUuid, ESP_GATT_PERM_READ, 2, outputRef);
    attribute(hidDb[19], declaration, ESP_GATT_PERM_READ, 1, &reportProp);
    attribute(hidDb[20], reportUuid, ESP_GATT_PERM_READ_ENCRYPTED, 2, media);
    attribute(hidDb[21], cccUuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE_ENCRYPTED, 2, mediaCcc);
    attribute(hidDb[22], refUuid, ESP_GATT_PERM_READ, 2, mediaRef);
    attribute(hidDb[23], declaration, ESP_GATT_PERM_READ, 1, &outputProp);
    attribute(hidDb[24], bootOutUuid, ESP_GATT_PERM_READ_ENCRYPTED|ESP_GATT_PERM_WRITE_ENCRYPTED, 1, &keyboardOut);

    attribute(disDb[0], primary, ESP_GATT_PERM_READ, 2, reinterpret_cast<uint8_t*>(&serviceUuid[1]));
    attribute(disDb[1], declaration, ESP_GATT_PERM_READ, 1, &readProp);
    attribute(disDb[2], manufacturerUuid, ESP_GATT_PERM_READ, sizeof(manufacturer)-1, manufacturer);
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
    switch(e) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (p->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) { error=true; break; }
        advReady |= 1; advertise(); break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        if (p->scan_rsp_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) { error=true; break; }
        advReady |= 2; advertise(); break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (p->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) error=true;
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
        if (p->reg.status != ESP_GATT_OK) { error=true; return; }
        iface=i;
        if (esp_ble_gatts_create_attr_tab(hidDb, i, 25, 0) != ESP_OK ||
            esp_ble_gatts_create_attr_tab(disDb, i, 5, 1) != ESP_OK) error=true;
        return;
    }
    if (i != iface && i != ESP_GATT_IF_NONE) return;
    switch(e) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
        unsigned id=p->add_attr_tab.svc_inst_id;
        if (p->add_attr_tab.status != ESP_GATT_OK || id>1 || p->add_attr_tab.num_handle != (id ? 5 : 25)) { error=true; break; }
        memcpy(handles[id], p->add_attr_tab.handles, p->add_attr_tab.num_handle*sizeof(uint16_t));
        if (esp_ble_gatts_start_service(handles[id][0]) != ESP_OK) error=true;
        break;
    }
    case ESP_GATTS_START_EVT:
        if (p->start.status != ESP_GATT_OK) { error=true; break; }
        ++services; advertise(); break;
    case ESP_GATTS_CONNECT_EVT: {
        conn=p->connect.conn_id; memcpy(peer,p->connect.remote_bda,6);
        linked=true; secure=false; subscribed=false; bootSubscribed=false; suspended=false; congested=false; ++epoch;
        memset(report,0,sizeof(report)); memset(ccc,0,sizeof(ccc)); protocol=1; protocolMode=1;
        esp_ble_gatts_set_attr_value(handles[0][11],2,ccc);
        esp_ble_gatts_set_attr_value(handles[0][15],2,ccc);
        esp_ble_gatts_set_attr_value(handles[0][8],1,&protocol);
        esp_ble_set_encryption(peer, ESP_BLE_SEC_ENCRYPT);
        esp_ble_conn_update_params_t params = {};
        memcpy(params.bda,peer,6); params.min_int=12; params.max_int=24;
        params.latency=0; params.timeout=400; esp_ble_gap_update_conn_params(&params);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        linked=false; secure=false; subscribed=false; bootSubscribed=false;
        ++epoch; sendFault=true; break; // Reopen the script; never replay after reconnect.
    case ESP_GATTS_CONGEST_EVT: congested=p->congest.congested; break;
    case ESP_GATTS_WRITE_EVT:
        if (p->write.is_prep) break;
        if (p->write.handle == handles[0][11] && p->write.len==2) subscribed=(p->write.value[0]&1)!=0;
        if (p->write.handle == handles[0][15] && p->write.len==2) bootSubscribed=(p->write.value[0]&1)!=0;
        if (p->write.handle == handles[0][6] && p->write.len==1) suspended=p->write.value[0]==0;
        if (p->write.handle == handles[0][8] && p->write.len==1) protocolMode=p->write.value[0];
        break;
    default: break;
    }
}

bool sendKeys(const uint8_t* data) {
    if (!keyboard_transport::ready() || congested) return false;
    return esp_ble_gatts_send_indicate(iface,conn,handles[0][protocolMode==0?14:10],8,
                                      const_cast<uint8_t*>(data),false)==ESP_OK;
}
void stopped() {
    // Try to release held keys even if the preceding report failed.
    if (bluetooth_manager::owns(lease) && linked && secure && !congested) {
        uint8_t neutral[8]={};
        esp_ble_gatts_send_indicate(iface,conn,handles[0][protocolMode==0?14:10],8,neutral,false);
    }
    active=false; linked=false; secure=false; subscribed=false; error=false;
}
uint8_t keys[8]={};
unsigned keyEpoch=0;
const uint8_t asciiMap[128] =
{
	0x00,             // NUL
	0x00,             // SOH
	0x00,             // STX
	0x00,             // ETX
	0x00,             // EOT
	0x00,             // ENQ
	0x00,             // ACK
	0x00,             // BEL
	0x2a,			// BS	Backspace
	0x2b,			// TAB	Tab
	0x28,			// LF	Enter
	0x00,             // VT
	0x00,             // FF
	0x00,             // CR
	0x00,             // SO
	0x00,             // SI
	0x00,             // DEL
	0x00,             // DC1
	0x00,             // DC2
	0x00,             // DC3
	0x00,             // DC4
	0x00,             // NAK
	0x00,             // SYN
	0x00,             // ETB
	0x00,             // CAN
	0x00,             // EM
	0x00,             // SUB
	0x00,             // ESC
	0x00,             // FS
	0x00,             // GS
	0x00,             // RS
	0x00,             // US

	0x2c,		   //  ' '
	0x1e|0x80,	   // !
	0x34|0x80,	   // "
	0x20|0x80,    // #
	0x21|0x80,    // $
	0x22|0x80,    // %
	0x24|0x80,    // &
	0x34,          // '
	0x26|0x80,    // (
	0x27|0x80,    // )
	0x25|0x80,    // *
	0x2e|0x80,    // +
	0x36,          // ,
	0x2d,          // -
	0x37,          // .
	0x38,          // /
	0x27,          // 0
	0x1e,          // 1
	0x1f,          // 2
	0x20,          // 3
	0x21,          // 4
	0x22,          // 5
	0x23,          // 6
	0x24,          // 7
	0x25,          // 8
	0x26,          // 9
	0x33|0x80,      // :
	0x33,          // ;
	0x36|0x80,      // <
	0x2e,          // =
	0x37|0x80,      // >
	0x38|0x80,      // ?
	0x1f|0x80,      // @
	0x04|0x80,      // A
	0x05|0x80,      // B
	0x06|0x80,      // C
	0x07|0x80,      // D
	0x08|0x80,      // E
	0x09|0x80,      // F
	0x0a|0x80,      // G
	0x0b|0x80,      // H
	0x0c|0x80,      // I
	0x0d|0x80,      // J
	0x0e|0x80,      // K
	0x0f|0x80,      // L
	0x10|0x80,      // M
	0x11|0x80,      // N
	0x12|0x80,      // O
	0x13|0x80,      // P
	0x14|0x80,      // Q
	0x15|0x80,      // R
	0x16|0x80,      // S
	0x17|0x80,      // T
	0x18|0x80,      // U
	0x19|0x80,      // V
	0x1a|0x80,      // W
	0x1b|0x80,      // X
	0x1c|0x80,      // Y
	0x1d|0x80,      // Z
	0x2f,          // [
	0x31,          // bslash
	0x30,          // ]
	0x23|0x80,    // ^
	0x2d|0x80,    // _
	0x35,          // `
	0x04,          // a
	0x05,          // b
	0x06,          // c
	0x07,          // d
	0x08,          // e
	0x09,          // f
	0x0a,          // g
	0x0b,          // h
	0x0c,          // i
	0x0d,          // j
	0x0e,          // k
	0x0f,          // l
	0x10,          // m
	0x11,          // n
	0x12,          // o
	0x13,          // p
	0x14,          // q
	0x15,          // r
	0x16,          // s
	0x17,          // t
	0x18,          // u
	0x19,          // v
	0x1a,          // w
	0x1b,          // x
	0x1c,          // y
	0x1d,          // z
	0x2f|0x80,    // {
	0x31|0x80,    // |
	0x30|0x80,    // }
	0x35|0x80,    // ~
	0				// DEL
};
void clearKeys() { memset(keys,0,sizeof(keys)); keyEpoch=epoch.load(); }
bool transmit() {
    if (keyEpoch!=epoch.load()) { sendFault=true; clearKeys(); return false; }
    const uint32_t started=millis();
    while (keyboard_transport::ready() && millis()-started<100) {
        if (millis()-lastSend>=10 && sendKeys(keys)) { lastSend=millis(); return true; }
        delay(1);
    }
    sendFault=true; clearKeys(); return false;
}
}
namespace keyboard_transport {
bool ready() {
    return bluetooth_manager::owns(lease) && active && linked && secure &&
        (protocolMode==0?bootSubscribed.load():subscribed.load()) && !suspended && !error && !sendFault;
}
}
bool bu_ble_begin() {
    if (bluetooth_manager::owns(lease)) return !error;
    lease=bluetooth_manager::acquire(bluetooth_manager::Owner::BadUsb,"FancyKitty BadUSB",gap,gatts,stopped);
    if (!lease) return false;
    active=true; linked=false; secure=false; subscribed=false; bootSubscribed=false; error=false;
    suspended=false; congested=false; sendFault=false; clearKeys();
    services=advReady=0; iface=ESP_GATT_IF_NONE; startedAt=millis(); lastSend=startedAt;
    databases();
    adv={}; adv.adv_int_min=0x30; adv.adv_int_max=0x60;
    adv.adv_type=ADV_TYPE_IND; adv.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
    adv.channel_map=ADV_CHNL_ALL; adv.adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    uint8_t auth=ESP_LE_AUTH_BOND, io=ESP_IO_CAP_NONE, mask=ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK, size=16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,&auth,1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,&io,1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,&size,1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,&mask,1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,&mask,1);
    static uint8_t ad[]={2,1,6,3,3,0x12,0x18,3,0x19,0xC1,3};
    static uint8_t name[]={17,9,'F','a','n','c','y','K','i','t','t','y',' ','B','a','d','U','S','B'};
    if (esp_ble_gap_config_adv_data_raw(ad,sizeof(ad))!=ESP_OK ||
        esp_ble_gap_config_scan_rsp_data_raw(name,sizeof(name))!=ESP_OK ||
        esp_ble_gatts_app_register(APP_ID)!=ESP_OK) error=true;
    if (error) { bu_ble_end(); return false; }
    return true;
}
void bu_ble_end() {
    bluetooth_manager::release(lease); lease=0; active=false; clearKeys();
}
bool bu_ble_connected() { return keyboard_transport::ready(); }
bool bu_ble_failed() {
    return error || sendFault || bluetooth_manager::failed() ||
        (active && services!=2 && millis()-startedAt>5000);
}
void bu_ble_press(uint8_t key) {
    if (!bu_ble_connected()) { clearKeys(); return; }
    if (keyEpoch!=epoch.load()) clearKeys();
    if (key>=136) key-=136;
    else if (key>=128) { keys[0]|=1<<(key-128); key=0; }
    else {
        key=asciiMap[key]; if (!key) return;
        if (key&0x80) { keys[0]|=2; key&=0x7f; }
    }
    if (key) {
        bool found=false;
        for (unsigned i=2;i<8;++i) if(keys[i]==key) found=true;
        if (!found) for(unsigned i=2;i<8;++i) if(!keys[i]) { keys[i]=key; found=true; break; }
        if(!found) { sendFault=true; clearKeys(); return; }
    }
    transmit();
}
void bu_ble_release(uint8_t key) {
    if (!bu_ble_connected() || keyEpoch!=epoch.load()) { clearKeys(); return; }
    if (key>=136) key-=136;
    else if (key>=128) { keys[0]&=~(1<<(key-128)); key=0; }
    else {
        key=asciiMap[key]; if(!key) return;
        if(key&0x80) { keys[0]&=~2; key&=0x7f; }
    }
    if(key) for(unsigned i=2;i<8;++i) if(keys[i]==key) keys[i]=0;
    transmit();
}
void bu_ble_release_all() { clearKeys(); if(bu_ble_connected()) transmit(); }
void bu_ble_write(uint8_t c) { if(!bu_ble_connected()) return; bu_ble_press(c); bu_ble_release(c); }
void bu_ble_print(const char* s) { while(s && *s && bu_ble_connected()) bu_ble_write(static_cast<uint8_t>(*s++)); }

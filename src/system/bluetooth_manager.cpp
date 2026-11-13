#include "bluetooth_manager.h"
#ifndef BLUETOOTH_MANAGER_HOST_TEST
#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_bt_main.h>
#endif
#include <atomic>
#include <cstring>

namespace {
using namespace bluetooth_manager;
std::atomic<bool> allowed{false}, closing{false}, fault{false};
std::atomic<Lease> current{0};
std::atomic<Owner> holder{Owner::None};
std::atomic<bool> linked{false}, stopDone{true};
Lease sequence=0;
uint8_t peer[6]={};
esp_gap_ble_cb_t gapHandler=nullptr;
esp_gatts_cb_t gattsHandler=nullptr;
Stop stopHandler=nullptr;

void gapDispatch(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p) {
    if (event==ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT)
        stopDone=true; // Completion also covers an already-stopped advertiser.
    if (!closing && current && gapHandler) gapHandler(event,p);
}
void gattsDispatch(esp_gatts_cb_event_t event, esp_gatt_if_t iface, esp_ble_gatts_cb_param_t* p) {
    if (event==ESP_GATTS_CONNECT_EVT) {
        std::memcpy(peer,p->connect.remote_bda,6);
        linked=true;
        if (closing) esp_ble_gap_disconnect(peer);
    }
    if (event==ESP_GATTS_DISCONNECT_EVT) linked=false;
    if (!closing && current && gattsHandler) gattsHandler(event,iface,p);
}
}
namespace bluetooth_manager {
void setEnabled(bool on) {
    // Called from the same launcher/UI task as acquire/release.
    if (!on && current) release(current.load());
    if (on && !current && esp_bluedroid_get_status()==ESP_BLUEDROID_STATUS_UNINITIALIZED)
        fault=false; // Explicit off/on retries a fully stopped stack.
    allowed=on;
}
bool enabled() { return allowed; }
bool failed() { return fault; }
Owner owner() { return holder; }
bool owns(Lease lease) { return lease && lease==current && !closing && !fault; }

Lease acquire(Owner who,const char* name,esp_gap_ble_cb_t gap,
              esp_gatts_cb_t gatts,Stop stop) {
    if (!allowed || who==Owner::None || current || closing || fault) return 0;
    // No app may retain Arduino BLEServer objects across this boundary.
    BLEDevice::init(name);
    if (esp_bluedroid_get_status()!=ESP_BLUEDROID_STATUS_ENABLED) {
        BLEDevice::deinit(false); fault=true; return 0;
    }
    gapHandler=gap; gattsHandler=gatts; stopHandler=stop;
    linked=false; stopDone=true;
    if (!++sequence) ++sequence;
    holder=who; current=sequence;
    if (esp_ble_gap_register_callback(gapDispatch)!=ESP_OK ||
        esp_ble_gatts_register_callback(gattsDispatch)!=ESP_OK) {
        // The app never received this lease (acquire returns 0): do not run its
        // stop handler for a lease it never held.
        stopHandler=nullptr;
        release(sequence); fault=true; return 0;
    }
    Serial.printf("[BT] acquire owner=%u session=%lu\n",unsigned(who),static_cast<unsigned long>(sequence));
    return sequence;
}

void release(Lease lease) {
    if (!lease || current!=lease || closing) return;
    if (stopHandler) stopHandler(); // neutral report / producer cleanup while still valid
    closing=true; // discard profile callbacks; dispatcher still observes disconnect/stop
    stopDone=false;
    const esp_err_t stopped=esp_ble_gap_stop_advertising();
    if (stopped!=ESP_OK) stopDone=true; // no active advertising operation
    esp_ble_gap_stop_scanning();
    if (linked) esp_ble_gap_disconnect(peer);
    const uint32_t started=millis();
    while ((!stopDone || linked) && millis()-started<1500) delay(1);
    const bool timedOut=!stopDone || linked;
    // Always invalidate all profile handles. Never free BLE controller memory:
    // Bluetooth must remain restartable without a system reboot.
    BLEDevice::deinit(false);
    current=0; holder=Owner::None;
    gapHandler=nullptr; gattsHandler=nullptr; stopHandler=nullptr;
    linked=false; closing=false;
    // A slow peer that missed the disconnect/stop deadline is not a fault once
    // the stack is down: latching it would block Bluetooth for every app until
    // the user toggled the setting. Only a stack that failed to deinitialise is.
    fault=esp_bluedroid_get_status()!=ESP_BLUEDROID_STATUS_UNINITIALIZED;
    Serial.printf("[BT] release session=%lu status=%s%s\n",static_cast<unsigned long>(lease),
                  fault?"error":"idle",timedOut?" (peer timeout)":"");
}
}

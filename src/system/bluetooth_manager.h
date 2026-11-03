#pragma once
#include <cstdint>
#ifdef BLUETOOTH_MANAGER_HOST_TEST
#include "../../test/bluetooth_manager_stubs.h"
#else
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#endif

// Lifecycle operations belong to the launcher task, never a BLE callback.
namespace bluetooth_manager {
enum class Owner : uint8_t { None, AirMouse, BadUsb, Other, HidRemote, Scanner };
using Lease = uint32_t;
using Stop = void (*)();
void setEnabled(bool enabled);
bool enabled();
Lease acquire(Owner owner, const char* name, esp_gap_ble_cb_t gap,
              esp_gatts_cb_t gatts, Stop stop);
void release(Lease lease);
bool owns(Lease lease);
bool failed();
Owner owner();
}

/**
 * @file recovery.cpp
 * @brief System recovery utilities.
 */
#include "recovery.h"
#include "persist.h"
#include "../ui/ui_wifi_bridge.h"
#include <Arduino.h>
#include <esp_system.h>

extern "C" void recovery_factory_reset(void)
{
    Serial.println("[Recovery] Factory reset — erasing NVS...");
    Serial.flush();
    persist_erase_all();
    ui_wifi_bridge_wipe_stored_config();   /* IDF-side WiFi creds */
    esp_restart();
}

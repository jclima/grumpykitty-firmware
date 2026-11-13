/**
 * @file recovery.cpp
 * @brief System recovery utilities.
 */
#include "recovery.h"
#include "persist.h"
#include "../ui/ui_wifi_bridge.h"
#include <Arduino.h>
#include <esp_system.h>
#include <nvs_flash.h>

extern "C" void recovery_factory_reset(void)
{
    Serial.println("[Recovery] Factory reset — erasing NVS...");
    Serial.flush();
    persist_erase_all();                   /* fallback if the full erase below fails */
    ui_wifi_bridge_wipe_stored_config();   /* IDF-side WiFi creds */

    /* Full NVS erase: also drops Bluetooth bonds and the other namespaces
     * (airmouse, mk_bbt, mk_app_ui, ...). Only the "nvs" partition is touched;
     * otadata and the app slots are separate partitions, so OTA slot selection
     * is unaffected. Next boot re-creates the schema from scratch. */
    persist_deinit();
    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED)
        Serial.printf("[Recovery] nvs_flash_deinit failed: 0x%x\n", (unsigned)err);
    err = nvs_flash_erase();
    if (err != ESP_OK)
        Serial.printf("[Recovery] nvs_flash_erase failed: 0x%x\n", (unsigned)err);
    Serial.flush();
    esp_restart();
}

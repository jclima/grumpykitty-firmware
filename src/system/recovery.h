/**
 * @file recovery.h
 * @brief System recovery utilities.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Erase the whole NVS partition (settings, WiFi credentials, Bluetooth bonds
 * and every other namespace) then restart.  Everything reverts to firmware
 * defaults on next boot.  otadata / app slots are not touched.
 */
void recovery_factory_reset(void);

#ifdef __cplusplus
}
#endif

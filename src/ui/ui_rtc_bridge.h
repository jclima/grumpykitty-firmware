/**
 * @file ui_rtc_bridge.h
 * @brief C-ABI bridge so generated LVGL UI screens (C) can read/write
 *        the PCF8563 RTC owned by the C++ DEVICES/Launcher layer.
 */
#ifndef UI_RTC_BRIDGE_H
#define UI_RTC_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Register the underlying PCF8563_Class* (called once by launcher). */
void ui_rtc_bridge_register(void * pcf8563_ptr);

/** Read current RTC time. Returns false if RTC not available. */
bool ui_rtc_bridge_get(uint16_t * year, uint8_t * month, uint8_t * day,
                       uint8_t * weekday,
                       uint8_t * hour, uint8_t * minute, uint8_t * second);

/** Write date (year/month/day). Weekday is auto-computed. Keeps current H:M:S. */
bool ui_rtc_bridge_set_date(uint16_t year, uint8_t month, uint8_t day);

/** Write time-of-day. Keeps current date/weekday. */
bool ui_rtc_bridge_set_time(uint8_t hour, uint8_t minute, uint8_t second);

/** Write weekday only (0=Sun..6=Sat). Keeps current date and H:M:S. */
bool ui_rtc_bridge_set_weekday(uint8_t weekday);
bool ui_rtc_bridge_date_valid(uint16_t year, uint8_t month, uint8_t day);

#ifdef __cplusplus
}
#endif

#endif /* UI_RTC_BRIDGE_H */

/**
 * @file ui_rtc_bridge.cpp
 * @brief C-ABI bridge implementation — forwards to PCF8563_Class.
 */
#include "ui_rtc_bridge.h"
#include "../bsp/rtc/PCF8563.hpp"

static PCF8563_Class * s_rtc = nullptr;

/* Zeller's congruence — returns 0=Sun..6=Sat */
static uint8_t compute_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    if (m < 3) { m += 12; y -= 1; }
    uint16_t K = y % 100;
    uint16_t J = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* Zeller: 0=Sat..6=Fri  →  remap to 0=Sun..6=Sat */
    int wd = (h + 6) % 7;
    return (uint8_t)wd;
}

extern "C" void ui_rtc_bridge_register(void * pcf8563_ptr)
{
    s_rtc = static_cast<PCF8563_Class *>(pcf8563_ptr);
}

extern "C" bool ui_rtc_bridge_get(uint16_t * year, uint8_t * month, uint8_t * day,
                                  uint8_t * weekday,
                                  uint8_t * hour, uint8_t * minute, uint8_t * second)
{
    if (!s_rtc) return false;
    RTC_Time t;
    if (!s_rtc->getTime(t)) return false;
    if (year)    *year    = t.year;
    if (month)   *month   = t.month;
    if (day)     *day     = t.day;
    if (weekday) *weekday = t.weekday;
    if (hour)    *hour    = t.hour;
    if (minute)  *minute  = t.min;
    if (second)  *second  = t.sec;
    return true;
}

extern "C" bool ui_rtc_bridge_set_date(uint16_t year, uint8_t month, uint8_t day)
{
    if (!s_rtc || !PCF8563_Class::isDateValid(year, month, day)) return false;
    RTC_Time t;
    if (!s_rtc->getTime(t)) {
        t.hour = 0; t.min = 0; t.sec = 0;
    }
    t.year    = year;
    t.month   = month;
    t.day     = day;
    t.weekday = compute_weekday(year, month, day);
    return s_rtc->setTime(t);
}

extern "C" bool ui_rtc_bridge_date_valid(uint16_t year, uint8_t month, uint8_t day)
{
    return PCF8563_Class::isDateValid(year, month, day);
}

extern "C" bool ui_rtc_bridge_set_weekday(uint8_t weekday)
{
    if (!s_rtc) return false;
    RTC_Time t;
    if (!s_rtc->getTime(t)) return false;
    t.weekday = weekday;
    return s_rtc->setTime(t);
}

extern "C" bool ui_rtc_bridge_set_time(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!s_rtc) return false;
    RTC_Time t;
    if (!s_rtc->getTime(t)) {
        t.year = 2026; t.month = 1; t.day = 1; t.weekday = 4;
    }
    t.hour = hour;
    t.min  = minute;
    t.sec  = second;
    return s_rtc->setTime(t);
}
